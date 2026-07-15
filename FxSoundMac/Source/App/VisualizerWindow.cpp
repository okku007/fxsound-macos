#include "VisualizerWindow.h"
#include "VisualizerComponent.h"
#include "FxTheme.h"

VisualizerWindow::VisualizerWindow(FxController& controller, std::function<void()> onCloseCallback)
    : juce::DocumentWindow("FxSound Visualizer",
                           // FXCOLOR values carry no alpha byte, so juce::Colour reads them
                           // as ARGB with alpha 0. withAlpha(1.0f) is the codebase idiom.
                           juce::Colour(FXCOLOR(WindowBackground)).withAlpha(1.0f),
                           juce::DocumentWindow::closeButton
                               | juce::DocumentWindow::minimiseButton
                               | juce::DocumentWindow::maximiseButton),
      onClose_(std::move(onCloseCallback))
{
    setUsingNativeTitleBar(true);
    setContentOwned(new VisualizerComponent(controller), true);
    setResizable(true, false);
    setResizeLimits(600, 320, 4000, 2400);
    centreWithSize(960, 540);
    setVisible(true);
}

void VisualizerWindow::closeButtonPressed()
{
    if (onClose_)
        onClose_();   // The owner destroys us. Nothing after this line may touch `this`.
}
