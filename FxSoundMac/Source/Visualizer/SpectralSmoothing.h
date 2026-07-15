#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// Spectral smoothing for the visualizer's loudness-axis traces (IN, OUT, peak hold).
//
// A symmetric moving average across neighbouring display points. The points are log-spaced
// (SpectrumAnalyzer::frequencyForPoint), so a fixed window in POINTS is a fixed window in
// OCTAVES - which is exactly what "1/3-octave smoothing" means on a hardware analyser. At
// ~25.6 points/octave a halfWidth of 4 is about 1/3 octave.
//
// Averaged in the POWER domain, not in dB: each point is converted to linear power
// (10^(dB/10)), the powers are averaged, and the mean is converted back to dB. A flat-weight
// moving average of power conserves the total energy in the window, so the smoothed trace sits
// at the true average LEVEL rather than the geometric-mean level - averaging dB directly gives
// the geometric mean of power, which a single quiet neighbour drags far down and which reads as
// the audio suddenly getting quieter. Power averaging keeps the body of the curve where the raw
// trace is and only rounds off the single-bin fizz. (This is why the level no longer collapses
// when smoothing is switched on.)
//
// The window is clamped at the array's ends - it averages only the neighbours that exist, never
// wrapping and never zero-padding, so the first and last points are not biased. Accumulated in
// double because the input spans a wide dynamic range (a -90 dB floor beside a 0 dB peak is a
// billion to one in power). A halfWidth of 0 (or an empty input) is a no-op.
inline std::vector<float> smoothDb(const std::vector<float>& db, int halfWidth)
{
    const int n = static_cast<int>(db.size());

    if (halfWidth <= 0 || n == 0)
        return db;

    std::vector<float> out(static_cast<size_t>(n));

    for (int i = 0; i < n; ++i)
    {
        const int lo = std::max(0, i - halfWidth);
        const int hi = std::min(n - 1, i + halfWidth);

        double power = 0.0;
        for (int j = lo; j <= hi; ++j)
            power += std::pow(10.0, static_cast<double>(db[static_cast<size_t>(j)]) / 10.0);

        power /= static_cast<double>(hi - lo + 1);
        out[static_cast<size_t>(i)] = static_cast<float>(10.0 * std::log10(power));
    }

    return out;
}
