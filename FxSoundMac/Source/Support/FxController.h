#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "LegacyDspAdapter.h"
#include "VisualizerTap.h"

// Single owner of DSP-facing state.
// UI calls setters on the message thread;
// processBlock runs on the audio thread and only touches the adapter.
// No locks or file I/O in the audio path.
class FxController
{
public:
    void prepare(int sampleRate, int maxBlockSize);

    void setPower(bool on);
    bool isPowerOn() const;
    void setBypassed(bool b);

    void setEffect(DfxDsp::Effect e, float value);
    float getEffect(DfxDsp::Effect e) const;

    void setEqBand(int band, float db);
    float getEqBand(int band) const;
    int getNumEqBands() const;
    float getEqBandFrequency(int band) const;

    void setOutputGainDb(float db);
    float getOutputGainDb() const;

    bool loadPreset(const juce::File& presetFile); // message thread only
    bool savePreset(const juce::File& directory, const juce::String& name); // message thread only

    // Audio thread — delegates directly to adapter, no locks.
    void processBlock(juce::AudioBuffer<float>& buffer);

    // Read-only observation hook for the visualizer. The tap is inert until something
    // calls setActive(true) on it, so a closed visualizer costs the audio thread one
    // relaxed atomic load per block and nothing else.
    VisualizerTap& getVisualizerTap() noexcept { return tap; }

    // Sample rate the DSP was last prepared at. The visualizer needs it to map FFT
    // bins to frequencies.
    int getSampleRate() const noexcept { return currentSampleRate.load(std::memory_order_relaxed); }

private:
    LegacyDspAdapter adapter;
    // Atomic so the audio thread sees the write from prepare() without a data race.
    std::atomic<bool> prepared { false };
    std::atomic<int> currentSampleRate { 48000 };
    VisualizerTap tap;
};
