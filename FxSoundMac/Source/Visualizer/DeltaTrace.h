#pragma once
#include <vector>

// The MEASURED transfer curve: wet-minus-dry, in dB, per display point.
//
// Unlike EqCurveModel this assumes nothing about the DSP's internals - it is simply
// what came out minus what went in. It therefore reflects the ENTIRE processing chain
// (EQ + effects + maximizer + output gain), not the EQ alone, and it is only
// comparable with the ideal EQ curve when the effects are at zero.
//
// Where the dry signal is below kGateDb there is no energy to compare, so the ratio is
// meaningless. Those points keep their last value and report zero confidence; the view
// fades them out rather than drawing noise.
class DeltaTrace
{
public:
    static constexpr float kGateDb = -80.0f;

    DeltaTrace();

    // Both vectors must be SpectrumAnalyzer::kNumPoints long; mismatched input is ignored.
    void update(const std::vector<float>& dryDb, const std::vector<float>& wetDb);

    const std::vector<float>& getDeltaDb()     const noexcept { return deltaDb_; }
    const std::vector<float>& getConfidence()  const noexcept { return confidence_; }

private:
    std::vector<float> deltaDb_;
    std::vector<float> confidence_;
};
