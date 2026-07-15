#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include "FxController.h"

// Resizable window hosting the visualizer. Closing it tells the owner, which drops
// the window - which destroys VisualizerComponent, which deactivates the tap.
class VisualizerWindow : public juce::DocumentWindow
{
public:
    VisualizerWindow(FxController& controller, std::function<void()> onCloseCallback);

    void closeButtonPressed() override;

private:
    std::function<void()> onClose_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualizerWindow)
};
