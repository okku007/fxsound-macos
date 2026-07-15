#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "EqGraphView.h"
#include "GoniometerView.h"
#include "LevelMeterView.h"
#include "SpectrumAnalyzer.h"
#include "DeltaTrace.h"
#include "FxController.h"

// Owns the visualizer's analysis loop: drains the tap, runs the FFTs, hands the
// results to the views, repaints. Activates the tap on construction and deactivates
// it on destruction, so the audio thread pays nothing while the window is closed.
class VisualizerComponent : public juce::Component,
                            private juce::Timer
{
public:
    explicit VisualizerComponent(FxController& controller);
    ~VisualizerComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    // Drains one ring into `scratch`, feeds the analyzer, and returns the frame count.
    int drain(bool wet, std::vector<float>& scratch);

    static constexpr int kFrameRateHz = 60;
    static constexpr int kMaxDrainFrames = VisualizerTap::kCapacityFrames;

    FxController& controller_;

    SpectrumAnalyzer dryAnalyzer_, wetAnalyzer_;
    std::vector<float> dryScratch_, wetScratch_;   // interleaved stereo, preallocated
    DeltaTrace delta_;

    EqGraphView graph_;
    GoniometerView goniometer_;
    LevelMeterView meters_;

    int lastSampleRate_ = 0;
    int lastDryFrames_ = 0, lastWetFrames_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualizerComponent)
};
