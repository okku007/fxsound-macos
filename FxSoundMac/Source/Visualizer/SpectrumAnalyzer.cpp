#include "SpectrumAnalyzer.h"
#include <cmath>

namespace {
// Fast to rise, slow to fall — the same asymmetry a hardware analyser has, so
// transients read clearly instead of flickering.
constexpr float kAttack  = 0.55f;
constexpr float kRelease = 0.12f;

// Peak hold: instant rise, then a slow linear fall. Per update() call, not per second —
// the visualizer calls update() once per display frame at 60 Hz, so 0.25 dB works out
// to roughly 15 dB/s, slow enough to read a spike that has already passed.
constexpr float kPeakDecayDb = 0.25f;
} // namespace

SpectrumAnalyzer::SpectrumAnalyzer()
{
    inputRing.assign(static_cast<size_t>(kFftSize), 0.0f);
    fftData.assign(static_cast<size_t>(2 * kFftSize), 0.0f);
    smoothedDb.assign(static_cast<size_t>(kNumPoints), kFloorDb);
    peakDb.assign(static_cast<size_t>(kNumPoints), kFloorDb);
    binStart.assign(static_cast<size_t>(kNumPoints), 0);
    binEnd.assign(static_cast<size_t>(kNumPoints), 0);
    rebuildPointMap();
}

void SpectrumAnalyzer::resetPeaks()
{
    peakDb = smoothedDb;
}

float SpectrumAnalyzer::frequencyForPoint(int pointIndex)
{
    const float t = static_cast<float>(pointIndex) / static_cast<float>(kNumPoints - 1);
    return kMinFreqHz * std::pow(kMaxFreqHz / kMinFreqHz, t);
}

void SpectrumAnalyzer::setSampleRate(double newSampleRate)
{
    if (newSampleRate <= 0.0 || std::abs(newSampleRate - sampleRate) < 1.0)
        return;

    sampleRate = newSampleRate;
    rebuildPointMap();
}

void SpectrumAnalyzer::rebuildPointMap()
{
    const int   numBins  = kFftSize / 2;
    const float binWidth = static_cast<float>(sampleRate) / static_cast<float>(kFftSize);

    for (int p = 0; p < kNumPoints; ++p)
    {
        // Half-point either side gives each display point a contiguous, non-overlapping
        // slice of the spectrum.
        const float lo = frequencyForPoint(juce::jmax(0, p - 1));
        const float hi = frequencyForPoint(juce::jmin(kNumPoints - 1, p + 1));

        const float centre = frequencyForPoint(p);
        const float loEdge = std::sqrt(lo * centre);      // geometric midpoints
        const float hiEdge = std::sqrt(centre * hi);

        int b0 = static_cast<int>(std::floor(loEdge / binWidth));
        int b1 = static_cast<int>(std::ceil (hiEdge / binWidth));

        b0 = juce::jlimit(1, numBins - 1, b0);            // skip DC
        b1 = juce::jlimit(b0 + 1, numBins, b1);           // always at least one bin

        binStart[static_cast<size_t>(p)] = b0;
        binEnd  [static_cast<size_t>(p)] = b1;
    }
}

void SpectrumAnalyzer::pushInterleavedStereo(const float* samples, int numFrames)
{
    if (samples == nullptr || numFrames <= 0)
        return;

    for (int i = 0; i < numFrames; ++i)
    {
        const float mono = 0.5f * (samples[2 * i] + samples[2 * i + 1]);
        inputRing[static_cast<size_t>(writePos)] = mono;
        writePos = (writePos + 1) % kFftSize;
    }
}

void SpectrumAnalyzer::update()
{
    // Unwrap the ring so the newest sample lands at the end of the FFT window.
    for (int i = 0; i < kFftSize; ++i)
        fftData[static_cast<size_t>(i)] = inputRing[static_cast<size_t>((writePos + i) % kFftSize)];

    std::fill(fftData.begin() + kFftSize, fftData.end(), 0.0f);

    window.multiplyWithWindowingTable(fftData.data(), static_cast<size_t>(kFftSize));
    fft.performFrequencyOnlyForwardTransform(fftData.data());

    // A Hann window sums to N/2, so that is the normalisation for a full-scale sine.
    const float norm = 2.0f / static_cast<float>(kFftSize);

    for (int p = 0; p < kNumPoints; ++p)
    {
        float peak = 0.0f;
        for (int b = binStart[static_cast<size_t>(p)]; b < binEnd[static_cast<size_t>(p)]; ++b)
            peak = juce::jmax(peak, fftData[static_cast<size_t>(b)]);

        const float db = juce::jmax(kFloorDb,
                                    juce::Decibels::gainToDecibels(peak * norm, kFloorDb));

        float& target = smoothedDb[static_cast<size_t>(p)];
        const float coeff = db > target ? kAttack : kRelease;
        target += coeff * (db - target);

        // Track the raw FFT value, not the smoothed one: smoothing is what buries the
        // spikes, so a peak hold fed from it would hold a spike that had already been
        // averaged down.
        float& held = peakDb[static_cast<size_t>(p)];
        held = db > held ? db : juce::jmax(kFloorDb, held - kPeakDecayDb);
    }
}
