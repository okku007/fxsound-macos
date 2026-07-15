#include "LevelMeterView.h"
#include "FxTheme.h"
#include <cmath>

LevelMeterView::LevelMeterView() = default;

LevelMeterView::Levels LevelMeterView::analyse(const float* interleaved, int numFrames)
{
    Levels levels;

    if (interleaved == nullptr || numFrames <= 0)
        return levels;

    float peak = 0.0f;
    double sumSquares = 0.0;

    const int numSamples = 2 * numFrames;
    for (int i = 0; i < numSamples; ++i)
    {
        const float s = std::abs(interleaved[i]);
        peak = juce::jmax(peak, s);
        sumSquares += static_cast<double>(s) * s;
    }

    const float rms = static_cast<float>(std::sqrt(sumSquares / numSamples));

    levels.peakDb = juce::Decibels::gainToDecibels(peak, -100.0f);
    levels.rmsDb  = juce::Decibels::gainToDecibels(rms,  -100.0f);
    return levels;
}

int LevelMeterView::countClips(const float* interleaved, int numFrames)
{
    if (interleaved == nullptr || numFrames <= 0)
        return 0;

    int clips = 0;
    const int numSamples = 2 * numFrames;

    for (int i = 0; i < numSamples; ++i)
        if (std::abs(interleaved[i]) >= kClipThreshold)
            ++clips;

    return clips;
}

void LevelMeterView::setFrames(const float* dry, int numDryFrames,
                               const float* wet, int numWetFrames)
{
    const auto newIn  = analyse(dry, numDryFrames);
    const auto newOut = analyse(wet, numWetFrames);

    // Rise instantly, fall slowly.
    auto ballistics = [](float& held, float fresh)
    {
        held = fresh > held ? fresh : held + kRelease * (fresh - held);
    };

    ballistics(in_.peakDb,  newIn.peakDb);
    ballistics(in_.rmsDb,   newIn.rmsDb);
    ballistics(out_.peakDb, newOut.peakDb);
    ballistics(out_.rmsDb,  newOut.rmsDb);

    clipCount_ += countClips(wet, numWetFrames);

    repaint();
}

void LevelMeterView::drawMeter(juce::Graphics& g, juce::Rectangle<float> area,
                               const Levels& levels, const juce::String& label) const
{
    auto labelArea = area.removeFromTop(14.0f);

    g.setColour(juce::Colour(FXCOLOR(DefaultText)).withAlpha(0.7f));
    g.setFont(11.0f);
    g.drawText(label, labelArea, juce::Justification::centredLeft);

    g.setColour(juce::Colour(FXCOLOR(Outline)).withAlpha(0.4f));
    g.drawRoundedRectangle(area, 3.0f, 1.0f);

    auto fillArea = area.reduced(2.0f);

    auto normalise = [](float db)
    {
        return juce::jlimit(0.0f, 1.0f, (db - kMinDb) / (0.0f - kMinDb));
    };

    // RMS as the solid bar; peak as a line on top of it.
    const float rmsW = normalise(levels.rmsDb) * fillArea.getWidth();
    g.setColour(juce::Colour(FXCOLOR(EqStart)).withAlpha(0.75f));
    g.fillRoundedRectangle(fillArea.withWidth(rmsW), 2.0f);

    const float peakX = fillArea.getX() + normalise(levels.peakDb) * fillArea.getWidth();
    g.setColour(levels.peakDb >= -0.1f ? juce::Colours::orangered
                                       : juce::Colour(FXCOLOR(DefaultText)).withAlpha(1.0f));
    g.drawLine(peakX, fillArea.getY(), peakX, fillArea.getBottom(), 2.0f);

    g.setColour(juce::Colour(FXCOLOR(DefaultText)).withAlpha(0.85f));
    g.setFont(10.0f);
    g.drawText(juce::String(levels.peakDb, 1) + " dB",
               area, juce::Justification::centredRight);
}

void LevelMeterView::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(juce::Colour(FXCOLOR(WidgetBackground)).withAlpha(1.0f));
    g.fillRoundedRectangle(bounds, 6.0f);

    auto area = bounds.reduced(10.0f);

    drawMeter(g, area.removeFromTop(38.0f), in_,  "IN  (pre-DSP)");
    area.removeFromTop(6.0f);
    drawMeter(g, area.removeFromTop(38.0f), out_, "OUT (post-DSP)");
    area.removeFromTop(6.0f);

    const float gainDelta = out_.rmsDb - in_.rmsDb;

    g.setColour(juce::Colour(FXCOLOR(DefaultText)).withAlpha(0.8f));
    g.setFont(11.0f);
    g.drawText("gain " + juce::String(gainDelta, 1) + " dB",
               area.removeFromTop(16.0f), juce::Justification::centredLeft);

    g.setColour(clipCount_ > 0 ? juce::Colours::orangered
                               : juce::Colour(FXCOLOR(DefaultText)).withAlpha(0.5f));
    g.drawText("clipped samples: " + juce::String(clipCount_),
               area.removeFromTop(16.0f), juce::Justification::centredLeft);
}
