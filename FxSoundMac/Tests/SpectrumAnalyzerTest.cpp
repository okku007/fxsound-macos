#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>
#include "SpectrumAnalyzer.h"

namespace {
// Feeds a steady sine through the analyzer for enough frames that the smoothing
// settles, then returns the display-point index with the highest magnitude.
int peakPointForSine(SpectrumAnalyzer& analyzer, double sampleRate, double freqHz)
{
    const int blockFrames = 512;
    std::vector<float> interleaved(2 * blockFrames);

    double phase = 0.0;
    const double phaseInc = juce::MathConstants<double>::twoPi * freqHz / sampleRate;

    // Enough blocks to fill the FFT window several times over and settle the smoothing.
    // Derived from kFftSize so it does not rot if the transform size changes.
    const int numBlocks = juce::jmax(60, 4 * SpectrumAnalyzer::kFftSize / blockFrames);

    for (int b = 0; b < numBlocks; ++b)
    {
        for (int i = 0; i < blockFrames; ++i)
        {
            const auto s = static_cast<float>(std::sin(phase));
            interleaved[2 * i]     = s;
            interleaved[2 * i + 1] = s;
            phase += phaseInc;
        }

        analyzer.pushInterleavedStereo(interleaved.data(), blockFrames);
        analyzer.update();
    }

    const auto& mags = analyzer.getMagnitudesDb();
    int peak = 0;
    for (int i = 1; i < static_cast<int>(mags.size()); ++i)
        if (mags[i] > mags[peak])
            peak = i;

    return peak;
}
} // namespace

struct SpectrumAnalyzerTest : juce::UnitTest
{
    SpectrumAnalyzerTest() : juce::UnitTest("SpectrumAnalyzer") {}

