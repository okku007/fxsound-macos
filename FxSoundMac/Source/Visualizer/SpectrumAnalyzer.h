#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>

// Turns interleaved-stereo sample blocks into a smoothed, log-spaced magnitude
// spectrum for display. No GUI and no audio-thread code: the consumer pushes the
// samples it drained from the VisualizerTap, then calls update() once per frame.
class SpectrumAnalyzer
{
public:
    // 8192-point transform: 5.9 Hz bins at 48 kHz. A 2048-point transform gives 23.4 Hz
    // bins, which leaves fewer than 10 bins under 200 Hz — a fifth of the log-frequency
    // display width, drawn from almost no data, so the bass end came out as a staircase
    // of repeated bins rather than a curve. The cost is a 170 ms window (vs 43 ms), so
    // transients smear more; that is already the same order as the display's own release
    // time constant (~140 ms, see kRelease), so it is not the limiting factor.
    static constexpr int   kFftOrder  = 13;
    static constexpr int   kFftSize   = 1 << kFftOrder;
    static constexpr int   kNumPoints = 256;          // log-spaced display points
    static constexpr float kMinFreqHz = 20.0f;
    static constexpr float kMaxFreqHz = 20000.0f;
    static constexpr float kFloorDb   = -90.0f;

    SpectrumAnalyzer();

    // Re-derives the bin -> display-point map. Safe to call on a device change.
    void setSampleRate(double newSampleRate);

    // Mono-sums each frame into the FFT input ring. Cheap; call with whatever the
    // tap handed over this frame.
    void pushInterleavedStereo(const float* samples, int numFrames);

    // FFTs the most recent kFftSize samples and updates the smoothed magnitudes.
    // Call once per display frame.
    void update();

    // Drops the peak-hold trace back onto the live spectrum.
    void resetPeaks();

    // kNumPoints magnitudes in dBFS, log-spaced kMinFreqHz..kMaxFreqHz.
    const std::vector<float>& getMagnitudesDb() const noexcept { return smoothedDb; }

    // Peak hold: the loudest value each display point has reached, sinking back slowly.
    // The smoothed magnitudes have a fast attack and a slow release, which still averages
    // short spikes away before they can be read — and short spikes (a resonance, a clip,
    // a transient the DSP is mishandling) are exactly what this view exists to catch.
    const std::vector<float>& getPeakDb() const noexcept { return peakDb; }

    // Display-point index -> centre frequency in Hz.
    static float frequencyForPoint(int pointIndex);

private:
    void rebuildPointMap();

    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window
        { static_cast<size_t>(kFftSize), juce::dsp::WindowingFunction<float>::hann };

    double sampleRate = 48000.0;

    std::vector<float> inputRing;    // kFftSize mono samples
    int   writePos = 0;

    std::vector<float> fftData;      // 2 * kFftSize (JUCE needs the doubled buffer)
    std::vector<float> smoothedDb;   // kNumPoints
    std::vector<float> peakDb;       // kNumPoints

    // Display point i covers FFT bins [binStart[i], binEnd[i]); the point takes the
    // max magnitude across its bins, so narrow peaks survive the log compression.
    std::vector<int> binStart, binEnd;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
};
