#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "FxController.h"
#include "SpectrumAnalyzer.h"
#include <cmath>
#include <vector>

// Headless diagnostic probe: measures, for each EQ band, whether boosting it
// actually moves the output spectrum. This is an instrument, not a feature —
// see FxSoundMac/DSP_DEBUG_HANDOFF.md for the suspected "dead EQ" bug this
// answers. The assertion in the per-band section is meant to FAIL if a band
// is dead; do not weaken it to make the test green.
namespace
{
constexpr double kSampleRate  = 48000.0;
constexpr int    kBlockFrames = 512;
// 60 blocks of 512 frames = ~30k samples: well past the 2048-sample FFT
// window and past the attack time of the analyzer's smoothing (same
// settling approach as SpectrumAnalyzerTest).
constexpr int    kNumBlocks   = 60;
constexpr float  kNoiseAmplitude = 0.3f;
constexpr juce::int64 kNoiseSeed = 424242;

// Fills every block with the same deterministic pseudo-random noise sequence
// (reseeded from kNoiseSeed on every call so separate captures are bit-identical),
// optionally runs it through a controller, and returns the resulting smoothed
// log-spaced magnitude spectrum. Passing controller == nullptr captures the
// spectrum of the raw, unprocessed noise (used as the step-1 input reference).
std::vector<float> captureSpectrum(FxController* controller)
{
    SpectrumAnalyzer analyzer;
    analyzer.setSampleRate(kSampleRate);

    juce::Random rng(kNoiseSeed);
    std::vector<float> interleaved(static_cast<size_t>(2 * kBlockFrames));

    for (int b = 0; b < kNumBlocks; ++b)
    {
        juce::AudioBuffer<float> buf(2, kBlockFrames);
        for (int i = 0; i < kBlockFrames; ++i)
        {
            buf.setSample(0, i, kNoiseAmplitude * (rng.nextFloat() * 2.0f - 1.0f));
            buf.setSample(1, i, kNoiseAmplitude * (rng.nextFloat() * 2.0f - 1.0f));
        }

        if (controller != nullptr)
            controller->processBlock(buf);

        for (int i = 0; i < kBlockFrames; ++i)
        {
            interleaved[static_cast<size_t>(2 * i)]     = buf.getSample(0, i);
            interleaved[static_cast<size_t>(2 * i + 1)] = buf.getSample(1, i);
        }

        analyzer.pushInterleavedStereo(interleaved.data(), kBlockFrames);
        analyzer.update();
    }

    return analyzer.getMagnitudesDb();
}

// Inverse of SpectrumAnalyzer::frequencyForPoint: nearest display-point index
// for a given frequency in Hz.
int pointForFrequency(float freqHz)
{
    freqHz = juce::jlimit(SpectrumAnalyzer::kMinFreqHz, SpectrumAnalyzer::kMaxFreqHz, freqHz);
    const float t = std::log(freqHz / SpectrumAnalyzer::kMinFreqHz)
                   / std::log(SpectrumAnalyzer::kMaxFreqHz / SpectrumAnalyzer::kMinFreqHz);
    const int idx = static_cast<int>(std::lround(t * static_cast<float>(SpectrumAnalyzer::kNumPoints - 1)));
    return juce::jlimit(0, SpectrumAnalyzer::kNumPoints - 1, idx);
}
} // namespace

struct EqProbeTest : juce::UnitTest
{
    EqProbeTest() : juce::UnitTest("EqProbe") {}

    void runTest() override
    {
        beginTest("bypass baseline: output spectrum matches input spectrum");
        {
            // Power OFF -> bypass active. No controller run at all is the input
            // reference; running the (bypassed) controller is the output.
            const auto inputDb = captureSpectrum(nullptr);

            FxController bypassController;
            bypassController.prepare(static_cast<int>(kSampleRate), kBlockFrames);
            // Power defaults to off (bypass active) — be explicit anyway.
            bypassController.setPower(false);
            const auto outputDb = captureSpectrum(&bypassController);

            float maxDeviationDb = 0.0f;
            for (int p = 0; p < SpectrumAnalyzer::kNumPoints; ++p)
            {
                const float freq = SpectrumAnalyzer::frequencyForPoint(p);
                if (freq < 50.0f || freq > 15000.0f)
                    continue;

                const float deviation = std::abs(outputDb[static_cast<size_t>(p)] - inputDb[static_cast<size_t>(p)]);
                maxDeviationDb = juce::jmax(maxDeviationDb, deviation);
            }

            logMessage("BYPASS BASELINE: max |output - input| deviation over 50 Hz-15 kHz = "
                       + juce::String(maxDeviationDb, 3) + " dB");

            expect(std::isfinite(maxDeviationDb), "bypass deviation must be a finite number");
        }

        beginTest("per-band effectiveness: +12 dB boost must move the output at its own frequency");
        {
            FxController probe;
            probe.prepare(static_cast<int>(kSampleRate), kBlockFrames);
            const int numBands = probe.getNumEqBands();
            logMessage("EQ band count reported by DSP: " + juce::String(numBands));

            logMessage("band | freq(Hz) | delta(dB) at +12 dB boost");

            for (int b = 0; b < numBands; ++b)
            {
                FxController controller;
                controller.prepare(static_cast<int>(kSampleRate), kBlockFrames);
                controller.setPower(true);

                // Isolate the EQ: all effects off, all bands flat.
                controller.setEffect(DfxDsp::Fidelity, 0.0f);
                controller.setEffect(DfxDsp::Ambience, 0.0f);
                controller.setEffect(DfxDsp::Surround, 0.0f);
                controller.setEffect(DfxDsp::DynamicBoost, 0.0f);
                controller.setEffect(DfxDsp::Bass, 0.0f);
                for (int c = 0; c < numBands; ++c)
                    controller.setEqBand(c, 0.0f);

                const auto flatDb = captureSpectrum(&controller);

                controller.setEqBand(b, 12.0f);
                const auto boostedDb = captureSpectrum(&controller);

                const float bandFreq = controller.getEqBandFrequency(b);
                const int   point    = pointForFrequency(bandFreq);
                const float delta    = boostedDb[static_cast<size_t>(point)] - flatDb[static_cast<size_t>(point)];

                logMessage(juce::String(b) + " | " + juce::String(bandFreq, 1) + " | " + juce::String(delta, 3));

                expect(delta > 3.0f,
                       "band " + juce::String(b) + " (" + juce::String(bandFreq, 1)
                       + " Hz): expected delta > 3.0 dB for a +12 dB boost, measured "
                       + juce::String(delta, 3) + " dB");
            }
        }
    }
};

static EqProbeTest eqProbeTest;