    void runTest() override
    {
        beginTest("magnitudes vector has the expected size and starts at the floor");
        {
            SpectrumAnalyzer analyzer;
            analyzer.setSampleRate(48000.0);
            analyzer.update();

            const auto& mags = analyzer.getMagnitudesDb();
            expectEquals(static_cast<int>(mags.size()), SpectrumAnalyzer::kNumPoints);

            for (const auto m : mags)
                expect(m <= SpectrumAnalyzer::kFloorDb + 0.001f,
                       "silence must sit at the floor, got " + juce::String(m));
        }

        beginTest("frequency mapping spans the configured range logarithmically");
        {
            expectWithinAbsoluteError(SpectrumAnalyzer::frequencyForPoint(0),
                                      SpectrumAnalyzer::kMinFreqHz, 0.01f);
            expectWithinAbsoluteError(SpectrumAnalyzer::frequencyForPoint(SpectrumAnalyzer::kNumPoints - 1),
                                      SpectrumAnalyzer::kMaxFreqHz, 1.0f);

            // Midpoint of a log sweep 20 Hz..20 kHz is the geometric mean, ~632 Hz.
            const float mid = SpectrumAnalyzer::frequencyForPoint(SpectrumAnalyzer::kNumPoints / 2);
            expect(mid > 550.0f && mid < 720.0f,
                   "midpoint should be near the geometric mean, got " + juce::String(mid));
        }

        beginTest("a 1 kHz sine peaks at the 1 kHz display point");
        {
            SpectrumAnalyzer analyzer;
            analyzer.setSampleRate(48000.0);

            const int peak = peakPointForSine(analyzer, 48000.0, 1000.0);
            const float peakFreq = SpectrumAnalyzer::frequencyForPoint(peak);

            // Within a sixth of an octave of 1 kHz (ratio 2^(1/6) ~= 1.122).
            expect(peakFreq > 1000.0f / 1.122f && peakFreq < 1000.0f * 1.122f,
                   "expected peak near 1000 Hz, got " + juce::String(peakFreq));
        }

        beginTest("a 5 kHz sine peaks at the 5 kHz display point");
        {
            SpectrumAnalyzer analyzer;
            analyzer.setSampleRate(48000.0);

            const int peak = peakPointForSine(analyzer, 48000.0, 5000.0);
            const float peakFreq = SpectrumAnalyzer::frequencyForPoint(peak);

            expect(peakFreq > 5000.0f / 1.122f && peakFreq < 5000.0f * 1.122f,
                   "expected peak near 5000 Hz, got " + juce::String(peakFreq));
        }

        beginTest("a sine at a different sample rate still lands on the right frequency");
        {
            // Guards the bin->frequency remap that runs on a device sample-rate change.
            SpectrumAnalyzer analyzer;
            analyzer.setSampleRate(44100.0);

            const int peak = peakPointForSine(analyzer, 44100.0, 1000.0);
            const float peakFreq = SpectrumAnalyzer::frequencyForPoint(peak);

            expect(peakFreq > 1000.0f / 1.122f && peakFreq < 1000.0f * 1.122f,
                   "expected peak near 1000 Hz at 44.1k, got " + juce::String(peakFreq));
        }

        beginTest("peak hold sits above the live spectrum once the signal stops");
        {
            SpectrumAnalyzer analyzer;
            analyzer.setSampleRate(48000.0);

            const int peak = peakPointForSine(analyzer, 48000.0, 1000.0);

            const int blockFrames = 512;
            std::vector<float> silence(2 * blockFrames, 0.0f);

            // Long enough for the smoothed trace to fall well away from the peak, but far
            // short of the peak's own decay to the floor.
            for (int b = 0; b < 40; ++b)
            {
                analyzer.pushInterleavedStereo(silence.data(), blockFrames);
                analyzer.update();
            }

            const float live = analyzer.getMagnitudesDb()[static_cast<size_t>(peak)];
            const float held = analyzer.getPeakDb()  [static_cast<size_t>(peak)];

            expectEquals(static_cast<int>(analyzer.getPeakDb().size()),
                         SpectrumAnalyzer::kNumPoints);

            expect(held > live + 3.0f,
                   "peak should still be holding above the decayed live trace, got held "
                       + juce::String(held) + " vs live " + juce::String(live));

            // ...and it must be a real decay, not a permanent ceiling.
            for (int b = 0; b < 2000; ++b)
                analyzer.update();

            expect(analyzer.getPeakDb()[static_cast<size_t>(peak)] < held,
                   "peak should decay, not stick");
        }

        beginTest("peak hold decays at roughly the advertised 15 dB per second");
        {
            // The one tunable number in the feature. update() is called once per display
            // frame, and VisualizerComponent runs at 60 Hz, so 60 updates is one second.
            SpectrumAnalyzer analyzer;
            analyzer.setSampleRate(48000.0);

            const int  peak = peakPointForSine(analyzer, 48000.0, 1000.0);
            const auto idx  = static_cast<size_t>(peak);

            const float before = analyzer.getPeakDb()[idx];

            const int blockFrames = 512;
            std::vector<float> silence(2 * blockFrames, 0.0f);

            for (int b = 0; b < 60; ++b)
            {
                analyzer.pushInterleavedStereo(silence.data(), blockFrames);
                analyzer.update();
            }

            const float fallen = before - analyzer.getPeakDb()[idx];

            expect(fallen > 12.0f && fallen < 18.0f,
                   "expected ~15 dB of fall in one second of frames, got "
                       + juce::String(fallen));
        }

        beginTest("resetPeaks drops the hold back onto the live spectrum");
        {
            SpectrumAnalyzer analyzer;
            analyzer.setSampleRate(48000.0);

            const int peak = peakPointForSine(analyzer, 48000.0, 1000.0);
            analyzer.resetPeaks();

            const auto idx = static_cast<size_t>(peak);
            expectWithinAbsoluteError(analyzer.getPeakDb()[idx],
                                      analyzer.getMagnitudesDb()[idx], 0.001f);
        }

        beginTest("silence decays back toward the floor");
        {
            SpectrumAnalyzer analyzer;
            analyzer.setSampleRate(48000.0);

            peakPointForSine(analyzer, 48000.0, 1000.0);   // charge it up

            const int blockFrames = 512;
            std::vector<float> silence(2 * blockFrames, 0.0f);

            for (int b = 0; b < 200; ++b)
            {
                analyzer.pushInterleavedStereo(silence.data(), blockFrames);
                analyzer.update();
            }

            for (const auto m : analyzer.getMagnitudesDb())
                expect(m < -60.0f, "expected decay toward the floor, got " + juce::String(m));
        }
    }
};

static SpectrumAnalyzerTest spectrumAnalyzerTest;
