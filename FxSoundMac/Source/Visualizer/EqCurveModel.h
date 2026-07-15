#pragma once
#include <vector>

// The IDEAL EQ curve: the sum of one analogue peaking-filter response per band.
//
// This is a MODEL, not a measurement. The DSP exposes band frequencies and gains but
// no Q, so Q is inferred from how far apart the neighbouring bands sit. The curve
// therefore shows what the EQ was ASKED to do. What it actually DID is the measured
// trace (see DeltaTrace) - and the two disagreeing is exactly the interesting case.
class EqCurveModel
{
public:
    struct Band
    {
        float frequencyHz;
        float gainDb;
    };

    // numPoints gain values in dB, log-spaced from minFreqHz to maxFreqHz.
    // Bands with 0 dB gain contribute nothing, so a flat EQ yields a flat curve.
    static std::vector<float> computeCurveDb(const std::vector<Band>& bands,
                                             int numPoints,
                                             float minFreqHz,
                                             float maxFreqHz);

    // Q derived from the octave distance to the nearest neighbouring band:
    //   Q = sqrt(2^bw) / (2^bw - 1),  bw = bandwidth in octaves.
    // Edge bands use their single neighbour. A lone band assumes one octave.
    static float inferQ(const std::vector<Band>& bands, int bandIndex);
};
