#include "GoniometerView.h"
#include "FxTheme.h"
#include <cmath>

GoniometerView::GoniometerView() = default;

void GoniometerView::keepRecent(std::vector<float>& dest, const float* src, int numFrames)
{
    if (src == nullptr || numFrames <= 0)
        return;

    const int keep  = juce::jmin(numFrames, kMaxDots);
    const int start = numFrames - keep;      // the newest frames

    dest.assign(src + 2 * start, src + 2 * numFrames);
}

void GoniometerView::setFrames(const float* dry, int numDryFrames,
                               const float* wet, int numWetFrames)
{
    keepRecent(dry_, dry, numDryFrames);
    keepRecent(wet_, wet, numWetFrames);
    repaint();
}

float GoniometerView::correlation(const std::vector<float>& interleaved)
{
    const size_t frames = interleaved.size() / 2;
    if (frames == 0)
        return 0.0f;

    double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;

    for (size_t i = 0; i < frames; ++i)
    {
        const double l = interleaved[2 * i];
        const double r = interleaved[2 * i + 1];
        sumLR += l * r;
        sumLL += l * l;
        sumRR += r * r;
    }

    const double denom = std::sqrt(sumLL * sumRR);
    if (denom < 1.0e-9)
        return 0.0f;   // silence: no meaningful correlation

    return static_cast<float>(juce::jlimit(-1.0, 1.0, sumLR / denom));
}

void GoniometerView::plotCloud(juce::Graphics& g, const std::vector<float>& interleaved,
                               juce::Rectangle<float> area, juce::Colour colour, float dotSize) const
{
    const size_t frames = interleaved.size() / 2;
    if (frames == 0)
        return;

    const float cx = area.getCentreX();
    const float cy = area.getCentreY();
    const float r  = juce::jmin(area.getWidth(), area.getHeight()) * 0.5f;

    // Rotate 45°: mono (L == R) becomes a vertical line, out-of-phase becomes horizontal.
    //
    // The horizontal term is (R - L), NOT (L - R). Both give the same vertical/horizontal
    // reading for mono and anti-phase, which is why the flipped sign survived unnoticed
    // while the axes were unlabelled — but it mirrors the display, putting a left-only
    // signal on the RIGHT. Every goniometer convention puts left-only up and to the left,
    // and the L/R axis labels below now assert that.
    const float k = 0.70710678f;   // 1/sqrt(2)

    g.setColour(colour);

    for (size_t i = 0; i < frames; ++i)
    {
        const float l = juce::jlimit(-1.0f, 1.0f, interleaved[2 * i]);
        const float rr = juce::jlimit(-1.0f, 1.0f, interleaved[2 * i + 1]);

        const float x = cx + (rr - l) * k * r;
        const float y = cy - (l + rr) * k * r;

        g.fillEllipse(x - dotSize * 0.5f, y - dotSize * 0.5f, dotSize, dotSize);
    }
}

void GoniometerView::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(juce::Colour(FXCOLOR(WidgetBackground)).withAlpha(1.0f));
    g.fillRoundedRectangle(bounds, 6.0f);

    // Nothing on this widget said what it was: an unlabelled dot cloud in a circle is
    // only readable if you already know it is a goniometer, and the whole point of the
    // view is to be read at a glance.
    auto titleArea = bounds.removeFromTop(18.0f).reduced(8.0f, 2.0f);
    auto meterArea = bounds.removeFromBottom(22.0f).reduced(8.0f, 4.0f);
    auto scopeArea = bounds.reduced(8.0f);

    g.setColour(juce::Colour(FXCOLOR(DefaultText)).withAlpha(0.75f));
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.drawText("GONIOMETER", titleArea, juce::Justification::centredLeft);

    // Which cloud is which: same colour coding as the spectrum graph's legend.
    g.setFont(9.0f);
    g.setColour(juce::Colour(FXCOLOR(DefaultText)).withAlpha(0.45f));
    g.drawText("grey = in / red = out", titleArea, juce::Justification::centredRight);

    // Reference circle and axes.
    const float r  = juce::jmin(scopeArea.getWidth(), scopeArea.getHeight()) * 0.5f;
    const float cx = scopeArea.getCentreX();
    const float cy = scopeArea.getCentreY();

    g.setColour(juce::Colour(FXCOLOR(Outline)).withAlpha(0.35f));
    g.drawEllipse(cx - r, cy - r, 2 * r, 2 * r, 1.0f);
    g.drawVerticalLine  (static_cast<int>(cx), cy - r, cy + r);
    g.drawHorizontalLine(static_cast<int>(cy), cx - r, cx + r);

    plotCloud(g, dry_, scopeArea, juce::Colour(FXCOLOR(DefaultText)).withAlpha(0.30f), 1.5f);
    plotCloud(g, wet_, scopeArea, juce::Colour(FXCOLOR(EqStart)).withAlpha(0.85f),     2.0f);

    // Axis labels. Without these the scope is a shape with no meaning: these say a dot up
    // the left diagonal is left-channel content, a dot straight up is mono, and a cloud
    // smeared along the horizontal is the out-of-phase case you actually want to catch.
    // ASCII only (the bundled Gilroy has no em-dash glyph, and a failed glyph corrupts
    // the rest of the line).
    const float k = 0.70710678f;

    struct Axis { float dx, dy; const char* text; };

    const Axis axes[] = {
        {  0.0f, -1.0f, "M"  },     // straight up: mono, L == R
        {    -k, -k,    "L"  },     // upper left:  left channel only
        {     k, -k,    "R"  },     // upper right: right channel only
        { -1.0f,  0.0f, "-"  },     // horizontal:  anti-phase
        {  1.0f,  0.0f, "-"  },
    };

    g.setFont(9.0f);
    g.setColour(juce::Colour(FXCOLOR(DefaultText)).withAlpha(0.55f));

    for (const auto& a : axes)
        g.drawText(a.text,
                   juce::Rectangle<float>(cx + a.dx * (r + 7.0f) - 6.0f,
                                          cy + a.dy * (r + 7.0f) - 6.0f, 12.0f, 12.0f),
                   juce::Justification::centred);

    // Correlation meter: +1 (mono) at the right, -1 (out of phase) at the left.
    const float corr = correlation(wet_);

    g.setColour(juce::Colour(FXCOLOR(Outline)).withAlpha(0.4f));
    g.drawRoundedRectangle(meterArea, 3.0f, 1.0f);

    const float t = (corr + 1.0f) * 0.5f;
    const float markerX = meterArea.getX() + t * meterArea.getWidth();

    g.setColour(corr < 0.0f ? juce::Colours::orangered : juce::Colour(FXCOLOR(EqStart)).withAlpha(1.0f));
    g.fillRoundedRectangle(markerX - 2.0f, meterArea.getY(), 4.0f, meterArea.getHeight(), 2.0f);

    // The bar's ends carry the meaning: -1 is a mono-cancelling mix, +1 is mono. The
    // number alone never said which way was bad.
    g.setFont(9.0f);
    g.setColour(juce::Colour(FXCOLOR(DefaultText)).withAlpha(0.45f));
    g.drawText("-1", meterArea.reduced(4.0f, 0.0f), juce::Justification::centredLeft);
    g.drawText("+1", meterArea.reduced(4.0f, 0.0f), juce::Justification::centredRight);

    g.setColour(juce::Colour(FXCOLOR(DefaultText)).withAlpha(0.8f));
    g.setFont(10.0f);
    g.drawText("corr " + juce::String(corr, 2),
               meterArea, juce::Justification::centred);
}
