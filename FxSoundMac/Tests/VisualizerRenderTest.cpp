#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
#include "EqCurveModel.h"
#include "EqGraphView.h"
#include "FxTheme.h"
#include "GoniometerView.h"
#include "SpectralSmoothing.h"
#include "SpectrumAnalyzer.h"

// Paints the visualizer views offscreen and asserts the things a screenshot cannot: that
// the smoothed curves never leave the range of the data they were drawn from, and that a
// hard-panned signal lands on the correct side of the goniometer.
//
// Both views are Components, but nothing here opens a window: they are painted into a
// juce::Image by the software renderer. The images are also written to disk (set
// FXMAC_RENDER_OUT to choose where) so the result can be eyeballed without having to
// reproduce live audio through BlackHole.

namespace {

juce::File outputDir()
{
    const auto fromEnv = juce::SystemStats::getEnvironmentVariable("FXMAC_RENDER_OUT", {});

    auto dir = fromEnv.isNotEmpty()
                 ? juce::File(fromEnv)
                 : juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("fxsound-visualizer-render");

    dir.createDirectory();
    return dir;
}

juce::Image renderComponent(juce::Component& c, int width, int height)
{
    c.setBounds(0, 0, width, height);

    juce::Image image(juce::Image::ARGB, width, height, true);
    juce::Graphics g(image);
    c.paintEntireComponent(g, false);

    return image;
}

// A stand-in for real audio: a falling spectrum with one narrow spike, which is the shape
// that makes an overshooting curve misbehave.
std::vector<float> syntheticSpectrum(float tiltDb, int spikePoint, float spikeDb)
{
    std::vector<float> db(static_cast<size_t>(SpectrumAnalyzer::kNumPoints));

    for (int i = 0; i < SpectrumAnalyzer::kNumPoints; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(SpectrumAnalyzer::kNumPoints - 1);
        db[static_cast<size_t>(i)] = -18.0f + tiltDb * t - 30.0f * t * t;
    }

    if (spikePoint >= 0 && spikePoint < SpectrumAnalyzer::kNumPoints)
        db[static_cast<size_t>(spikePoint)] = spikeDb;   // a one-point spike: worst case

    return db;
}

// A left-click at `p`, in the component's own coordinates. JUCE has no ready-made way to
// synthesise one, and the alternative — exposing a test-only "handleClick" seam — would
// test a method the app never calls. This drives the real mouseDown override.
juce::MouseEvent clickAt(juce::Component& c, juce::Point<float> p)
{
    const auto now = juce::Time::getCurrentTime();

    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                            p,
                            juce::ModifierKeys(),
                            1.0f,                  // pressure
                            0.0f, 0.0f,            // orientation, rotation
                            0.0f, 0.0f,            // tiltX, tiltY
                            &c, &c,
                            now,
                            p, now,
                            1,                     // one click
                            false);                // not a drag
}

// Counts pixels in `region` whose hue matches `target`. An exact RGB match finds nothing —
// every trace is drawn translucent over the widget background — so match on hue, and use
// saturation to separate traces that share one: the peak-hold pink (0xffffb3be) has almost
// exactly the accent red's hue (0xef4b65) and differs only in how saturated it is.
int countHuePixels(const juce::Image& image, juce::Rectangle<float> region,
                   juce::Colour target, float minSaturation)
{
    int hits = 0;

    for (int y = (int) region.getY(); y < (int) region.getBottom(); ++y)
    {
        for (int x = (int) region.getX(); x < (int) region.getRight(); ++x)
        {
            const auto p = image.getPixelAt(x, y);

            if (p.getAlpha() < 128)
                continue;

            if (std::abs(p.getHue() - target.getHue()) < 0.03f
                && p.getSaturation() > minSaturation
                && p.getBrightness() > 0.15f)
                ++hits;
        }
    }

    return hits;
}

// countHuePixels cannot find the IN trace: white is achromatic, so it has no stable hue, and
// the little saturation an alpha-blended white pixel does pick up can land close enough to 0
// (red's hue, which wraps there too) to risk matching the wrong trace. Match near-white
// directly on the RGB channels instead - the widget background is 0x181818, so even a single
// alpha-blended white stroke pushes all three channels well past a fully-lit background pixel.
int countWhitePixels(const juce::Image& image, juce::Rectangle<float> region)
{
    int hits = 0;

    for (int y = (int) region.getY(); y < (int) region.getBottom(); ++y)
    {
        for (int x = (int) region.getX(); x < (int) region.getRight(); ++x)
        {
            const auto p = image.getPixelAt(x, y);

            if (p.getAlpha() < 128)
                continue;

            if (p.getRed() > 200 && p.getGreen() > 200 && p.getBlue() > 200)
                ++hits;
        }
    }

    return hits;
}

// The graph, sized and loaded with all five traces. Used by the visibility tests, which care
// about what disappears rather than about the exact numbers.
//
// The bounds are set HERE and not left to renderComponent: plotArea() is derived from the
// component's bounds, and a test that reads plotArea() before its first paint would otherwise
// be scanning an empty rectangle and passing for the wrong reason.
void loadAllTraces(EqGraphView& view)
{
    view.setBounds(0, 0, 900, 420);

    const auto dry = syntheticSpectrum(6.0f,  128, -6.0f);
    const auto wet = syntheticSpectrum(14.0f, 128, -2.0f);

    auto peak = wet;
    for (size_t i = 0; i < peak.size(); ++i)
        peak[i] = juce::jmin(0.0f, wet[i] + 4.0f);

    std::vector<float> delta(wet.size()), confidence(wet.size(), 1.0f);
    for (size_t i = 0; i < wet.size(); ++i)
        delta[i] = wet[i] - dry[i];

    const std::vector<EqCurveModel::Band> bands {
        { 110.0f, 4.0f }, { 1000.0f, -3.0f }, { 8000.0f, 6.0f }
    };

    view.setSpectra(dry, wet);
    view.setPeaks(peak);
    view.setDeltaCurve(delta, confidence);
    view.setEqCurve(EqCurveModel::computeCurveDb(bands,
                                                 SpectrumAnalyzer::kNumPoints,
                                                 SpectrumAnalyzer::kMinFreqHz,
                                                 SpectrumAnalyzer::kMaxFreqHz),
                    bands);
}

// The measured trace, driven up into the legend panel's own top-left corner: +12 dB at
// 20 Hz, sloping down across the plot, at full confidence so it draws solid rather than
// gated out. Shared by the draw-order regression test and the collapse test below - both
// need the SAME trace sitting exactly where the panel is, or a colour match found somewhere
// else on the plot would pass either test for the wrong reason.
std::pair<std::vector<float>, std::vector<float>> deltaThroughLegendCorner()
{
    const int n = SpectrumAnalyzer::kNumPoints;
    std::vector<float> delta(static_cast<size_t>(n));
    std::vector<float> confidence(static_cast<size_t>(n), 1.0f);

    for (int i = 0; i < n; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(n - 1);
        delta[static_cast<size_t>(i)] = 12.0f - 20.0f * t;
    }

    return { delta, confidence };
}

} // namespace

struct VisualizerRenderTest : juce::UnitTest
{
    VisualizerRenderTest() : juce::UnitTest("VisualizerRender") {}

