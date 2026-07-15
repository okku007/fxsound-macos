#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>   // juce::Decibels

// Peak + RMS for the signal going in and the signal coming out, the gain delta between
// them, and a running count of clipped samples in the output.
//
// The clip count is computed from the FIFO, not on the audio thread. If the consumer
// ever stalls, the dropped block's clips are not counted. That is the tradeoff the spec
// accepts: nothing extra runs on the audio thread.
class LevelMeterView : public juce::Component
{
public:
    LevelMeterView();

    void setFrames(const float* dry, int numDryFrames,
                   const float* wet, int numWetFrames);

    void paint(juce::Graphics& g) override;

private:
    struct Levels
    {
        float peakDb = -100.0f;
        float rmsDb  = -100.0f;
    };

    static Levels analyse(const float* interleaved, int numFrames);
    static int    countClips(const float* interleaved, int numFrames);

    void drawMeter(juce::Graphics& g, juce::Rectangle<float> area,
                   const Levels& levels, const juce::String& label) const;

    static constexpr float kClipThreshold = 0.999f;
    static constexpr float kMinDb = -60.0f;

    // Ballistics: instant attack, slow release, so peaks are legible.
    static constexpr float kRelease = 0.15f;

    Levels in_, out_;
    int clipCount_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeterView)
};
