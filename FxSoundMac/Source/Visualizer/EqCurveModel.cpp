#include "EqCurveModel.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kMinOctaveSpacing = 0.25f;
constexpr float kMaxOctaveSpacing = 4.0f;

// Magnitude of the analogue peaking filter
//   H(s) = (s^2 + s*(A/Q)*w0 + w0^2) / (s^2 + s/(A*Q)*w0 + w0^2)
// evaluated at frequency f. Sample-rate independent, and exactly `gainDb` at f == f0.
float peakingMagnitudeDb(float f, float f0, float gainDb, float q)
{
    if (std::abs(gainDb) < 1.0e-4f)
        return 0.0f;

    const float a  = std::pow(10.0f, gainDb / 40.0f);
    const float x  = f / f0;
    const float x2 = x * x;

    const float common = (1.0f - x2) * (1.0f - x2);

    const float numer = std::sqrt(common + (a * x / q) * (a * x / q));
    const float denom = std::sqrt(common + (x / (a * q)) * (x / (a * q)));

    if (denom <= 0.0f)
        return 0.0f;

    return 20.0f * std::log10(numer / denom);
}
} // namespace

float EqCurveModel::inferQ(const std::vector<Band>& bands, int bandIndex)
{
    const int n = static_cast<int>(bands.size());

    if (n <= 0 || bandIndex < 0 || bandIndex >= n)
        return 1.41f;

    const float f0 = bands[static_cast<size_t>(bandIndex)].frequencyHz;
    if (f0 <= 0.0f)
        return 1.41f;

    float spacingOctaves = 1.0f;   // a lone band assumes one octave

    if (n > 1)
    {
        float nearest = 0.0f;

        if (bandIndex > 0)
        {
            const float fPrev = bands[static_cast<size_t>(bandIndex - 1)].frequencyHz;
            if (fPrev > 0.0f)
                nearest = std::abs(std::log2(f0 / fPrev));
        }

        if (bandIndex < n - 1)
        {
            const float fNext = bands[static_cast<size_t>(bandIndex + 1)].frequencyHz;
            if (fNext > 0.0f)
            {
                const float d = std::abs(std::log2(fNext / f0));
                nearest = nearest > 0.0f ? std::min(nearest, d) : d;
            }
        }

        if (nearest > 0.0f)
            spacingOctaves = nearest;
    }

    spacingOctaves = std::clamp(spacingOctaves, kMinOctaveSpacing, kMaxOctaveSpacing);

    const float twoPowBw = std::pow(2.0f, spacingOctaves);
    return std::sqrt(twoPowBw) / (twoPowBw - 1.0f);
}

std::vector<float> EqCurveModel::computeCurveDb(const std::vector<Band>& bands,
                                                int numPoints,
                                                float minFreqHz,
                                                float maxFreqHz)
{
    std::vector<float> curve(static_cast<size_t>(std::max(0, numPoints)), 0.0f);

    if (numPoints <= 0 || bands.empty() || minFreqHz <= 0.0f || maxFreqHz <= minFreqHz)
        return curve;

    std::vector<float> qs(bands.size());
    for (int b = 0; b < static_cast<int>(bands.size()); ++b)
        qs[static_cast<size_t>(b)] = inferQ(bands, b);

    for (int i = 0; i < numPoints; ++i)
    {
        const float t = numPoints > 1 ? static_cast<float>(i) / static_cast<float>(numPoints - 1)
                                      : 0.0f;
        const float f = minFreqHz * std::pow(maxFreqHz / minFreqHz, t);

        float sumDb = 0.0f;

        for (size_t b = 0; b < bands.size(); ++b)
        {
            const auto& band = bands[b];
            if (band.frequencyHz > 0.0f)
                sumDb += peakingMagnitudeDb(f, band.frequencyHz, band.gainDb, qs[b]);
        }

        curve[static_cast<size_t>(i)] = sumDb;
    }

    return curve;
}