    void runTest() override
    {
        // Painting needs the GUI subsystem up, even though no window is ever shown.
        const juce::ScopedJuceInitialiser_GUI gui;

        const auto dir = outputDir();

        beginTest("the smoothed spectrum curve never overshoots the data");
        {
            EqGraphView view;
            view.setBounds(0, 0, 900, 420);

            // A single-point spike next to a smooth slope: Catmull-Rom's worst case. If the
            // control points are not clamped, the curve dips below the data either side of
            // the spike and draws a notch that is not in the audio.
            const auto db   = syntheticSpectrum(10.0f, 128, -3.0f);
            const auto plot = view.plotArea();
            const auto path = view.buildSpectrumPath(db, plot);

            // Screen y grows downward, so a LOUDER dB is a SMALLER y. Walk every cubic and
            // require each control point to stay inside its own segment's y range: that is
            // exactly the invariant the clamp exists to enforce.
            juce::Path::Iterator it(path);

            juce::Point<float> previous;
            bool  havePrevious = false;
            int   cubics       = 0;
            float worstEscape  = 0.0f;

            while (it.next())
            {
                if (it.elementType == juce::Path::Iterator::startNewSubPath)
                {
                    previous     = { it.x1, it.y1 };
                    havePrevious = true;
                    continue;
                }

                if (it.elementType != juce::Path::Iterator::cubicTo || ! havePrevious)
                    continue;

                const juce::Point<float> c1  { it.x1, it.y1 };
                const juce::Point<float> c2  { it.x2, it.y2 };
                const juce::Point<float> end { it.x3, it.y3 };

                const float lo = juce::jmin(previous.y, end.y);
                const float hi = juce::jmax(previous.y, end.y);

                worstEscape = juce::jmax(worstEscape,
                                         lo - juce::jmin(c1.y, c2.y),   // escaped above
                                         juce::jmax(c1.y, c2.y) - hi);  // escaped below

                previous = end;
                ++cubics;
            }

            expect(cubics == SpectrumAnalyzer::kNumPoints - 1,
                   "expected one cubic per gap between display points, got "
                       + juce::String(cubics));

            expect(worstEscape <= 0.01f,
                   "a control point left its segment's dB range by "
                       + juce::String(worstEscape) + " px: the curve is inventing values");
        }

        beginTest("smoothDb leaves a flat spectrum flat and does not bias the ends");
        {
            // The clamped ends must average only the neighbours that exist, never zero-pad:
            // zero-padding would drag point 0 up toward 0 dB. A flat input is the proof - if
            // any point moves off the flat line, the window is reaching past the array.
            std::vector<float> flat(64, -40.0f);
            const auto out = smoothDb(flat, 4);

            expectEquals((int) out.size(), 64);
            for (size_t i = 0; i < out.size(); ++i)
                expectWithinAbsoluteError(out[i], -40.0f, 1.0e-4f);
        }

        beginTest("smoothDb spreads a spike into a lower symmetric hump and never invents a peak");
        {
            const int hw  = 4;
            const int mid = 32;

            std::vector<float> s(65, -80.0f);
            s[static_cast<size_t>(mid)] = -20.0f;   // one loud point in a quiet field

            const auto out = smoothDb(s, hw);

            // A moving average of one loud point among quiet ones cannot stay as tall as the
            // point - which is the whole reason smoothing softens the spikiness.
            expect(out[static_cast<size_t>(mid)] < s[static_cast<size_t>(mid)],
                   "the spike was not lowered: " + juce::String(out[static_cast<size_t>(mid)]));

            // It spreads symmetrically, exactly hw points either side and no further.
            for (int d = 1; d <= hw; ++d)
                expectWithinAbsoluteError(out[static_cast<size_t>(mid - d)],
                                          out[static_cast<size_t>(mid + d)], 1.0e-4f);

            expect(out[static_cast<size_t>(mid - hw)] > -80.0f + 1.0e-3f,
                   "the hump should reach the edge of its window");
            expectWithinAbsoluteError(out[static_cast<size_t>(mid - hw - 1)], -80.0f, 1.0e-4f);

            // The load-bearing guarantee - the spectral analogue of the overshoot test above:
            // a moving average can never exceed the loudest raw value within its own window, so
            // it can never draw a peak that is not in the audio.
            for (int i = 0; i < (int) s.size(); ++i)
            {
                const int lo = juce::jmax(0, i - hw);
                const int hi = juce::jmin((int) s.size() - 1, i + hw);

                float wmax = -1000.0f, wmin = 1000.0f;
                for (int j = lo; j <= hi; ++j)
                {
                    wmax = juce::jmax(wmax, s[static_cast<size_t>(j)]);
                    wmin = juce::jmin(wmin, s[static_cast<size_t>(j)]);
                }

                expect(out[static_cast<size_t>(i)] <= wmax + 1.0e-4f
                           && out[static_cast<size_t>(i)] >= wmin - 1.0e-4f,
                       "smoothed point " + juce::String(i) + " left its window's range");
            }
        }

        beginTest("smoothing conserves band energy - a spike spreads without being thrown away");
        {
            // The mitigation, locked: smoothing must average in the POWER domain, not dB. A
            // flat-weight moving average of power conserves total energy - the spike's power is
            // spread across its window, not destroyed - so the smoothed trace sits at the real
            // average level instead of the geometric-mean level, which read as artificially
            // quiet. dB-domain averaging FAILS this test: it throws the energy away.
            const int hw  = 4;
            const int mid = 32;

            std::vector<float> s(65, -120.0f);   // a deep floor, so its energy is negligible
            s[static_cast<size_t>(mid)] = 0.0f;  // one full-scale point: power == 1

            const auto out = smoothDb(s, hw);

            auto power = [](float db) { return std::pow(10.0f, db / 10.0f); };

            float inEnergy = 0.0f, outEnergy = 0.0f;
            for (size_t i = 0; i < s.size(); ++i)
            {
                inEnergy  += power(s[static_cast<size_t>(i)]);
                outEnergy += power(out[static_cast<size_t>(i)]);
            }

            expectWithinAbsoluteError(outEnergy, inEnergy, inEnergy * 0.02f);
        }

        beginTest("smoothDb with a zero window changes nothing");
        {
            const auto s   = syntheticSpectrum(10.0f, 128, -3.0f);
            const auto out = smoothDb(s, 0);

            expectEquals((int) out.size(), (int) s.size());
            for (size_t i = 0; i < s.size(); ++i)
                expectWithinAbsoluteError(out[i], s[i], 1.0e-6f);
        }

        beginTest("smoothing lowers a wet spike on the plot");
        {
            EqGraphView view;
            view.setBounds(0, 0, 900, 420);

            const int n     = SpectrumAnalyzer::kNumPoints;
            const int spike = (3 * n) / 4;   // right of centre, clear of the top-left legend

            std::vector<float> wet(static_cast<size_t>(n), -90.0f);
            std::vector<float> dry(static_cast<size_t>(n), -90.0f);
            wet[static_cast<size_t>(spike)] = -4.0f;   // one tall narrow tone

            view.setSpectra(dry, wet);

            const auto  plot   = view.plotArea();
            const float spikeX = plot.getX()
                               + (float) spike / (float) (n - 1) * plot.getWidth();

            // A narrow strip across the TOP TENTH of the plot (dB 0 down to about -9), over the
            // spike's column. Raw, the -4 dB tone reaches up into it; smoothed to 1/3 octave in
            // the POWER domain, the lone spike spreads to about -13.5 dB - lower than the raw
            // peak, so it is planed off the top strip, but nowhere near the floor: the energy is
            // preserved, not thrown away. (dB-domain averaging would crush it to about -80.)
            const auto topStrip = juce::Rectangle<float>(spikeX - 12.0f, plot.getY(),
                                                         24.0f, plot.getHeight() * 0.10f);
            const auto wetHue   = juce::Colour(FxTheme::getColor(FxColor::EqStart));

            view.setSmoothingEnabled(false);
            const int rawTop = countHuePixels(renderComponent(view, 900, 420), topStrip, wetHue, 0.35f);

            view.setSmoothingEnabled(true);
            const int smoothedTop = countHuePixels(renderComponent(view, 900, 420), topStrip, wetHue, 0.35f);

            writePng(renderComponent(view, 900, 420), dir.getChildFile("eq-graph-smoothed-wet.png"));

            expect(rawTop > 0,
                   "the raw wet spike should reach the top strip; found " + juce::String(rawTop));
            expect(smoothedTop < rawTop,
                   "smoothing did not lower the wet spike: raw " + juce::String(rawTop)
                       + " top pixels, smoothed " + juce::String(smoothedTop));
        }

        beginTest("smoothing lowers the peak-hold spike too");
        {
            // Q2, locked: the peak-hold rides the same loudness axis as the wet spectrum, so a
            // jagged hold sitting over a smoothed curve would look like a bug. It smooths with
            // the spectra - accepting that the hold now means "the held maximum, 1/3 octave".
            EqGraphView view;
            view.setBounds(0, 0, 900, 420);

            const int n     = SpectrumAnalyzer::kNumPoints;
            const int spike = (3 * n) / 4;

            std::vector<float> floorSpec(static_cast<size_t>(n), -90.0f);
            std::vector<float> peak(static_cast<size_t>(n), -90.0f);
            peak[static_cast<size_t>(spike)] = -4.0f;

            view.setSpectra(floorSpec, floorSpec);   // keep the wet fill out of the strip
            view.setPeaks(peak);

            const auto  plot   = view.plotArea();
            const float spikeX = plot.getX()
                               + (float) spike / (float) (n - 1) * plot.getWidth();
            // Top tenth only (dB 0 to about -9): the raw -4 dB spike reaches it, the smoothed
            // ~-13.5 dB plateau does not - the peak is lowered but energy-preserving, matching
            // the wet-spike test above. (A top-quarter strip would still catch the plateau and
            // prove nothing.)
            const auto topStrip = juce::Rectangle<float>(spikeX - 12.0f, plot.getY(),
                                                         24.0f, plot.getHeight() * 0.10f);
            const auto peakHue  = juce::Colour(0xffffb3be);

            view.setSmoothingEnabled(false);
            const int rawTop = countHuePixels(renderComponent(view, 900, 420), topStrip, peakHue, 0.2f);

            view.setSmoothingEnabled(true);
            const int smoothedTop = countHuePixels(renderComponent(view, 900, 420), topStrip, peakHue, 0.2f);

            expect(rawTop > 0,
                   "the raw peak-hold spike should reach the top strip; found " + juce::String(rawTop));
            expect(smoothedTop < rawTop,
                   "smoothing did not lower the peak-hold spike: raw " + juce::String(rawTop)
                       + ", smoothed " + juce::String(smoothedTop));
        }

        beginTest("clicking the smooth chip toggles smoothing and leaves the peak hold alone");
        {
            EqGraphView view;
            view.setBounds(0, 0, 900, 420);

            int resets = 0;
            view.onResetPeaks = [&resets] { ++resets; };

            const auto chip = view.smoothChipBounds().getCentre();

            expect(! view.isSmoothingEnabled(), "smoothing must start off");

            view.mouseDown(clickAt(view, chip));
            expect(view.isSmoothingEnabled(), "clicking the smooth chip did not turn smoothing on");
            expect(resets == 0, "toggling smoothing must not clear the peak hold");

            view.mouseDown(clickAt(view, chip));
            expect(! view.isSmoothingEnabled(), "clicking the smooth chip again did not turn it off");
            expect(resets == 0, "toggling smoothing off must not clear the peak hold either");
        }

        beginTest("the smooth chip and the collapse handle do not trigger each other");
        {
            EqGraphView view;
            view.setBounds(0, 0, 900, 420);

            const auto chip   = view.smoothChipBounds();
            const auto handle = view.legendHandleBounds();

            // The chip sits inside the handle's row on purpose; mouseDown must resolve the
            // overlap in the chip's favour, and the rest of the handle must still collapse.
            expect(handle.contains(chip.getCentre()),
                   "the chip is expected to sit inside the handle row (mouseDown checks it first)");

            const auto handleLeft = juce::Point<float>(handle.getX() + 4.0f, handle.getCentreY());
            expect(! chip.contains(handleLeft),
                   "test setup: the handle-left probe fell on the chip");

            view.mouseDown(clickAt(view, chip.getCentre()));
            expect(view.isSmoothingEnabled(), "the chip did not toggle smoothing");
            expect(! view.isLegendCollapsed(), "the chip must not collapse the legend");

            view.mouseDown(clickAt(view, handleLeft));
            expect(view.isLegendCollapsed(), "the handle did not collapse the legend");
            expect(view.isSmoothingEnabled(), "collapsing must not have changed smoothing");
        }

        beginTest("smoothing never touches the measured trace");
        {
            // Q3, locked: DeltaTrace gates on the raw dry level, so smoothing the spectra first
            // would shift where the cyan trace breaks - a behavioural change to a diagnostic,
            // not a cosmetic one. It stays raw, and this proves the render is byte-for-byte the
            // same with smoothing on and off.
            EqGraphView view;
            view.setBounds(0, 0, 900, 420);

            view.setTraceVisible(EqGraphView::TraceIn,       false);
            view.setTraceVisible(EqGraphView::TraceOut,      false);
            view.setTraceVisible(EqGraphView::TracePeakHold, false);
            view.setTraceVisible(EqGraphView::TraceEqCurve,  false);

            const int n = SpectrumAnalyzer::kNumPoints;
            std::vector<float> delta(static_cast<size_t>(n)), confidence(static_cast<size_t>(n), 1.0f);
            for (int i = 0; i < n; ++i)
                delta[static_cast<size_t>(i)] = (i % 8 < 4) ? 8.0f : -8.0f;   // deliberately jagged

            view.setDeltaCurve(delta, confidence);

            const auto plot = view.plotArea();
            const auto cyan = juce::Colour(0xff4dd0e1);

            view.setSmoothingEnabled(false);
            const int rawCyan = countHuePixels(renderComponent(view, 900, 420), plot, cyan, 0.3f);

            view.setSmoothingEnabled(true);
            const int smoothedCyan = countHuePixels(renderComponent(view, 900, 420), plot, cyan, 0.3f);

            expect(rawCyan > 0, "the measured trace should be drawn; found no cyan");
            expectEquals(smoothedCyan, rawCyan,
                         "smoothing changed the measured trace - it must stay raw");
        }

        beginTest("the legend no longer erases a trace that passes under it");
        {
            // The regression this pins down: the legend panel sits INSIDE the plot, top-left,
            // at 0.78 alpha - and used to be painted LAST, after every trace, which erased
            // whatever crossed its corner. The broad wet fill still showed through at that
            // alpha; the 1.4px dashed measured trace did not, and simply vanished - which
            // read as the trace "breaking apart" rather than being painted over. The fix is
            // draw order (backdrop before the traces, text after), not position - so this
            // test proves a trace is still visible ON TOP of the panel, not that the panel
            // moved somewhere out of the way.
            EqGraphView view;
            view.setBounds(0, 0, 900, 420);

            // +12 dB at 20 Hz, sloping down across the plot: full confidence, so the trace is
            // drawn solid, and starts right in the legend panel's own corner - exactly where
            // the old draw order erased it.
            const auto [delta, confidence] = deltaThroughLegendCorner();
            view.setDeltaCurve(delta, confidence);

            const auto image = renderComponent(view, 900, 420);
            const auto cyan  = juce::Colour(0xff4dd0e1);

            // Exclude the swatch column: the "measured" row's own legend swatch is drawn in
            // this identical cyan and sits inside the panel, so scanning the whole panel would
            // report "trace visible" whether or not the bug is fixed. The swatches occupy
            // roughly the first 20px of each row; trim a little further to clear them with
            // margin rather than scanning right up against their edge.
            const auto panel = view.legendPanelBounds();
            const auto scan  = panel.withTrimmedLeft(25.0f);

            // 0.5 is chosen, not the countHuePixels default: a pixel painted straight on top of
            // the traces lands around 0.65 saturation; one painted UNDER the panel's 0.78-alpha
            // overlay (itself the same near-black as the widget background, so it does not fully
            // desaturate what is under it) lands below 0.4. 0.5 sits cleanly between the two, so
            // this genuinely distinguishes "drawn on top of the panel" from "drawn under it".
            const int cyanHits = countHuePixels(image, scan, cyan, 0.5f);

            expect(cyanHits > 0,
                   "expected the measured trace to paint cyan on top of the legend panel; found "
                       + juce::String(cyanHits) + " - the panel is erasing it again");

            writePng(image, dir.getChildFile("eq-graph-legend-regression.png"));
        }

        beginTest("collapsing the legend frees the plot underneath it");
        {
            // Same corner-pinning trick as the draw-order regression above, reused rather
            // than re-derived: it is the one trace guaranteed to sit under every row the
            // panel currently has, expanded or not.
            EqGraphView view;
            loadAllTraces(view);

            const auto [delta, confidence] = deltaThroughLegendCorner();
            view.setDeltaCurve(delta, confidence);

            const auto cyan = juce::Colour(0xff4dd0e1);

            // Fixed to the TRACE ROWS specifically, not legendPanelBounds() - that call
            // answers a different question once collapsed (it shrinks to the handle alone),
            // and scanning it post-collapse would only prove the handle renders, not that the
            // plot got its ink back. legendRowBounds() keeps returning this geometry
            // regardless of legendCollapsed_ (see its own comment), so this rectangle, captured
            // once, is valid for both renders below.
            const auto row0    = view.legendRowBounds(0);
            const auto rowLast = view.legendRowBounds(EqGraphView::kNumTraces - 1);
            const auto scan    = juce::Rectangle<float>(row0.getX() + 20.0f, row0.getY(),
                                                         row0.getWidth() - 20.0f,
                                                         rowLast.getBottom() - row0.getY());

            expect(! view.isLegendCollapsed(), "the legend must start expanded");

            const auto expandedImage = renderComponent(view, 900, 420);
            writePng(expandedImage, dir.getChildFile("eq-graph-legend-expanded.png"));

            view.setLegendCollapsed(true);

            const auto collapsedImage = renderComponent(view, 900, 420);
            writePng(collapsedImage, dir.getChildFile("eq-graph-legend-collapsed.png"));

            // Measured directly against these renders (sort every matching-hue pixel's
            // saturation in the scan region and bucket it): the scrimmed (expanded) render's
            // saturations top out at 0.618, and NONE of its 207 matching pixels clear 0.65.
            // The unscrimmed (collapsed) render's 214 matching pixels reach as high as 0.654,
            // and 17 of them clear 0.65. 0.65 is therefore not a guess sitting "between two
            // clusters" - it is the exact ceiling of the expanded render, so it is a floor
            // ONLY the collapsed render can ever clear. (A lower floor, e.g. 0.35, finds ink
            // in both - 164 and 200 pixels respectively - which is what proves collapsing
            // does not remove the trace, only what sits on top of it.)
            constexpr float kPresenceFloor    = 0.35f;
            constexpr float kUnscrimmedFloor  = 0.65f;

            const int expandedPresence  = countHuePixels(expandedImage,  scan, cyan, kPresenceFloor);
            const int collapsedPresence = countHuePixels(collapsedImage, scan, cyan, kPresenceFloor);
            const int expandedHighSat   = countHuePixels(expandedImage,  scan, cyan, kUnscrimmedFloor);
            const int collapsedHighSat  = countHuePixels(collapsedImage, scan, cyan, kUnscrimmedFloor);

            expect(expandedPresence > 0,
                   "expected the measured trace to paint cyan through the (scrimmed) trace-row "
                   "region while expanded; found " + juce::String(expandedPresence));

            expect(collapsedPresence > 0,
                   "expected the measured trace to still paint cyan in that region once "
                   "collapsed; found " + juce::String(collapsedPresence)
                       + " - collapsing must not remove the trace itself, only the legend");

            expect(expandedHighSat == 0,
                   "the scrimmed render should not be able to clear " + juce::String(kUnscrimmedFloor)
                       + " saturation at all; found " + juce::String(expandedHighSat)
                       + " - either the fixture changed or the 0.35 scrim is not actually there");

            expect(collapsedHighSat > 0,
                   "expected the collapsed trace to clear a saturation floor ("
                       + juce::String(kUnscrimmedFloor)
                       + ") the scrimmed render cannot reach at all; found none - collapsing hid "
                         "the rows' TEXT but left their scrim darkening the plot underneath it");
        }

        beginTest("nothing hit-tests to a legend row while it is collapsed");
        {
            // The rows are switches; collapsed, they are not drawn, and a switch nobody can
            // see must not still be reachable by a click that happens to land where it used
            // to be - that is worse than the reflow bug "toggling a trace moves no row"
            // (further down) exists to catch, because there the row was at least visible.
            EqGraphView view;
            view.setBounds(0, 0, 900, 420);

            std::vector<juce::Point<float>> rowCentres;
            for (int i = 0; i < EqGraphView::kNumTraces; ++i)
                rowCentres.push_back(view.legendRowBounds(i).getCentre());

            const auto handleCentre = view.legendHandleBounds().getCentre();

            // Expanded (the default): a sanity check that this probe is even meaningful -
            // every former row centre already resolves to its own row, and the handle
            // resolves to no row at all.
            for (int i = 0; i < EqGraphView::kNumTraces; ++i)
                expect(view.legendRowAt(rowCentres[static_cast<size_t>(i)]) == i,
                       "setup bug: row " + juce::String(i)
                           + "'s centre did not hit-test to itself while expanded");

            expect(view.legendRowAt(handleCentre) == -1,
                   "the handle must not hit-test as a trace row even while expanded");

            view.setLegendCollapsed(true);

            for (int i = 0; i < EqGraphView::kNumTraces; ++i)
                expect(view.legendRowAt(rowCentres[static_cast<size_t>(i)]) == -1,
                       "row " + juce::String(i)
                           + "'s former centre still hit-tests to a row once collapsed");

            // The handle itself must keep answering, collapsed or not - it is the only
            // control left, and its own bounds are what mouseDown checks first.
            expect(view.legendHandleBounds().contains(handleCentre),
                   "the handle's own centre fell outside its own bounds once collapsed");
            expect(view.legendRowAt(handleCentre) == -1,
                   "the handle must not hit-test as a trace row while collapsed either");
        }

        beginTest("click routing: the handle toggles collapse, a row toggles its trace, "
                  "the plot resets, dead space does neither");
        {
            EqGraphView view;
            view.setBounds(0, 0, 900, 420);

            int resets = 0;
            view.onResetPeaks = [&resets] { ++resets; };

            const auto handleCentre = view.legendHandleBounds().getCentre();

            expect(! view.isLegendCollapsed(), "the legend must start expanded");

            view.mouseDown(clickAt(view, handleCentre));

            expect(view.isLegendCollapsed(), "clicking the handle did not collapse the legend");
            expect(resets == 0, "collapsing the legend must not clear the peak hold");

            view.mouseDown(clickAt(view, handleCentre));

            expect(! view.isLegendCollapsed(),
                   "clicking the handle a second time did not re-expand the legend");
            expect(resets == 0, "expanding the legend must not clear the peak hold");

            // A trace row, expanded: toggles the trace, still no reset - the coupling the
            // pre-existing "a legend row toggles that trace" test covers, re-asserted here so
            // this test stands as the single place documenting the FULL routing order.
            const auto eqRow = view.legendRowBounds(EqGraphView::TraceEqCurve).getCentre();

            expect(view.isTraceVisible(EqGraphView::TraceEqCurve), "every trace starts visible");

            view.mouseDown(clickAt(view, eqRow));

            expect(! view.isTraceVisible(EqGraphView::TraceEqCurve),
                   "clicking a legend row did not toggle its trace");
            expect(resets == 0, "toggling a trace row must not clear the peak hold");

            view.mouseDown(clickAt(view, eqRow));   // put it back for the assertions below
            expect(view.isTraceVisible(EqGraphView::TraceEqCurve));

            // The plot body still resets, exactly as before the handle existed.
            const auto plot = view.plotArea();
            view.mouseDown(clickAt(view, plot.getCentre()));

            expect(resets == 1, "a click in the plot body must still clear the peak hold");

            // True panel dead space - inside legendPanelBounds() but neither the handle nor a
            // row - only exists once collapsed: expanded, legendRowAt's own clamp (see its
            // comment) swallows every point inside the panel that is not the handle. Collapsed,
            // the panel is nearly all handle, but is 3 px taller than the handle itself, so a
            // point in that sliver is dead space that must resolve to nothing at all.
            view.setLegendCollapsed(true);

            const auto panel     = view.legendPanelBounds();
            const auto deadSpace = juce::Point<float>(panel.getCentreX(), panel.getBottom() - 1.0f);

            expect(! view.legendHandleBounds().contains(deadSpace),
                   "test setup bug: the dead-space probe landed on the handle, not past it");
            expect(view.legendRowAt(deadSpace) == -1,
                   "a collapsed legend has no rows for dead space to hit-test to");

            view.mouseDown(clickAt(view, deadSpace));

            expect(resets == 1,
                   "a click in the collapsed panel's dead space must not clear the peak hold");
            expect(view.isTraceVisible(EqGraphView::TraceEqCurve),
                   "a click in dead space must not have toggled any trace");
        }

        beginTest("collapsing shrinks the panel without moving the handle");
        {
            // The handle is the thing the user re-clicks to get the legend back; if it moved
            // when the panel resized around it, the second click would land on whatever the
            // plot put under it instead.
            EqGraphView view;
            view.setBounds(0, 0, 900, 420);

            const auto handleBefore  = view.legendHandleBounds();
            const auto expandedPanel = view.legendPanelBounds();

            view.setLegendCollapsed(true);

            const auto collapsedPanel = view.legendPanelBounds();
            const auto handleAfter    = view.legendHandleBounds();

            expect(collapsedPanel.getHeight() < expandedPanel.getHeight(),
                   "the panel did not shrink: expanded height "
                       + juce::String(expandedPanel.getHeight()) + ", collapsed height "
                       + juce::String(collapsedPanel.getHeight()));

            expect(handleBefore == handleAfter,
                   "the handle moved when the legend collapsed - a second click meant to "
                   "re-expand it would land somewhere else");

            expect(expandedPanel.contains(handleBefore) && collapsedPanel.contains(handleAfter),
                   "the handle fell outside its own panel - a shrink that clips it is as bad "
                   "as one that moves it");

            view.setLegendCollapsed(false);

            expect(view.legendPanelBounds() == expandedPanel,
                   "re-expanding did not restore the original panel size");
        }

        beginTest("every legend row hit-tests back to the row that was painted there");
        {
            // The painter and the click hit-test have to agree, to the pixel, about where
            // each row is — and the only way to prove they do is to feed one's output to
            // the other. A drifted row does not crash or look wrong; it just toggles the
            // wrong trace, which nobody would catch by eye.
            EqGraphView view;
            view.setBounds(0, 0, 900, 420);

            const auto plot = view.plotArea();

            for (int i = 0; i < EqGraphView::kNumTraces; ++i)
            {
                const auto row = view.legendRowBounds(i);

                expect(view.legendPanelBounds().contains(row),
                       "legend row " + juce::String(i) + " is not inside the legend panel");

                expect(view.legendRowAt(row.getCentre()) == i,
                       "the centre of legend row " + juce::String(i) + " hit-tested as row "
                           + juce::String(view.legendRowAt(row.getCentre())));
            }

            expect(view.legendRowAt(plot.getCentre()) == -1,
                   "the middle of the plot is not a legend row");

            expect(view.legendRowAt({ plot.getRight() - 4.0f, plot.getBottom() - 4.0f }) == -1,
                   "the bottom-right of the plot is not a legend row");
        }

        beginTest("toggling a trace moves no row");
        {
            // The rows are switches, and a switch that moves when you throw it is a trap: the
            // next click lands on whatever slid under the cursor. This is also what lets the
            // peak-hold row drop its "click plot to clear" hint when the hold is off - the row
            // keeps its own width regardless of which text is drawn inside it, so shortening
            // the caption cannot shift the rows after it.
            EqGraphView view;
            view.setBounds(0, 0, 900, 420);

            std::vector<juce::Rectangle<float>> before;

            for (int i = 0; i < EqGraphView::kNumTraces; ++i)
                before.push_back(view.legendRowBounds(i));

            // Switch every trace off, one at a time, checking after each that nothing has moved.
            for (int toggled = 0; toggled < EqGraphView::kNumTraces; ++toggled)
            {
                view.setTraceVisible(static_cast<EqGraphView::Trace>(toggled), false);

                for (int i = 0; i < EqGraphView::kNumTraces; ++i)
                    expect(view.legendRowBounds(i) == before[static_cast<size_t>(i)],
                           "switching trace " + juce::String(toggled) + " off moved row "
                               + juce::String(i));
            }
        }

        beginTest("a legend row toggles that trace; a plot click still clears the peak hold");
        {
            // Two gestures share one mouseDown now. The regression this pins down: a legend
            // click that ALSO clears the peak hold, silently throwing away the very
            // measurement the user was reading when they reached for the legend.
            EqGraphView view;
            view.setBounds(0, 0, 900, 420);

            int resets = 0;
            view.onResetPeaks = [&resets] { ++resets; };

            const auto plot  = view.plotArea();
            const auto eqRow = view.legendRowBounds(EqGraphView::TraceEqCurve).getCentre();

            expect(view.isTraceVisible(EqGraphView::TraceEqCurve),
                   "every trace starts visible");

            view.mouseDown(clickAt(view, eqRow));

            expect(! view.isTraceVisible(EqGraphView::TraceEqCurve),
                   "clicking the EQ curve's legend row did not switch it off");
            expect(resets == 0,
                   "a legend click must not clear the peak hold");

            // The other four traces are untouched: a click toggles one row, not a mode.
            expect(view.isTraceVisible(EqGraphView::TraceIn)
                       && view.isTraceVisible(EqGraphView::TraceOut)
                       && view.isTraceVisible(EqGraphView::TracePeakHold)
                       && view.isTraceVisible(EqGraphView::TraceMeasured),
                   "toggling one row changed the visibility of another");

            // The panel's own padding (here, just above row 0, inside the panel but outside
            // every row's own rectangle) clamps to its nearest row rather than falling through
            // to onResetPeaks - a dead strip inside the legend would be a surprising place for
            // a click aimed at the legend, and missing its row by a couple of pixels, to land.
            const auto panel   = view.legendPanelBounds();
            const auto padding = juce::Point<float>(panel.getCentreX(), panel.getY() + 1.0f);

            expect(view.legendRowAt(padding) == EqGraphView::TraceIn,
                   "a click in the panel's top padding should clamp to row 0, not miss every row");

            view.mouseDown(clickAt(view, padding));

            expect(resets == 0,
                   "a click in the legend panel's padding must not clear the peak hold");
            expect(! view.isTraceVisible(EqGraphView::TraceIn),
                   "clicking the padding above row 0 should have toggled row 0, not left it alone");

            view.mouseDown(clickAt(view, padding));   // put IN back on before the next assertions

            view.mouseDown(clickAt(view, plot.getCentre()));

            expect(resets == 1, "a click in the plot body must still clear the peak hold");
            expect(! view.isTraceVisible(EqGraphView::TraceEqCurve),
                   "a click in the plot body must not change trace visibility");

            view.mouseDown(clickAt(view, eqRow));

            expect(view.isTraceVisible(EqGraphView::TraceEqCurve),
                   "a second legend click did not switch the trace back on");
            expect(resets == 1, "the second legend click cleared the peak hold");
        }

        beginTest("switching a trace off takes its ink off the plot");
        {
            EqGraphView view;
            loadAllTraces(view);

            const auto plot = view.plotArea();

            // Scan away from the legend panel (top-left), or the OUT row's own colour swatch
            // would count as wet-spectrum ink and the test would pass with the trace still
            // painted. The panel gained the collapse handle as a sixth row, so it now ends at
            // plot.x + 293 and plot.y + 98 (was + 83); trim top by 115 rather than 100 to keep
            // the same clearance past it.
            const auto body = plot.withTrimmedLeft(300.0f).withTrimmedTop(115.0f);
            const auto wet  = juce::Colour(FxTheme::getColor(FxColor::EqStart));

            const int before = countHuePixels(renderComponent(view, 900, 420), body, wet, 0.45f);

            expect(before > 200,
                   "expected the wet spectrum to be painted; found only "
                       + juce::String(before) + " accent pixels");

            view.setTraceVisible(EqGraphView::TraceOut, false);

            const auto image = renderComponent(view, 900, 420);
            const int  after = countHuePixels(image, body, wet, 0.45f);

            expect(after == 0,
                   "the wet spectrum is switched off but " + juce::String(after)
                       + " accent pixels are still on the plot");

            writePng(image, dir.getChildFile("eq-graph-out-off.png"));
        }

        beginTest("hiding the EQ curve takes its stroke and its band markers with it");
        {
            // Only OUT's ink removal was covered above. The curve's own stroke and the band
            // markers were a separate, untested coupling: Minor 4 nested them under one `if`,
            // but nesting is not a test - delete that one `if` and amber dots would still float
            // on a plot with no curve under them, and nothing above would notice.
            EqGraphView view;
            loadAllTraces(view);

            const auto plot = view.plotArea();

            // Same precedent as the OUT test above: trim left 300, top 115 to stay off the
            // legend panel (now six rows tall with the collapse handle), whose EQ-curve swatch
            // is amber and would otherwise count as curve ink.
            const auto body  = plot.withTrimmedLeft(300.0f).withTrimmedTop(115.0f);
            const auto amber = juce::Colour(0xffffc857);

            const int before = countHuePixels(renderComponent(view, 900, 420), body, amber, 0.4f);

            expect(before > 0,
                   "expected the EQ curve stroke or a band marker to paint amber in the plot "
                       "body; found none - the scan region missed them");

            // Measured has to go too: it rides the gain axis's zero line, which is also amber,
            // and would otherwise keep the body non-zero for a reason that has nothing to do
            // with the EQ curve or its markers.
            view.setTraceVisible(EqGraphView::TraceEqCurve, false);
            view.setTraceVisible(EqGraphView::TraceMeasured, false);

            const auto image = renderComponent(view, 900, 420);
            const int  after  = countHuePixels(image, body, amber, 0.4f);

            expect(after == 0,
                   "the EQ curve and the gain axis are both off but " + juce::String(after)
                       + " amber pixels remain in the plot body - the curve stroke or a band "
                         "marker survived");

            writePng(image, dir.getChildFile("eq-graph-eqcurve-off.png"));
        }

        beginTest("the gain axis outlives the EQ curve but not the measured trace as well");
        {
            // The axis is drawn in the EQ curve's amber, but it belongs to BOTH traces that
            // ride it. Hide it too early and the measured trace floats against no scale;
            // hide it too late and an empty axis invites you to read the spectra against it.
            EqGraphView view;
            loadAllTraces(view);

            const auto plot  = view.plotArea();
            const auto amber = juce::Colour(0xffffc857);

            // The right-hand label strip: paint's gain labels sit in a 42 px box hard against
            // plot.getRight(). Nothing else amber is drawn there (the EQ curve's legend
            // swatch is at the far left).
            const auto strip = juce::Rectangle<float>(plot.getRight() - 46.0f, plot.getY(),
                                                      46.0f, plot.getHeight());

            expect(countHuePixels(renderComponent(view, 900, 420), strip, amber, 0.4f) > 20,
                   "the gain axis labels are missing with both of its traces visible");

            // EQ curve off, measured still on: the axis stays, because the measured trace is
            // still plotted against it.
            view.setTraceVisible(EqGraphView::TraceEqCurve, false);

            expect(countHuePixels(renderComponent(view, 900, 420), strip, amber, 0.4f) > 20,
                   "the gain axis vanished while the measured trace was still riding it");

            // The symmetric case this test is named after: EQ curve back on, measured off. A
            // full-height scan of `strip` cannot tell "the axis is there" apart from "the axis
            // is gone but the EQ curve itself, which spans the whole plot width, paints amber
            // through this strip anyway" - so scan only the +15 dB label at the very top.
            // yForGainDb puts it at plot.getY(), and loadAllTraces's curve is nowhere near
            // +15 dB that close to 20 kHz, so this slice is clear of curve ink either way.
            view.setTraceVisible(EqGraphView::TraceEqCurve, true);
            view.setTraceVisible(EqGraphView::TraceMeasured, false);

            const auto labelStrip = juce::Rectangle<float>(plot.getRight() - 46.0f, plot.getY(),
                                                            46.0f, 16.0f);

            // A correct render puts 53 amber pixels in this slice (the "+15 dB" glyphs); >20
            // keeps a wide margin below that while staying far above what the broken single-
            // flag gate produces here, which is 0.
            expect(countHuePixels(renderComponent(view, 900, 420), labelStrip, amber, 0.4f) > 20,
                   "the gain axis vanished while the EQ curve was still riding it");

            // Both off: nothing is plotted against the axis, so the axis goes.
            view.setTraceVisible(EqGraphView::TraceEqCurve, false);
            view.setTraceVisible(EqGraphView::TraceMeasured, false);

            const auto image = renderComponent(view, 900, 420);
            const int  left  = countHuePixels(image, strip, amber, 0.4f);

            expect(left == 0,
                   "both gain-axis traces are off but " + juce::String(left)
                       + " amber pixels remain in the axis strip");

            writePng(image, dir.getChildFile("eq-graph-gain-axis-off.png"));
        }

        beginTest("the hover readout only quotes traces that are actually on screen");
        {
            // Testing this through pixels would mean measuring the height of a box, which
            // tells you nothing about WHICH row went. The rows are the unit of meaning, so
            // assert on the rows: a readout that prints "OUT -12.3 dB" while the wet trace is
            // switched off is quoting a curve the user cannot see.
            EqGraphView view;
            loadAllTraces(view);

            constexpr int point = 128;   // the spike, which every synthetic trace has a value at

            auto textOf = [](const std::vector<EqGraphView::ReadoutRow>& rows)
            {
                juce::String all;
                for (const auto& r : rows)
                    all += r.text + "|";
                return all;
            };

            const auto full = view.readoutRowsAt(point);

            expect(full.size() == 4,
                   "expected frequency + IN + OUT + DIFF, got " + juce::String((int) full.size())
                       + ": " + textOf(full));
            expect(textOf(full).contains("IN") && textOf(full).contains("OUT")
                       && textOf(full).contains("DIFF"),
                   "a row is missing from the full readout: " + textOf(full));

            view.setTraceVisible(EqGraphView::TraceOut, false);

            const auto withoutOut = view.readoutRowsAt(point);

            expect(withoutOut.size() == 3,
                   "hiding OUT should drop exactly one row, got "
                       + juce::String((int) withoutOut.size()));
            expect(! textOf(withoutOut).contains("OUT"),
                   "the readout still quotes OUT with the wet trace switched off: "
                       + textOf(withoutOut));
            expect(textOf(withoutOut).contains("IN") && textOf(withoutOut).contains("DIFF"),
                   "hiding OUT took another trace's row with it: " + textOf(withoutOut));

            // The frequency belongs to the cursor, not to a trace, so it is always row 0 -
            // but a box with nothing in it but the frequency is not worth drawing, and
            // drawHoverReadout keys off exactly this.
            view.setTraceVisible(EqGraphView::TraceIn, false);
            view.setTraceVisible(EqGraphView::TraceMeasured, false);

            const auto bare = view.readoutRowsAt(point);

            expect(bare.size() == 1,
                   "with every value-bearing trace off, only the frequency row should remain, got "
                       + textOf(bare));

            expect(view.readoutRowsAt(-1).empty() && view.readoutRowsAt(99999).empty(),
                   "an out-of-range point must produce no rows at all");
        }

        beginTest("hiding IN takes its hover dot off the plot, not just its row of text");
        {
            // readoutRowsAt (tested above) only proves the TEXT rows react to visibility. The
            // dots drawHoverReadout paints on the two spectra are gated by their OWN pair of
            // ifs, entirely separate from readoutRowsAt's - delete one of those ifs and every
            // test above still passes, leaving a marker sitting on a curve that is not there.
            //
            // IN is picked over OUT: OUT's dot sits inside the opaque wet fill, so "did a red
            // pixel disappear" is ambiguous between the fill, the stroke and the dot. IN is a
            // bare white ghost painted over everything else, so a near-white pixel at its exact
            // position can only be the IN stroke or the IN dot - and both of those are gated on
            // the same trace flag, in production and in the deliberately-broken case alike.
            EqGraphView view;
            loadAllTraces(view);

            const auto plot = view.plotArea();

            constexpr int point = 128;   // the spike, same display point the readout test uses
            const float x = plot.getX()
                          + (static_cast<float>(point)
                             / static_cast<float>(SpectrumAnalyzer::kNumPoints - 1))
                            * plot.getWidth();

            // A full-height column at that x, not a box around a computed y: yForDb's mapping
            // from dB to a screen y is a private implementation detail this test has no
            // business depending on. The dry curve only crosses this column once - at the
            // exact point the dot sits on - so scanning the whole column finds it regardless.
            const auto column = juce::Rectangle<float>(x - 3.0f, plot.getY(), 6.0f, plot.getHeight());

            view.mouseMove(clickAt(view, { x, plot.getCentreY() }));

            const int before = countWhitePixels(renderComponent(view, 900, 420), column);

            expect(before > 0,
                   "expected the IN dot or its stroke to paint a near-white pixel in its own "
                       "column; found none - the scan box missed it");

            view.setTraceVisible(EqGraphView::TraceIn, false);

            const auto image = renderComponent(view, 900, 420);
            const int  after  = countWhitePixels(image, column);

            expect(after == 0,
                   "IN is hidden but " + juce::String(after)
                       + " near-white pixels remain in its column - the dot or the stroke "
                         "survived");

            writePng(image, dir.getChildFile("eq-graph-in-dot-off.png"));
        }

        beginTest("a hard-panned signal leans down the correct goniometer diagonal");
        {
            // The bug this pins down: the horizontal term was (l - rr), which mirrored the
            // scope and put left-only content on the RIGHT. Nothing else on the view would
            // have caught it — mono still pointed up and correlation was unaffected.
            //
            // Do NOT test this with a centroid. A single-channel SINE swings positive and
            // negative, so its dots trace a full line through the centre and the centroid
            // of that line is the centre, mirrored or not. What distinguishes the two cases
            // is which way the line LEANS:
            //
            //   left-only:  dx = (0 - l)k  and  dy = -(l + 0)k   =>  dx ==  dy   (lean "\")
            //   right-only: dx = (r - 0)k  and  dy = -(0 + r)k   =>  dx == -dy   (lean "/")
            //
            // So the sign of the covariance of (dx, dy) over the dot cloud IS the mirror.
            // Positive means left; negative means right. Note y grows downward on screen.
            const float leftLean  = diagonalLean(true,  dir.getChildFile("goniometer-hard-left.png"));
            const float rightLean = diagonalLean(false, dir.getChildFile("goniometer-hard-right.png"));

            expect(leftLean > 0.7f,
                   "a left-only signal must lean down the L diagonal (positive covariance), got "
                       + juce::String(leftLean));

            expect(rightLean < -0.7f,
                   "a right-only signal must lean down the R diagonal (negative covariance), got "
                       + juce::String(rightLean));
        }

        beginTest("peak hold strands itself above the live trace after a burst");
        {
            // Driven by the real SpectrumAnalyzer, not synthetic arrays: this is the only
            // render that actually demonstrates the feature. A loud burst charges both the
            // live trace and the hold; the silence that follows collapses the live trace
            // while the hold sinks slowly, so the gap between them IS the peak hold doing
            // its job. Faking the peak line as "wet + 4 dB" would draw the same picture
            // whether or not the analyzer worked.
            SpectrumAnalyzer analyzer;
            analyzer.setSampleRate(48000.0);

            constexpr int blockFrames = 512;
            std::vector<float> block(2 * blockFrames);

            juce::Random rng(1234);   // fixed seed: the render must not change run to run

            // A burst of band-limited noise plus a strong 1 kHz tone, so the hold has both
            // a broadband shape and one obvious spike to remember.
            double phase = 0.0;
            const double inc = juce::MathConstants<double>::twoPi * 1000.0 / 48000.0;

            for (int b = 0; b < 40; ++b)
            {
                for (int i = 0; i < blockFrames; ++i)
                {
                    const auto noise = 0.25f * (rng.nextFloat() * 2.0f - 1.0f);
                    const auto tone  = static_cast<float>(0.5 * std::sin(phase));
                    phase += inc;

                    block[static_cast<size_t>(2 * i)]     = noise + tone;
                    block[static_cast<size_t>(2 * i + 1)] = noise + tone;
                }

                analyzer.pushInterleavedStereo(block.data(), blockFrames);
                analyzer.update();
            }

            const auto charged = analyzer.getPeakDb();

            // Now silence. Half a second of frames: long enough for the fast-release live
            // trace to fall well away, far short of the hold's own decay to the floor.
            std::vector<float> silence(2 * blockFrames, 0.0f);

            for (int b = 0; b < 30; ++b)
            {
                analyzer.pushInterleavedStereo(silence.data(), blockFrames);
                analyzer.update();
            }

            const auto live = analyzer.getMagnitudesDb();
            const auto held = analyzer.getPeakDb();

            // The hold must sit clearly above the live trace across the audible band, or
            // there is nothing on screen to see.
            int separated = 0;

            for (int i = 0; i < SpectrumAnalyzer::kNumPoints; ++i)
            {
                const auto idx = static_cast<size_t>(i);

                if (held[idx] > live[idx] + 6.0f && charged[idx] > SpectrumAnalyzer::kFloorDb + 10.0f)
                    ++separated;
            }

            expect(separated > SpectrumAnalyzer::kNumPoints / 2,
                   "the hold should stand well clear of the decayed live trace; only "
                       + juce::String(separated) + " of "
                       + juce::String(SpectrumAnalyzer::kNumPoints) + " points did");

            EqGraphView view;
            view.setSpectra(live, live);   // dry == wet: this render is about the hold alone
            view.setPeaks(held);

            writePng(renderComponent(view, 900, 420), dir.getChildFile("peak-hold.png"));
        }

        beginTest("the full graph paints without falling over, and is written out to look at");
        {
            EqGraphView view;

            const auto dry  = syntheticSpectrum(6.0f,  128, -6.0f);
            const auto wet  = syntheticSpectrum(14.0f, 128, -2.0f);
            auto       peak = wet;

            for (size_t i = 0; i < peak.size(); ++i)
                peak[i] = juce::jmin(0.0f, wet[i] + 4.0f);   // a hold sitting above the live trace

            std::vector<float> delta(wet.size()), confidence(wet.size(), 1.0f);

            for (size_t i = 0; i < wet.size(); ++i)
                delta[i] = wet[i] - dry[i];

            // A stretch too quiet to measure: the trace must break here rather than draw a
            // flat line across it.
            for (size_t i = 200; i < confidence.size(); ++i)
                confidence[i] = 0.0f;

            std::vector<EqCurveModel::Band> bands {
                { 110.0f, 4.0f }, { 1000.0f, -3.0f }, { 8000.0f, 6.0f }
            };

            view.setSpectra(dry, wet);
            view.setPeaks(peak);
            view.setDeltaCurve(delta, confidence);
            view.setEqCurve(EqCurveModel::computeCurveDb(bands,
                                                         SpectrumAnalyzer::kNumPoints,
                                                         SpectrumAnalyzer::kMinFreqHz,
                                                         SpectrumAnalyzer::kMaxFreqHz),
                            bands);

            const auto image = renderComponent(view, 900, 420);

            // Something must actually be on the canvas: an all-transparent image would sail
            // through every assertion above.
            int opaque = 0;
            for (int y = 0; y < image.getHeight(); y += 4)
                for (int x = 0; x < image.getWidth(); x += 4)
                    if (image.getPixelAt(x, y).getAlpha() > 200)
                        ++opaque;

            expect(opaque > 1000, "the graph rendered almost nothing: " + juce::String(opaque)
                                      + " opaque samples");

            writePng(image, dir.getChildFile("eq-graph.png"));
            logMessage("wrote render output to " + dir.getFullPathName());
        }
    }

