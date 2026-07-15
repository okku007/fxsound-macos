#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>
#include "EqCurveModel.h"

namespace {
constexpr int   kPoints  = 256;
constexpr float kMinFreq = 20.0f;
constexpr float kMaxFreq = 20000.0f;

float freqForPoint(int i)
{
    const float t = static_cast<float>(i) / static_cast<float>(kPoints - 1);
    return kMinFreq * std::pow(kMaxFreq / kMinFreq, t);
}

// A plausible 10-band layout, geometrically spaced like the real EQ.
std::vector<EqCurveModel::Band> tenFlatBands()
{
    return { { 30.0f, 0.0f },   { 60.0f, 0.0f },   { 120.0f, 0.0f },
             { 250.0f, 0.0f },  { 500.0f, 0.0f },  { 1000.0f, 0.0f },
             { 2000.0f, 0.0f }, { 4000.0f, 0.0f }, { 8000.0f, 0.0f },
             { 16000.0f, 0.0f } };
}
} // namespace

struct EqCurveModelTest : juce::UnitTest
{
    EqCurveModelTest() : juce::UnitTest("EqCurveModel") {}

    void runTest() override
    {
        beginTest("all bands flat produces a flat curve");
        {
            const auto curve = EqCurveModel::computeCurveDb(tenFlatBands(), kPoints, kMinFreq, kMaxFreq);
            expectEquals(static_cast<int>(curve.size()), kPoints);

            for (const auto v : curve)
                expectWithinAbsoluteError(v, 0.0f, 0.001f);
        }

        beginTest("no bands produces a flat curve rather than a crash");
        {
            const auto curve = EqCurveModel::computeCurveDb({}, kPoints, kMinFreq, kMaxFreq);
            expectEquals(static_cast<int>(curve.size()), kPoints);
            for (const auto v : curve)
                expectWithinAbsoluteError(v, 0.0f, 0.001f);
        }

        beginTest("a boosted band peaks at its own frequency, at its own gain");
        {
            auto bands = tenFlatBands();
            bands[5].gainDb = 12.0f;             // the 1 kHz band

            const auto curve = EqCurveModel::computeCurveDb(bands, kPoints, kMinFreq, kMaxFreq);

            int peak = 0;
            for (int i = 1; i < kPoints; ++i)
                if (curve[static_cast<size_t>(i)] > curve[static_cast<size_t>(peak)])
                    peak = i;

            const float peakFreq = freqForPoint(peak);
            expect(peakFreq > 1000.0f / 1.12f && peakFreq < 1000.0f * 1.12f,
                   "expected the peak near 1 kHz, got " + juce::String(peakFreq));

            // A peaking filter hits exactly its gain at its centre frequency.
            expectWithinAbsoluteError(curve[static_cast<size_t>(peak)], 12.0f, 0.6f);
        }

        beginTest("a boosted band leaves distant frequencies alone");
        {
            auto bands = tenFlatBands();
            bands[5].gainDb = 12.0f;             // 1 kHz

            const auto curve = EqCurveModel::computeCurveDb(bands, kPoints, kMinFreq, kMaxFreq);

            // At 20 Hz - more than five octaves away - the boost must have died out.
            expectWithinAbsoluteError(curve[0], 0.0f, 1.0f);
        }

        beginTest("a cut band dips below zero");
        {
            auto bands = tenFlatBands();
            bands[2].gainDb = -9.0f;             // the 120 Hz band

            const auto curve = EqCurveModel::computeCurveDb(bands, kPoints, kMinFreq, kMaxFreq);

            float lowest = 0.0f;
            for (const auto v : curve)
                lowest = juce::jmin(lowest, v);

            expectWithinAbsoluteError(lowest, -9.0f, 0.6f);
        }

        beginTest("adjacent boosts sum");
        {
            auto bands = tenFlatBands();
            bands[5].gainDb = 6.0f;              // 1 kHz
            bands[6].gainDb = 6.0f;              // 2 kHz

            const auto curve = EqCurveModel::computeCurveDb(bands, kPoints, kMinFreq, kMaxFreq);

            float highest = 0.0f;
            for (const auto v : curve)
                highest = juce::jmax(highest, v);

            // Between two 6 dB peaks an octave apart, the overlap pushes the total
            // above either one alone.
            expect(highest > 6.0f, "expected overlapping bands to sum, got " + juce::String(highest));
        }

        beginTest("Q is inferred from band spacing");
        {
            const auto bands = tenFlatBands();

            // Octave-spaced bands give a bandwidth of ~1 octave, which is Q ~= 1.41.
            const float q = EqCurveModel::inferQ(bands, 5);
            expect(q > 1.0f && q < 2.0f, "expected Q near 1.41, got " + juce::String(q));

            // Edge bands have only one neighbour but must still return something sane.
            expect(EqCurveModel::inferQ(bands, 0) > 0.1f);
            expect(EqCurveModel::inferQ(bands, static_cast<int>(bands.size()) - 1) > 0.1f);

            // A single lonely band must not divide by zero.
            expect(EqCurveModel::inferQ({ { 1000.0f, 0.0f } }, 0) > 0.1f);
        }
    }
};

static EqCurveModelTest eqCurveModelTest;
