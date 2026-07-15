#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <vector>
#include "EqCurveModel.h"
#include "DeltaTrace.h"

// The Logic-style plot: log-frequency X axis, dB Y axis, dry spectrum as a faint
// outline behind the filled wet spectrum.
//
// Read-only. It renders what it is given and never touches the DSP.
class EqGraphView : public juce::Component
{
public:
    EqGraphView();

    // The traces this view can draw. The enum value doubles as the index into the
    // visibility flags AND as the legend's row order, so the legend physically cannot
    // disagree with what is on the graph.
    enum Trace { TraceIn = 0, TraceOut, TracePeakHold, TraceEqCurve, TraceMeasured };

    static constexpr int kNumTraces = 5;

    // Every trace starts visible. Deliberately not persisted: this is a debug view, and the
    // app has no settings file at all — five booleans do not justify inventing one.
    bool isTraceVisible(Trace t) const noexcept { return traceVisible_[static_cast<size_t>(t)]; }
    void setTraceVisible(Trace t, bool visible);

    // Both vectors are SpectrumAnalyzer::kNumPoints magnitudes in dBFS.
    void setSpectra(const std::vector<float>& dryDb, const std::vector<float>& wetDb);

    // The wet peak-hold trace: the loudest each point has reached, sinking back slowly.
    void setPeaks(const std::vector<float>& peakDb);

    // Clicking the plot clears the peak trace. The owner wires this to the analyzers,
    // because the view holds no analyzer of its own.
    std::function<void()> onResetPeaks;

    // curveDb holds SpectrumAnalyzer::kNumPoints gain values in dB; bands are drawn
    // as static markers on it. Display only — dragging is deliberately not supported,
    // because the visualizer must never write DSP state.
    void setEqCurve(const std::vector<float>& curveDb,
                    const std::vector<EqCurveModel::Band>& bands);

    // The measured wet-minus-dry curve. confidence (0..1 per point) fades out the
    // stretches where the input was too quiet to measure.
    void setDeltaCurve(const std::vector<float>& deltaDb, const std::vector<float>& confidence);

    void paint(juce::Graphics& g) override;

    // Maps a dB series onto a smooth open polyline through the plot. Pure function of its
    // arguments; public so the render test can assert the curve never overshoots the data
    // it was built from, which is the one way this smoothing could lie about the audio.
    juce::Path buildSpectrumPath(const std::vector<float>& db,
                                 juce::Rectangle<float> plot) const;

    // The plot rectangle, inset for the axis labels.
    juce::Rectangle<float> plotArea() const;

    // Legend geometry. Public because the painter and the click hit-test MUST agree about
    // where each row is, and the only way to prove that is to test one against the other.
    // Neither takes a `plot` argument: both derive the plot internally via plotArea(),
    // matching plotArea()'s own no-argument shape rather than making every caller thread a
    // rectangle through that could drift from what plotArea() actually returns.
    juce::Rectangle<float> legendRowBounds(int index) const;
    juce::Rectangle<float> legendPanelBounds() const;

    // The legend's own header: the one row that is always there, collapsed or not, so it is
    // the anchor every other legend rectangle is measured from (see legendRowBounds and
    // legendPanelBounds) rather than each recomputing plotArea()'s row-0 offset separately.
    // Not gated on legendCollapsed_ - if it moved when the panel collapsed, the second click
    // meant to reopen it would land somewhere else.
    juce::Rectangle<float> legendHandleBounds() const;

    // Which legend row is under this point? -1 when the point is outside the panel, or when
    // the legend is collapsed (the rows are not on screen, so nothing can hit-test to them).
    int legendRowAt(juce::Point<float> p) const;

    // Not persisted, same as traceVisible_: this is a debug view with no settings file, and
    // whether the legend happened to be tucked away last time is not worth inventing one for.
    bool isLegendCollapsed() const noexcept { return legendCollapsed_; }
    void setLegendCollapsed(bool collapsed);

    // Spectral smoothing: a display-only 1/3-octave moving average over the loudness-axis
    // traces (IN, OUT, peak hold). Off by default, so the view opens on exactly today's raw,
    // peak-picked spectrum; on, the same series are averaged across frequency on the way to
    // the screen. It never reaches the analyzer, so the audio measurement is untouched either
    // way - only what is drawn changes. Not persisted, same reasoning as the flags above.
    bool isSmoothingEnabled() const noexcept { return smoothingEnabled_; }
    void setSmoothingEnabled(bool enabled);