    // Renders a hard-panned sine into the goniometer and returns the normalised covariance
    // of the dot cloud about the scope centre: +1 means the cloud lies on the "\" diagonal
    // (up-left / down-right, which is LEFT channel), -1 means "/" (RIGHT channel).
    float diagonalLean(bool pannedLeft, const juce::File& pngOut)
    {
        GoniometerView gonio;

        constexpr int frames = 512;
        std::vector<float> audio(2 * frames, 0.0f);

        for (int i = 0; i < frames; ++i)
        {
            const auto s = static_cast<float>(
                0.8 * std::sin(juce::MathConstants<double>::twoPi * i / 64.0));

            audio[static_cast<size_t>(2 * i)]     = pannedLeft ? s : 0.0f;
            audio[static_cast<size_t>(2 * i + 1)] = pannedLeft ? 0.0f : s;
        }

        gonio.setFrames(nullptr, 0, audio.data(), frames);

        constexpr int size = 220;
        const auto image = renderComponent(gonio, size, size);

        writePng(image, pngOut);

        // Mirror GoniometerView::paint's layout so the scan covers the scope and nothing
        // else. The correlation marker at the bottom is painted in the SAME accent colour
        // as the dots, and including it drags the measurement toward the centre — which is
        // what made the first version of this test fail against correct code.
        auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float) size, (float) size);
        bounds.removeFromTop(18.0f);       // title
        bounds.removeFromBottom(22.0f);    // correlation meter
        const auto scope = bounds.reduced(8.0f);

        const auto wet = juce::Colour(FxTheme::getColor(FxColor::EqStart));

        const double centreX = scope.getCentreX();
        const double centreY = scope.getCentreY();

        double sumXY = 0.0, sumXX = 0.0, sumYY = 0.0;
        int    hits = 0;

        for (int y = (int) scope.getY(); y < (int) scope.getBottom(); ++y)
        {
            for (int x = (int) scope.getX(); x < (int) scope.getRight(); ++x)
            {
                const auto p = image.getPixelAt(x, y);

                if (p.getAlpha() < 128)
                    continue;

                // The dots are drawn at 0.85 alpha over a dark widget background, so match
                // on hue and saturation rather than on an exact RGB equality.
                const bool looksLikeWet = std::abs(p.getHue() - wet.getHue()) < 0.05f
                                       && p.getSaturation() > 0.35f
                                       && p.getBrightness() > 0.35f;

                if (! looksLikeWet)
                    continue;

                const double dx = x - centreX;
                const double dy = y - centreY;

                sumXY += dx * dy;
                sumXX += dx * dx;
                sumYY += dy * dy;
                ++hits;
            }
        }

        expect(hits > 50, "expected a visible dot cloud, found " + juce::String(hits)
                              + " matching pixels in " + pngOut.getFileName());

        const double denom = std::sqrt(sumXX * sumYY);

        return denom < 1.0e-9 ? 0.0f : static_cast<float>(sumXY / denom);
    }

    void writePng(const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();

        if (auto stream = file.createOutputStream())
        {
            juce::PNGImageFormat png;
            expect(png.writeImageToStream(image, *stream), "failed to write " + file.getFullPathName());
        }
        else
        {
            expect(false, "could not open " + file.getFullPathName());
        }
    }
};

static VisualizerRenderTest visualizerRenderTest;
