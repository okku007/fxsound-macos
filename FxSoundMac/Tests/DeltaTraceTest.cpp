#include <juce_core/juce_core.h>
#include <vector>
#include "DeltaTrace.h"
#include "SpectrumAnalyzer.h"

namespace {
std::vector<float> filled(float value)
{
    return std::vector<float>(static_cast<size_t>(SpectrumAnalyzer::kNumPoints), value);
}

// Runs enough updates for the exponential smoothing to converge.
void settle(DeltaTrace& trace, const std::vector<float>& dry, const std::vector<float>& wet)
{
    for (int i = 0; i < 200; ++i)
        trace.update(dry, wet);
}
} // namespace

struct DeltaTraceTest : juce::UnitTest
{
    DeltaTraceTest() : juce::UnitTest("DeltaTrace") {}

    void runTest() override
    {
        beginTest("identical dry and wet give a flat zero trace");
        {
            DeltaTrace trace;
            const auto signal = filled(-20.0f);
            settle(trace, signal, signal);

            expectEquals(static_cast<int>(trace.getDeltaDb().size()), SpectrumAnalyzer::kNumPoints);

            for (const auto v : trace.getDeltaDb())
                expectWithinAbsoluteError(v, 0.0f, 0.1f);
        }

        beginTest("a uniform boost shows up as that boost");
        {
            DeltaTrace trace;
            settle(trace, filled(-30.0f), filled(-24.0f));

            for (const auto v : trace.getDeltaDb())
                expectWithinAbsoluteError(v, 6.0f, 0.1f);
        }

        beginTest("a uniform cut shows up as that cut");
        {
            DeltaTrace trace;
            settle(trace, filled(-20.0f), filled(-29.0f));

            for (const auto v : trace.getDeltaDb())
                expectWithinAbsoluteError(v, -9.0f, 0.1f);
        }

        beginTest("points below the gate report zero confidence");
        {
            DeltaTrace trace;

            // Dry sits at the analyzer floor: there is no signal to compare against, so
            // wet-minus-dry is meaningless and must not be drawn.
            settle(trace, filled(SpectrumAnalyzer::kFloorDb), filled(-40.0f));

            for (const auto c : trace.getConfidence())
                expectWithinAbsoluteError(c, 0.0f, 0.01f);
        }

        beginTest("points above the gate report full confidence");
        {
            DeltaTrace trace;
            settle(trace, filled(-20.0f), filled(-20.0f));

            for (const auto c : trace.getConfidence())
                expectWithinAbsoluteError(c, 1.0f, 0.01f);
        }

        beginTest("gated points hold their last value rather than jumping to zero");
        {
            DeltaTrace trace;

            // Establish a +6 dB reading with a healthy signal...
            settle(trace, filled(-30.0f), filled(-24.0f));
            const float established = trace.getDeltaDb()[0];
            expectWithinAbsoluteError(established, 6.0f, 0.1f);

            // ...then drop the input below the gate. The value must persist (confidence
            // is what fades the trace out), not snap to zero and imply "DSP did nothing".
            for (int i = 0; i < 5; ++i)
                trace.update(filled(SpectrumAnalyzer::kFloorDb), filled(SpectrumAnalyzer::kFloorDb));

            expectWithinAbsoluteError(trace.getDeltaDb()[0], established, 0.5f);
            expect(trace.getConfidence()[0] < 1.0f, "confidence must fall when the signal is gone");
        }

        beginTest("mismatched input sizes are ignored rather than crashing");
        {
            DeltaTrace trace;
            trace.update(filled(-20.0f), std::vector<float>(3, -20.0f));
            expectEquals(static_cast<int>(trace.getDeltaDb().size()), SpectrumAnalyzer::kNumPoints);
        }
    }
};

static DeltaTraceTest deltaTraceTest;