    // The 'smooth' chip: a clickable toggle sharing the legend handle's row, at its right end.
    // Public because the painter and the click hit-test MUST agree where it is, same contract
    // as the legend rectangles. It sits inside legendHandleBounds(), so mouseDown checks it
    // FIRST - a click here toggles smoothing, not collapse.
    juce::Rectangle<float> smoothChipBounds() const;

    // The hover readout's rows for a display point: the frequency first (it belongs to the
    // cursor, not to any trace), then one row per visible trace that has a value to report.
    // Public so a test can assert WHICH row disappeared - the alternative, measuring the
    // height of the box on screen, would not.
    struct ReadoutRow
    {
        juce::Colour colour;
        juce::String text;
    };

    std::vector<ReadoutRow> readoutRowsAt(int pointIndex) const;

    // Hovering the plot reads out the exact values under the cursor. Without it the only
    // way to get a number off this graph is to eyeball it against the gridlines, which
    // defeats the point of a debug view.
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    // Maps a display-point index and a dB value into the plot rectangle.
    float xForPoint(int pointIndex, juce::Rectangle<float> plot) const;
    float yForDb(float db, juce::Rectangle<float> plot) const;
    float yForGainDb(float gainDb, juce::Rectangle<float> plot) const;

    // Inverse of xForPoint: which display point sits under this x? Clamped to range.
    int pointForX(float x, juce::Rectangle<float> plot) const;

    void drawGrid(juce::Graphics& g, juce::Rectangle<float> plot) const;

    // Split in two because the two halves paint at opposite ends of paint()'s draw order:
    // the backdrop goes in BEFORE the traces (it only needs to darken the wet fill under the
    // text, not sit on top of anything), and the foreground goes in AFTER them (the swatches
    // and text need to stay legible, which painting them under the traces would not achieve).
    // A single drawLegend() that did both in one call could only run at one point in that
    // order - which is exactly how the panel ended up erasing a trace before.
    void drawLegendBackdrop(juce::Graphics& g, juce::Rectangle<float> plot) const;
    void drawLegendForeground(juce::Graphics& g, juce::Rectangle<float> plot) const;

    void drawHoverReadout(juce::Graphics& g, juce::Rectangle<float> plot) const;

    // The series as it should be DRAWN: the raw values when smoothing is off, the 1/3-octave
    // moving average when it is on. One funnel for every consumer of a loudness-axis trace -
    // the drawn path, the hover dots, and the readout numbers all go through it, so the dot on
    // the curve and the number in the box can never describe different values than the line.
    std::vector<float> displaySpectrum(const std::vector<float>& raw) const;

    // The right-hand centred gain axis: its zero line and its scale. Shared by the EQ curve
    // and the measured trace, so it is drawn while EITHER is visible.
    void drawGainAxis(juce::Graphics& g, juce::Rectangle<float> plot) const;

    static constexpr float kTopDb    = 0.0f;
    static constexpr float kBottomDb = -90.0f;
    static constexpr float kMaxGainDb = 15.0f;   // a little headroom past the ±12 dB range

    // Gutter on the left for the dBFS scale. The spectra are the only traces on that
    // axis and it carried no labels at all before, so a line on the graph said nothing
    // about how loud it actually was.
    static constexpr float kLeftGutter = 32.0f;

    // Half-width of the smoothing window in display points. ~25.6 points/octave, so 4 is about
    // 1/3 octave - the classic analyser default: takes the fizz off without flattening a real
    // tone. A single on/off toggle was what was asked for, so this is a constant, not a slider.
    static constexpr int kSmoothHalfWidth = 4;

    std::vector<float> dryDb_, wetDb_, peakDb_;
    std::vector<float> eqCurveDb_;
    std::vector<EqCurveModel::Band> bands_;
    std::vector<float> deltaDb_, deltaConfidence_;

    // Every trace starts visible (see isTraceVisible's comment). An in-class initialiser
    // means there is no window between construction and the constructor body where this
    // array holds indeterminate values.
    std::array<bool, kNumTraces> traceVisible_ = { true, true, true, true, true };

    // Starts expanded, same reasoning as traceVisible_'s default: whatever was hidden last
    // session should not stay hidden the next time this debug view is opened.
    bool legendCollapsed_ = false;

    // Starts off, so the view opens on the raw peak-picked spectrum - today's exact display.
    // Smoothing is strictly opt-in; disabling it returns bit-for-bit to that default.
    bool smoothingEnabled_ = false;

    // -1 when the cursor is not over the plot.
    float hoverX_ = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqGraphView)
};
