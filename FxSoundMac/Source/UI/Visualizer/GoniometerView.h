#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// Lissajous scope (L on X, R on Y, rotated 45° so mono is vertical) plus a correlation
// meter. Dry is drawn dim, wet bright: stereo widening shows up as the wet cloud
// spreading horizontally while the dry cloud stays narrow.
class GoniometerView : public juce::Component
{
public:
    GoniometerView();

    // Interleaved stereo. Only the most recent kMaxDots frames of each are plotted.
    void setFrames(const float* dry, int numDryFrames,
                   const float* wet, int numWetFrames);

    void paint(juce::Graphics& g) override;

private:
    static constexpr int kMaxDots = 1024;

    static float correlation(const std::vector<float>& interleaved);
    void plotCloud(juce::Graphics& g, const std::vector<float>& interleaved,
                   juce::Rectangle<float> area, juce::Colour colour, float dotSize) const;

    // Keeps the most recent kMaxDots frames of `src`.
    static void keepRecent(std::vector<float>& dest, const float* src, int numFrames);

    std::vector<float> dry_, wet_;   // interleaved stereo

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GoniometerView)
};
