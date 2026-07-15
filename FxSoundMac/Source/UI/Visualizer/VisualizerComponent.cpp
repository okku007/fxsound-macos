#include "VisualizerComponent.h"
#include "FxTheme.h"
#include "EqCurveModel.h"

VisualizerComponent::VisualizerComponent(FxController& controller)
    : controller_(controller)
{
    dryScratch_.assign(static_cast<size_t>(2 * kMaxDrainFrames), 0.0f);
    wetScratch_.assign(static_cast<size_t>(2 * kMaxDrainFrames), 0.0f);

    // Clicking the graph clears the peak hold. Both analyzers are reset, not just the wet
    // one whose peaks are drawn: leaving the dry peaks stale would make them wrong the
    // moment the trace is ever shown.
    graph_.onResetPeaks = [this]
    {
        dryAnalyzer_.resetPeaks();
        wetAnalyzer_.resetPeaks();
    };

    addAndMakeVisible(graph_);
    addAndMakeVisible(goniometer_);
    addAndMakeVisible(meters_);

    controller_.getVisualizerTap().setActive(true);
    startTimerHz(kFrameRateHz);
}

VisualizerComponent::~VisualizerComponent()
{
    stopTimer();
    controller_.getVisualizerTap().setActive(false);
}

void VisualizerComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(FXCOLOR(WindowBackground)).withAlpha(1.0f));
}

void VisualizerComponent::resized()
{
    auto area = getLocalBounds().reduced(12);

    auto bottom = area.removeFromBottom(juce::jmin(200, area.getHeight() / 3));
    area.removeFromBottom(8);

    goniometer_.setBounds(bottom.removeFromLeft(bottom.getHeight()));
    bottom.removeFromLeft(8);
    meters_.setBounds(bottom);

    graph_.setBounds(area);
}

int VisualizerComponent::drain(bool wet, std::vector<float>& scratch)
{
    auto& tap = controller_.getVisualizerTap();

    const int frames = wet ? tap.readWet(scratch.data(), kMaxDrainFrames)
                           : tap.readDry(scratch.data(), kMaxDrainFrames);

    if (frames > 0)
    {
        auto& analyzer = wet ? wetAnalyzer_ : dryAnalyzer_;
        analyzer.pushInterleavedStereo(scratch.data(), frames);
    }

    return frames;
}

void VisualizerComponent::timerCallback()
{
    // Follow the device: a sample-rate change re-derives the bin->frequency map.
    const int sr = controller_.getSampleRate();
    if (sr != lastSampleRate_)
    {
        lastSampleRate_ = sr;
        dryAnalyzer_.setSampleRate(static_cast<double>(sr));
        wetAnalyzer_.setSampleRate(static_cast<double>(sr));
    }

    lastDryFrames_ = drain(false, dryScratch_);
    lastWetFrames_ = drain(true,  wetScratch_);

    // Always update, even with no new samples: that is what makes the display decay
    // to the floor when the audio stops instead of freezing on the last frame.
    dryAnalyzer_.update();
    wetAnalyzer_.update();

    graph_.setSpectra(dryAnalyzer_.getMagnitudesDb(), wetAnalyzer_.getMagnitudesDb());
    graph_.setPeaks(wetAnalyzer_.getPeakDb());

    // Measured wet-minus-dry: what the DSP actually did, across the whole chain.
    delta_.update(dryAnalyzer_.getMagnitudesDb(), wetAnalyzer_.getMagnitudesDb());
    graph_.setDeltaCurve(delta_.getDeltaDb(), delta_.getConfidence());

    // Rebuild the ideal curve from the DSP's current band settings. Read-only.
    const int numBands = controller_.getNumEqBands();

    std::vector<EqCurveModel::Band> bands;
    bands.reserve(static_cast<size_t>(juce::jmax(0, numBands)));

    for (int b = 0; b < numBands; ++b)
        bands.push_back({ controller_.getEqBandFrequency(b), controller_.getEqBand(b) });

    graph_.setEqCurve(EqCurveModel::computeCurveDb(bands,
                                                   SpectrumAnalyzer::kNumPoints,
                                                   SpectrumAnalyzer::kMinFreqHz,
                                                   SpectrumAnalyzer::kMaxFreqHz),
                      bands);

    goniometer_.setFrames(dryScratch_.data(), lastDryFrames_,
                          wetScratch_.data(), lastWetFrames_);

    meters_.setFrames(dryScratch_.data(), lastDryFrames_,
                      wetScratch_.data(), lastWetFrames_);
}
