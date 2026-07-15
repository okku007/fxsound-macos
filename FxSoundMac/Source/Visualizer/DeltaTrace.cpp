#include "DeltaTrace.h"
#include "SpectrumAnalyzer.h"
#include <algorithm>

namespace {
// Music is non-stationary, so a single frame of wet-minus-dry is mostly noise.
// Heavy smoothing is what turns it into a readable curve.
constexpr float kSmoothing = 0.1f;

// Confidence rises quickly when signal appears and falls quickly when it goes, so the
// trace fades rather than lingering over silence.
constexpr float kConfidenceRise = 0.25f;
constexpr float kConfidenceFall = 0.35f;
} // namespace

DeltaTrace::DeltaTrace()
{
    deltaDb_.assign(static_cast<size_t>(SpectrumAnalyzer::kNumPoints), 0.0f);
    confidence_.assign(static_cast<size_t>(SpectrumAnalyzer::kNumPoints), 0.0f);
}

void DeltaTrace::update(const std::vector<float>& dryDb, const std::vector<float>& wetDb)
{
    const size_t n = deltaDb_.size();

    if (dryDb.size() != n || wetDb.size() != n)
        return;

    for (size_t i = 0; i < n; ++i)
    {
        const bool  gated  = dryDb[i] <= DeltaTrace::kGateDb;
        const float target = gated ? 0.0f : 1.0f;
        const float coeff  = target > confidence_[i] ? kConfidenceRise : kConfidenceFall;

        confidence_[i] += coeff * (target - confidence_[i]);
        confidence_[i]  = std::clamp(confidence_[i], 0.0f, 1.0f);

        if (gated)
            continue;   // hold the last value; confidence is what fades it out

        // Subtracting dB IS the ratio: 20log10(wet/dry) = wetDb - dryDb.
        const float instant = wetDb[i] - dryDb[i];
        deltaDb_[i] += kSmoothing * (instant - deltaDb_[i]);
    }
}
