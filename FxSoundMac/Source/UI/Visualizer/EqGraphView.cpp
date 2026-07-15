#include "EqGraphView.h"
#include "FxTheme.h"
#include "SpectralSmoothing.h"
#include "SpectrumAnalyzer.h"
#include <array>
#include <cmath>

namespace {
constexpr float kPlotInset = 8.0f;

// The EQ curve rides a different Y axis from the spectra (centred gain, not dBFS), so it
// needs a colour that belongs to neither of them. The theme is all reds and greys, so an
// amber reads as clearly "not the spectrum, not the ghost".
const juce::Colour kEqCurveColour { 0xffffc857 };

// The measured (wet-minus-dry) trace rides the SAME gain axis as the EQ curve, so it
// needs a colour distinct from all three existing traces: white dry ghost, red/pink wet
// fill (FXCOLOR(EqStart) = 0xef4b65), and the amber EQ curve. FXCOLOR(GraphHigh)
// (0xd51535) — the plan's original suggestion — is a red almost identical to the wet
// fill and would be unreadable on top of it. A cyan reads clearly as "measurement"
// against this otherwise warm/amber palette.
const juce::Colour kMeasuredTraceColour { 0xff4dd0e1 };

// Peak hold rides the same dBFS axis as the wet spectrum and marks its ceiling, so it
// belongs to the wet trace visually — a pale wash of the accent colour, not a fifth hue.
// Anything more saturated competes with the live curve it is supposed to annotate.
const juce::Colour kPeakHoldColour { 0xffffb3be };

// Decade-ish gridlines, the ones an engineer actually looks for.
const float kGridFreqs[] = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
                             1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };

juce::String labelForFreq(float f)
{
    return f >= 1000.0f ? juce::String(static_cast<int>(f / 1000.0f)) + "k"
                        : juce::String(static_cast<int>(f));
}

// The hover readout needs more precision than the axis ticks: "1.02 kHz", not "1k".
juce::String preciseFreq(float f)
{
    return f >= 1000.0f ? juce::String(f / 1000.0f, 2) + " kHz"
                        : juce::String(f, 1) + " Hz";
}

juce::String signedDb(float db)
{
    return (db >= 0.0f ? "+" : "") + juce::String(db, 1);
}

struct LegendEntry
{
    juce::Colour colour;
    const char*  text;     // the base caption; peak hold's clear-hint is appended separately
    bool         dashed;
};

// One entry per EqGraphView::Trace, in enum order. Returned by value rather than held in a
// static: FXCOLOR reads the theme, which is not up at static-initialisation time.
//
// ASCII only. The bundled Gilroy typeface has no em-dash glyph, and the failed glyph
// corrupts the layout of every word after it (each loses its second character). Do not
// reintroduce en/em dashes or other non-ASCII here.
//
// measured's caption must keep saying "whole DSP": that trace covers the whole processing
// chain, not just the EQ, and without that hint it reads as a bug the moment an effect makes
// it diverge from the EQ curve.
std::array<LegendEntry, EqGraphView::kNumTraces> legendEntries()
{
    return { {
        { juce::Colours::white.withAlpha(0.85f),          "IN  - before FxSound (dBFS)",       false },
        { juce::Colour(FXCOLOR(EqStart)).withAlpha(1.0f), "OUT - after FxSound (dBFS)",        false },
        { kPeakHoldColour.withAlpha(0.75f),               "peak hold",                         false },
        { kEqCurveColour,                                 "EQ curve - gain, right axis",       false },
        { kMeasuredTraceColour.withAlpha(0.9f),           "measured - whole DSP, not just EQ", true  },
    } };
}
} // namespace

EqGraphView::EqGraphView()
{
    dryDb_.assign(static_cast<size_t>(SpectrumAnalyzer::kNumPoints), SpectrumAnalyzer::kFloorDb);
    wetDb_.assign(static_cast<size_t>(SpectrumAnalyzer::kNumPoints), SpectrumAnalyzer::kFloorDb);

    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

juce::Rectangle<float> EqGraphView::plotArea() const
{
    return getLocalBounds().toFloat()
             .reduced(kPlotInset)
             .withTrimmedBottom(16.0f)      // frequency labels
             .withTrimmedLeft(kLeftGutter); // dBFS scale
}

void EqGraphView::mouseMove(const juce::MouseEvent& e)
{
    const auto plot = plotArea();
    const float x = static_cast<float>(e.position.x);

    // Outside the plot proper (in a gutter) reads as "not hovering" rather than clamping
    // to the nearest edge point, which would show a value the cursor is not pointing at.
    const float next = plot.contains(e.position) ? x : -1.0f;

    if (next != hoverX_)
    {
        hoverX_ = next;
        repaint();
    }
}

void EqGraphView::mouseExit(const juce::MouseEvent&)
{
    if (hoverX_ >= 0.0f)
    {
        hoverX_ = -1.0f;
        repaint();
    }
}

void EqGraphView::setTraceVisible(Trace t, bool visible)
{
    auto& flag = traceVisible_[static_cast<size_t>(t)];

    if (flag == visible)
        return;

    flag = visible;
    repaint();
}

void EqGraphView::setLegendCollapsed(bool collapsed)
{
    if (legendCollapsed_ == collapsed)
        return;

    legendCollapsed_ = collapsed;
    repaint();
}

void EqGraphView::setSmoothingEnabled(bool enabled)
{
    if (smoothingEnabled_ == enabled)
        return;

    smoothingEnabled_ = enabled;
    repaint();
}

std::vector<float> EqGraphView::displaySpectrum(const std::vector<float>& raw) const
{
    return smoothingEnabled_ ? smoothDb(raw, kSmoothHalfWidth) : raw;
}

juce::Rectangle<float> EqGraphView::smoothChipBounds() const
{
    // The rightmost slice of the legend handle's own row. It sits INSIDE legendHandleBounds()
    // deliberately: keeping both controls on one row is what makes them read as a matched pair
    // of view-chrome toggles, and mouseDown checks this chip before the handle so the overlap
    // resolves in the chip's favour. Fixed to the handle rather than plotArea() so it tracks
    // the handle if that ever moves, exactly like every other legend rectangle.
    const auto h = legendHandleBounds();
    return { h.getRight() - 64.0f, h.getY(), 64.0f, h.getHeight() };
}

void EqGraphView::mouseDown(const juce::MouseEvent& e)
{
    // The smooth chip is checked FIRST, before the handle it sits inside: the two share one
    // row, and the only thing that resolves their overlap is this order. A click on the chip
    // toggles smoothing; a click anywhere else on the handle row collapses.
    if (smoothChipBounds().contains(e.position))
    {
        setSmoothingEnabled(! smoothingEnabled_);
        return;
    }

    // Checked before legendRowAt: the handle sits inside legendPanelBounds() too, and if the
    // row hit-test ran first it would never see a click here anyway (legendRowAt only ever
    // returns a trace index) - but ordering it first is what keeps this readable as "the
    // handle is the outermost control", matching the collapsed state where it is the ONLY
    // thing left to click.
    if (legendHandleBounds().contains(e.position))
    {
        setLegendCollapsed(! legendCollapsed_);
        return;
    }

    const int row = legendRowAt(e.position);

    // The legend is the control surface: a row is a switch for its trace. Checked first, so
    // reaching for a switch never also throws away the peak hold you were reading.
    if (row >= 0)
    {
        const auto trace = static_cast<Trace>(row);
        setTraceVisible(trace, ! isTraceVisible(trace));
        return;
    }

    // Dead space inside the panel - its padding, or a collapsed panel's now-tiny body past
    // the handle - must not fall through to onResetPeaks: a click aimed at the legend that
    // missed every row by a couple of pixels should not throw away the peak hold you were
    // reading, any more than a hit row does.
    if (legendPanelBounds().contains(e.position))
        return;

    // A peak hold with no way to clear it is a record of the loudest thing that ever
    // happened, which stops being useful the moment you change something and want to
    // know what the NEW loudest thing is.
    if (onResetPeaks != nullptr)
        onResetPeaks();
}

int EqGraphView::pointForX(float x, juce::Rectangle<float> plot) const
{
    if (plot.getWidth() <= 0.0f)
        return 0;

    const float t = (x - plot.getX()) / plot.getWidth();
    const int   i = juce::roundToInt(t * static_cast<float>(SpectrumAnalyzer::kNumPoints - 1));

    return juce::jlimit(0, SpectrumAnalyzer::kNumPoints - 1, i);
}

void EqGraphView::setSpectra(const std::vector<float>& dryDb, const std::vector<float>& wetDb)
{
    dryDb_ = dryDb;
    wetDb_ = wetDb;
    repaint();
}

void EqGraphView::setPeaks(const std::vector<float>& peakDb)
{
    peakDb_ = peakDb;
    repaint();
}

void EqGraphView::setEqCurve(const std::vector<float>& curveDb,
                             const std::vector<EqCurveModel::Band>& bands)
{
    eqCurveDb_ = curveDb;
    bands_     = bands;
    repaint();
}

void EqGraphView::setDeltaCurve(const std::vector<float>& deltaDb,
                                const std::vector<float>& confidence)
{
    deltaDb_         = deltaDb;
    deltaConfidence_ = confidence;
    repaint();
}

float EqGraphView::xForPoint(int pointIndex, juce::Rectangle<float> plot) const
{
    const float t = static_cast<float>(pointIndex)
                  / static_cast<float>(SpectrumAnalyzer::kNumPoints - 1);
    return plot.getX() + t * plot.getWidth();
}

float EqGraphView::yForDb(float db, juce::Rectangle<float> plot) const
{
    const float t = juce::jlimit(0.0f, 1.0f, (db - kBottomDb) / (kTopDb - kBottomDb));
    return plot.getBottom() - t * plot.getHeight();
}

float EqGraphView::yForGainDb(float gainDb, juce::Rectangle<float> plot) const
{
    // The gain axis is centred: 0 dB sits on the middle line, ±kMaxGainDb at the edges.
    const float t = juce::jlimit(-1.0f, 1.0f, gainDb / kMaxGainDb);
    return plot.getCentreY() - t * (plot.getHeight() * 0.5f);
}

void EqGraphView::drawGrid(juce::Graphics& g, juce::Rectangle<float> plot) const
{
    g.setColour(juce::Colour(FXCOLOR(Outline)).withAlpha(0.35f));

    const float logMin = std::log10(SpectrumAnalyzer::kMinFreqHz);
    const float logMax = std::log10(SpectrumAnalyzer::kMaxFreqHz);

    for (const float f : kGridFreqs)
    {
        const float t = (std::log10(f) - logMin) / (logMax - logMin);
        const float x = plot.getX() + t * plot.getWidth();
        g.drawVerticalLine(static_cast<int>(x), plot.getY(), plot.getBottom());
    }

    for (float db = kTopDb; db >= kBottomDb; db -= 20.0f)
    {
        const float y = yForDb(db, plot);
        g.drawHorizontalLine(static_cast<int>(y), plot.getX(), plot.getRight());
    }

    g.setColour(juce::Colour(FXCOLOR(DefaultText)).withAlpha(0.5f));
    g.setFont(11.0f);

    for (const float f : kGridFreqs)
    {
        const float t = (std::log10(f) - logMin) / (logMax - logMin);
        const float x = plot.getX() + t * plot.getWidth();
        g.drawText(labelForFreq(f),
                   juce::Rectangle<float>(x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 14.0f),
                   juce::Justification::centred);
    }

    // The dBFS scale, in the left gutter. This is the axis both spectra are drawn
    // against; unlabelled, a trace on this graph told you nothing about level.
    g.setFont(10.0f);

    for (float db = kTopDb; db >= kBottomDb; db -= 20.0f)
    {
        g.drawText(juce::String(static_cast<int>(db)),
                   juce::Rectangle<float>(plot.getX() - kLeftGutter,
                                          yForDb(db, plot) - 7.0f,
                                          kLeftGutter - 5.0f, 14.0f),
                   juce::Justification::centredRight);
    }
}

juce::Path EqGraphView::buildSpectrumPath(const std::vector<float>& db,
                                          juce::Rectangle<float> plot) const
{
    juce::Path p;

    const int n = static_cast<int>(db.size());

    if (n == 0)
        return p;

    auto pointAt = [&](int i)
    {
        const int c = juce::jlimit(0, n - 1, i);   // clamp the ends, so no phantom slope
        return juce::Point<float>(xForPoint(c, plot), yForDb(db[static_cast<size_t>(c)], plot));
    };

    p.startNewSubPath(pointAt(0));

    if (n == 1)
        return p;

    // Catmull-Rom through every display point, converted to cubic Beziers. Straight
    // segments between 256 points left the traces visibly faceted.
    for (int i = 0; i < n - 1; ++i)
    {
        const auto p0 = pointAt(i - 1);
        const auto p1 = pointAt(i);
        const auto p2 = pointAt(i + 1);
        const auto p3 = pointAt(i + 2);

        auto c1 = p1 + (p2 - p0) * (1.0f / 6.0f);
        auto c2 = p2 - (p3 - p1) * (1.0f / 6.0f);

        // Catmull-Rom overshoots at a sharp change of direction. On a spectrum that reads
        // as a phantom dip beside every peak — an artefact of the drawing that looks like
        // a notch in the audio, which is precisely the lie this view must not tell. Hold
        // each control point inside its own segment's dB range: the curve stays smooth but
        // can never leave the range of the data it was built from.
        const float lo = juce::jmin(p1.y, p2.y);
        const float hi = juce::jmax(p1.y, p2.y);

        c1.y = juce::jlimit(lo, hi, c1.y);
        c2.y = juce::jlimit(lo, hi, c2.y);

        p.cubicTo(c1, c2, p2);
    }

    return p;
}

void EqGraphView::drawGainAxis(juce::Graphics& g, juce::Rectangle<float> plot) const
{
    // The EQ curve and the measured trace live on a SECOND Y axis: centred gain
    // (±kMaxGainDb), not the dBFS axis the spectra use. Two axes on one plot is what Logic
    // does, but it is only readable if the gain axis is marked - so draw its zero line and
    // its scale in the curve's colour.
    const float zeroY = yForGainDb(0.0f, plot);

    g.setColour(kEqCurveColour.withAlpha(0.30f));
    g.drawHorizontalLine(static_cast<int>(zeroY), plot.getX(), plot.getRight());

    g.setColour(kEqCurveColour.withAlpha(0.65f));
    g.setFont(10.0f);

    for (const float gain : { kMaxGainDb, 0.0f, -kMaxGainDb })
    {
        g.drawText(juce::String(gain > 0.0f ? "+" : "") + juce::String(static_cast<int>(gain)) + " dB",
                   juce::Rectangle<float>(plot.getRight() - 44.0f,
                                          yForGainDb(gain, plot) - 7.0f, 42.0f, 14.0f),
                   juce::Justification::centredRight);
    }
}

void EqGraphView::paint(juce::Graphics& g)
{
    const auto plot = plotArea();

    g.setColour(juce::Colour(FXCOLOR(WidgetBackground)).withAlpha(1.0f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);

    drawGrid(g, plot);

    // Backdrop BEFORE the traces, text AFTER them (drawLegendForeground, at the bottom of
    // this function): the panel only exists to darken the wet fill under the legend text so
    // it stays readable, not to sit on top of every trace that happens to cross its corner.
    // Painting both halves together, in either order, reproduces the old bug one way (the
    // fill erases the traces if it goes last) or loses the readability fix the other way (the
    // text goes underneath the traces if it goes first). Splitting the call in two is the only
    // way to get both: see the comment on the two functions themselves for why they must stay
    // split rather than being "tidied" back into one.
    drawLegendBackdrop(g, plot);

    // Wet: filled, in the accent colour. This is what you are hearing. The outline is the
    // same path as the fill minus its closing segments, so build it once and close a copy
    // rather than walking all kNumPoints twice per frame.
    if (traceVisible_[static_cast<size_t>(TraceOut)])
    {
        const auto wetFill = juce::Colour(FXCOLOR(EqStart));
        const juce::Path wetStroke = buildSpectrumPath(displaySpectrum(wetDb_), plot);

        juce::Path wetFilled = wetStroke;
        wetFilled.lineTo(plot.getRight(), plot.getBottom());
        wetFilled.lineTo(plot.getX(),     plot.getBottom());
        wetFilled.closeSubPath();

        g.setGradientFill(juce::ColourGradient(wetFill.withAlpha(0.55f), plot.getX(), plot.getY(),
                                               wetFill.withAlpha(0.05f), plot.getX(), plot.getBottom(),
                                               false));
        g.fillPath(wetFilled);

        g.setColour(wetFill.withAlpha(0.95f));
        g.strokePath(wetStroke, juce::PathStrokeType(1.6f));
    }

    // Peak hold on the wet trace, drawn thin and above the fill: it is a ceiling, so it must
    // read as an outline sitting over the live spectrum rather than another curve competing
    // with it.
    if (traceVisible_[static_cast<size_t>(TracePeakHold)] && ! peakDb_.empty())
    {
        g.setColour(kPeakHoldColour.withAlpha(0.75f));
        g.strokePath(buildSpectrumPath(displaySpectrum(peakDb_), plot), juce::PathStrokeType(1.0f));
    }

    // Dry: unfilled ghost, drawn last so it stays readable on top of the wet fill. Where the
    // DSP is doing nothing the two curves coincide, and telling "converged" apart from "too
    // faint to see" is the entire point of this view - so the ghost needs enough contrast to
    // be legible against the fill.
    if (traceVisible_[static_cast<size_t>(TraceIn)])
    {
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.strokePath(buildSpectrumPath(displaySpectrum(dryDb_), plot), juce::PathStrokeType(1.6f));
    }

    // The gain axis serves the EQ curve AND the measured trace. It survives while either is
    // on; with both off nothing is plotted against it, and an empty second axis is worse than
    // no axis - it invites you to read the spectra against the wrong scale.
    if (traceVisible_[static_cast<size_t>(TraceEqCurve)] || traceVisible_[static_cast<size_t>(TraceMeasured)])
        drawGainAxis(g, plot);

    // One condition, one meaning: the curve's stroke and its band markers used to be gated by
    // two textually-identical but separate ifs, which let one survive an edit that deleted the
    // other. Nest them instead, so there is exactly one place that decides whether the EQ
    // curve is on screen at all.
    if (traceVisible_[static_cast<size_t>(TraceEqCurve)])
    {
        if (! eqCurveDb_.empty())
        {
            juce::Path curve;
            curve.startNewSubPath(xForPoint(0, plot), yForGainDb(eqCurveDb_[0], plot));

            for (int i = 1; i < static_cast<int>(eqCurveDb_.size()); ++i)
                curve.lineTo(xForPoint(i, plot), yForGainDb(eqCurveDb_[static_cast<size_t>(i)], plot));

            g.setColour(kEqCurveColour);
            g.strokePath(curve, juce::PathStrokeType(2.0f));
        }

        // Static band markers. Not draggable: EQ edits happen in the main window. They are
        // pinned to the EQ curve's gain values, so they go when it goes - a marker with no
        // curve under it is a dot floating in space.
        const float logMin = std::log10(SpectrumAnalyzer::kMinFreqHz);
        const float logMax = std::log10(SpectrumAnalyzer::kMaxFreqHz);

        for (const auto& band : bands_)
        {
            if (band.frequencyHz <= 0.0f)
                continue;

            const float t = (std::log10(band.frequencyHz) - logMin) / (logMax - logMin);
            const float x = plot.getX() + juce::jlimit(0.0f, 1.0f, t) * plot.getWidth();
            const float y = yForGainDb(band.gainDb, plot);

            g.setColour(kEqCurveColour);
            g.fillEllipse(x - 4.0f, y - 4.0f, 8.0f, 8.0f);

            g.setColour(juce::Colour(FXCOLOR(WindowBackground)).withAlpha(1.0f));
            g.drawEllipse(x - 4.0f, y - 4.0f, 8.0f, 8.0f, 1.5f);
        }
    }

    // The measured trace: literally wet-minus-dry in dB, per display point. It shares the EQ
    // curve's centred gain axis (yForGainDb) but covers the WHOLE processing chain - EQ,
    // effects, maximizer, output gain - not the EQ alone. It is only meant to track the EQ
    // curve when everything else is at zero; the legend says so.
    //
    // Drawn AFTER the EQ curve and dashed, so a coinciding trace stays visible instead of
    // disappearing: kEqCurveColour is fully opaque, so anything painted underneath it along
    // the same pixels would simply vanish. A dashed cyan line on top leaves the amber curve
    // showing through the gaps - read as "these two agree" rather than one trace silently
    // swallowing the other. Where the trace diverges (an effect is engaged) the dashes plainly
    // separate from the amber line.
    if (traceVisible_[static_cast<size_t>(TraceMeasured)]
        && deltaDb_.size() == deltaConfidence_.size()
        && deltaDb_.size() > 1)
    {
        juce::Path measured;
        bool subpathOpen = false;

        for (size_t i = 0; i < deltaDb_.size(); ++i)
        {
            // Below the gate, DeltaTrace holds its last value but confidence has decayed
            // toward zero: skip those points so the trace breaks instead of drawing a
            // meaningless flat line across silence.
            if (deltaConfidence_[i] < 0.1f)
            {
                subpathOpen = false;
                continue;
            }

            const float x = xForPoint(static_cast<int>(i), plot);
            const float y = yForGainDb(deltaDb_[i], plot);

            if (! subpathOpen)
            {
                measured.startNewSubPath(x, y);
                subpathOpen = true;
            }
            else
            {
                measured.lineTo(x, y);
            }
        }

        juce::Path dashed;
        const float dashLengths[] = { 4.0f, 3.0f };
        juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
            .createDashedStroke(dashed, measured, dashLengths, 2);

        g.setColour(kMeasuredTraceColour.withAlpha(0.9f));
        g.fillPath(dashed);
    }

    // Foreground last: the swatches and the text ride on top of every trace, which is what
    // lets the legend live back inside the plot's corner without erasing anything under it.
    drawLegendForeground(g, plot);
    drawHoverReadout(g, plot);
}

std::vector<EqGraphView::ReadoutRow> EqGraphView::readoutRowsAt(int pointIndex) const
{
    std::vector<ReadoutRow> rows;

    if (pointIndex < 0
        || pointIndex >= static_cast<int>(dryDb_.size())
        || pointIndex >= static_cast<int>(wetDb_.size()))
        return rows;

    const auto idx = static_cast<size_t>(pointIndex);

    // The number must describe the line as DRAWN: with smoothing on, the dot sits on the
    // smoothed curve, so a raw number here would label a point the curve does not pass through
    // - the one lie this view must never tell. Same funnel as paint() and the hover dots.
    const auto dryDisp = displaySpectrum(dryDb_);
    const auto wetDisp = displaySpectrum(wetDb_);

    rows.reserve(4);

    // ASCII only, as in the legend: the bundled Gilroy has no em-dash glyph and a failed
    // glyph corrupts the rest of the line.
    rows.push_back({ juce::Colour(FXCOLOR(DefaultText)),
                     preciseFreq(SpectrumAnalyzer::frequencyForPoint(pointIndex)) });

    if (traceVisible_[static_cast<size_t>(TraceIn)])
        rows.push_back({ juce::Colours::white.withAlpha(0.85f),
                         "IN   " + juce::String(dryDisp[idx], 1) + " dB" });

    if (traceVisible_[static_cast<size_t>(TraceOut)])
        rows.push_back({ juce::Colour(FXCOLOR(EqStart)),
                         "OUT  " + juce::String(wetDisp[idx], 1) + " dB" });

    if (traceVisible_[static_cast<size_t>(TraceMeasured)])
    {
        // Report the gated DeltaTrace value, not a raw wet-minus-dry of this frame: below the
        // noise floor the raw difference is two floor values subtracting to a meaningless
        // number. Where DeltaTrace has no confidence, say so instead of printing noise.
        const bool haveDelta = idx < deltaDb_.size()
                            && idx < deltaConfidence_.size()
                            && deltaConfidence_[idx] >= 0.1f;

        rows.push_back({ kMeasuredTraceColour,
                         haveDelta ? "DIFF " + signedDb(deltaDb_[idx]) + " dB"
                                   : juce::String("DIFF --") });
    }

    return rows;
}

void EqGraphView::drawHoverReadout(juce::Graphics& g, juce::Rectangle<float> plot) const
{
    if (hoverX_ < 0.0f)
        return;

    const int i = pointForX(hoverX_, plot);

    const auto rows = readoutRowsAt(i);

    if (rows.empty())
        return;

    const auto idx = static_cast<size_t>(i);
    const float x  = xForPoint(i, plot);   // snap to the point, not the raw cursor

    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.drawVerticalLine(static_cast<int>(x), plot.getY(), plot.getBottom());

    // Dots on the two spectra so it is obvious which samples the numbers came from. A dot on
    // a trace that is switched off would be a marker on a curve that is not there. Placed on
    // the DISPLAYED curve (smoothed when smoothing is on), so the dot always lands on the line
    // the reader sees rather than on the raw value hidden beneath it.
    if (traceVisible_[static_cast<size_t>(TraceIn)])
    {
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.fillEllipse(x - 2.5f, yForDb(displaySpectrum(dryDb_)[idx], plot) - 2.5f, 5.0f, 5.0f);
    }

    if (traceVisible_[static_cast<size_t>(TraceOut)])
    {
        g.setColour(juce::Colour(FXCOLOR(EqStart)));
        g.fillEllipse(x - 2.5f, yForDb(displaySpectrum(wetDb_)[idx], plot) - 2.5f, 5.0f, 5.0f);
    }

    // Row 0 is the frequency, which belongs to the cursor rather than to any trace. If it is
    // the ONLY row, every value-bearing trace is switched off and there is nothing worth
    // boxing: leave the crosshair and stop.
    if (rows.size() < 2)
        return;

    constexpr float kBoxW = 108.0f;
    const float     kBoxH = 12.0f + 15.0f * static_cast<float>(rows.size());

    // Sits at the bottom of the plot so it never collides with the legend, which occupies the
    // top-left corner (see legendPanelBounds). It also stays clear of the hover dots on the
    // traces themselves, which can land anywhere in the plot's vertical range.
    // Flips to the other side of the crosshair near the right edge so it stays on screen.
    const bool  flip = x + 10.0f + kBoxW > plot.getRight();
    const float boxX = flip ? x - 10.0f - kBoxW : x + 10.0f;

    auto box = juce::Rectangle<float>(boxX, plot.getBottom() - kBoxH - 6.0f, kBoxW, kBoxH);

    g.setColour(juce::Colour(FXCOLOR(WindowBackground)).withAlpha(0.92f));
    g.fillRoundedRectangle(box, 4.0f);

    g.setColour(juce::Colour(FXCOLOR(Outline)).withAlpha(0.6f));
    g.drawRoundedRectangle(box, 4.0f, 1.0f);

    auto row = box.reduced(8.0f, 6.0f).removeFromTop(15.0f);

    g.setFont(11.0f);

    for (const auto& r : rows)
    {
        g.setColour(r.colour);
        g.drawText(r.text, row, juce::Justification::centredLeft);
        row.translate(0.0f, 15.0f);
    }
}

juce::Rectangle<float> EqGraphView::legendHandleBounds() const
{
    // This is the rectangle row 0 used to occupy before the legend could collapse: the
    // handle is now the legend's header, and every trace row is measured one pitch below
    // it (see legendRowBounds) rather than off plotArea() a second time.
    const auto plot = plotArea();
    return { plot.getX() + 8.0f, plot.getY() + 6.0f, 280.0f, 14.0f };
}

juce::Rectangle<float> EqGraphView::legendRowBounds(int index) const
{
    // Trace rows sit one row pitch below the handle regardless of legendCollapsed_: this
    // function answers "where WOULD row i be", not "is it on screen" - drawLegendForeground
    // and legendRowAt each decide that separately, off the one legendCollapsed_ flag, so the
    // geometry itself never needs two versions.
    return legendHandleBounds().translated(0.0f, 15.0f * static_cast<float>(index + 1));
}

juce::Rectangle<float> EqGraphView::legendPanelBounds() const
{
    // Derived from the handle rather than from plotArea() directly: the handle IS the thing
    // the panel has to fit around, so measuring the panel off the same rectangle the rows use
    // is what keeps the two from drifting apart the way the chip strip and its layout once
    // could not (see legendRowAt's comment - agreement between the painter and the hit-test
    // only holds if both read from one function, and this makes the panel one of the things
    // that reads from it too).
    //
    // Collapsed, the panel is exactly tall enough for the handle and nothing else - that is
    // what makes collapsing actually free the plot rather than just hiding the row text
    // while the backdrop and the scrim keep darkening the same rectangle.
    const auto handle = legendHandleBounds();
    const int  rows   = legendCollapsed_ ? 1 : (kNumTraces + 1);

    return { handle.getX() - 5.0f, handle.getY() - 4.0f,
             handle.getWidth() + 10.0f, 15.0f * static_cast<float>(rows) + 6.0f };
}

int EqGraphView::legendRowAt(juce::Point<float> p) const
{
    // Collapsed, the trace rows are not painted at all - hit-testing them anyway would let a
    // click land on a switch nobody can see, which is a worse trap than the reflow bug the
    // "rows never move" test above exists to catch.
    if (legendCollapsed_)
        return -1;

    const auto panel = legendPanelBounds();

    if (! panel.contains(p))
        return -1;

    // The handle occupies the top of the same panel but is not a trace row; mouseDown checks
    // legendHandleBounds() before this function ever runs, but legendRowAt has to be correct
    // standing on its own too - the render test probes it directly, without going through a
    // click at all.
    if (legendHandleBounds().contains(p))
        return -1;

    // Inside the panel but between two rows, or in the panel's own padding: clamp to
    // whichever row is nearest rather than returning -1. A dead strip inside the legend
    // would fall through to onResetPeaks, which is a surprising thing to do to someone who
    // was aiming at a row and landed a couple of pixels off it.
    for (int i = 0; i < kNumTraces; ++i)
        if (legendRowBounds(i).contains(p))
            return i;

    const auto  row0 = legendRowBounds(0);
    const float rel  = (p.y - row0.getY()) / 15.0f;

    return juce::jlimit(0, kNumTraces - 1, static_cast<int>(std::round(rel)));
}

void EqGraphView::drawLegendBackdrop(juce::Graphics& g, juce::Rectangle<float>) const
{
    // The ENTIRE reason this panel exists: the legend sits inside the plot, so its lower
    // rows land on the wet spectrum's bright fill and the text loses contrast against it -
    // with five rows the bottom two were sitting on solid red and were effectively
    // unreadable. Painted here, before any trace, it only darkens the fill underneath; it
    // does not occlude anything, because everything it could occlude is drawn afterwards.
    // (Paint this same fill AFTER the traces instead and you have reintroduced the exact bug
    // this view was rebuilt to stop reproducing - see paint()'s comment at the call site.)
    g.setColour(juce::Colour(FXCOLOR(WindowBackground)).withAlpha(0.78f));
    g.fillRoundedRectangle(legendPanelBounds(), 4.0f);
}

void EqGraphView::drawLegendForeground(juce::Graphics& g, juce::Rectangle<float>) const
{
    // Without this the plot is unreadable: five traces, two Y axes, no way to tell which is
    // which. Each entry names the trace AND the axis it is measured against. Drawn AFTER
    // every trace so the swatches and the text stay legible on top of whatever is under them.
    const auto entries = legendEntries();

    // The backdrop alone is not enough any more. It goes down BEFORE the traces, so the wet
    // spectrum's fill paints straight over it, and the bottom rows' text ends up sitting on
    // solid red again - the exact illegibility the panel was introduced to cure.
    //
    // So lay a second, much thinner wash over the traces just before the text. At 0.35 a trace
    // crossing the legend keeps about two thirds of its colour and stays plainly visible; the
    // 0.78 fill that used to sit here left it 22% and effectively erased it. This is the whole
    // trade: the legend dims what crosses it, but no longer eats it.
    //
    // legendPanelBounds() is already small when collapsed, so this scrim shrinks down to just
    // the handle's own rectangle with it - do not raise its alpha or widen this rect to cover
    // the collapsed panel's old footprint, or collapsing stops freeing the plot it sits on.
    g.setColour(juce::Colour(FXCOLOR(WindowBackground)).withAlpha(0.35f));
    g.fillRoundedRectangle(legendPanelBounds(), 4.0f);

    g.setFont(11.0f);

    // The handle is the legend's own header row, drawn regardless of collapsed state - it is
    // the one thing that must always be clickable, or there would be no way back from
    // collapsed. No colour swatch: it names the legend, not a trace, and a swatch here would
    // read as a sixth trace nobody drew. ASCII 'v' / '>' rather than a real chevron glyph -
    // the bundled Gilroy has no arrow glyphs and a failed glyph corrupts every word after it.
    g.setColour(juce::Colour(FXCOLOR(DefaultText)).withAlpha(0.85f));
    g.drawText(legendCollapsed_ ? "legend  >" : "legend  v",
               legendHandleBounds(), juce::Justification::centredLeft);

    // The smooth chip rides the right end of the same row, drawn regardless of collapsed state
    // so the toggle stays reachable when the rows are tucked away. It dims to 0.35 alpha when
    // off and lights to 0.85 when on - the exact on/off language the trace rows use - so its
    // state reads at a glance without a checkbox or a second colour. Right-justified to sit
    // hard against the panel's edge, clear of the 'legend' text at the left.
    g.setColour(juce::Colour(FXCOLOR(DefaultText)).withAlpha(smoothingEnabled_ ? 0.85f : 0.35f));
    g.drawText("smooth", smoothChipBounds(), juce::Justification::centredRight);

    // Collapsed, this is where the legend stops: no rows, no per-trace scrim beyond the
    // handle's own small rectangle above. The plot beneath the five rows' former footprint
    // gets nothing painted over it at all, which is what actually frees it - not just the
    // text disappearing while the backdrop and scrim keep darkening the same pixels.
    if (legendCollapsed_)
        return;

    for (int i = 0; i < kNumTraces; ++i)
    {
        const auto& e   = entries[static_cast<size_t>(i)];
        const auto  row = legendRowBounds(i);
        const bool  on  = traceVisible_[static_cast<size_t>(i)];

        // An off row stays in place and stays legible: the legend is still a complete list
        // of what this view CAN draw, just gone quiet. Reflowing the rows would move the
        // next switch out from under the cursor between clicks.
        g.setColour(e.colour.withMultipliedAlpha(on ? 1.0f : 0.22f));

        if (e.dashed)
        {
            // Echo the dashed stroke used for the trace itself, so the swatch matches what
            // is actually on screen.
            g.fillRect(row.getX(),        row.getCentreY() - 1.5f, 6.0f, 3.0f);
            g.fillRect(row.getX() + 9.0f, row.getCentreY() - 1.5f, 6.0f, 3.0f);
        }
        else
        {
            g.fillRect(row.getX(), row.getCentreY() - 1.5f, 14.0f, 3.0f);
        }

        // The clear gesture is a click on the PLOT, not on this row: a row click toggles the
        // trace like every other row. Say which, or the label lies. And with the hold off
        // there is nothing to clear, so the hint goes with it. The row itself keeps its full
        // width regardless - only the text drawn inside it shrinks - so nothing reflows.
        juce::String text = e.text;

        if (i == TracePeakHold && on)
            text += " - click plot to clear";

        g.setColour(juce::Colour(FXCOLOR(DefaultText)).withAlpha(on ? 0.85f : 0.35f));
        g.drawText(text, row.withTrimmedLeft(20.0f), juce::Justification::centredLeft);
    }
}
