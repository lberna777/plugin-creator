#pragma once
#include "../source/KWeighting.h"
#include <algorithm>
#include <cmath>
#include <vector>

/*  Loudness integrata ITU-R BS.1770-4 / EBU R128, offline, per i test.

    Blocchi da 400 ms con sovrapposizione del 75 %, gate assoluto a -70 LUFS e gate relativo
    a -10 LU. Vive in tools/: alloca liberamente, non entra mai nel plugin. La pesatura è la
    stessa del DSP (source/KWeighting.h), così "catena accesa ≈ spenta" si misura con lo
    stesso metro che il level match insegue.
*/
namespace vftest
{

/** Loudness integrata di un segnale a `numChannels` canali (puntatori per canale). */
inline double integratedLufs (const float* const* channels, int numChannels, int numSamples, double sampleRate)
{
    const int block = static_cast<int> (0.4 * sampleRate);
    const int hop   = block / 4;
    if (numSamples < block || numChannels <= 0) return -144.0;

    // potenza pesata K per campione, sommata sui canali (pesi 1.0: niente surround qui)
    std::vector<double> power (static_cast<size_t> (numSamples), 0.0);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        vf::kweighting::Filter filter;
        filter.prepare (sampleRate);
        for (int i = 0; i < numSamples; ++i)
        {
            const auto y = filter.process (channels[ch][i]);
            power[static_cast<size_t> (i)] += y * y;
        }
    }

    // somme prefisse: ogni blocco costa O(1)
    std::vector<double> prefix (power.size() + 1, 0.0);
    for (size_t i = 0; i < power.size(); ++i) prefix[i + 1] = prefix[i] + power[i];

    std::vector<double> blocks;
    for (int start = 0; start + block <= numSamples; start += hop)
        blocks.push_back ((prefix[static_cast<size_t> (start + block)] - prefix[static_cast<size_t> (start)]) / block);

    auto toLufs = [] (double meanSquare) { return vf::kweighting::kOffsetDb + 10.0 * std::log10 (std::max (meanSquare, 1.0e-20)); };

    auto gatedMean = [&] (double thresholdLufs)
    {
        double sum = 0.0; int count = 0;
        for (auto z : blocks)
            if (toLufs (z) > thresholdLufs) { sum += z; ++count; }
        return count > 0 ? sum / count : 0.0;
    };

    const double absolute = gatedMean (-70.0);
    if (absolute <= 0.0) return -144.0;
    const double relative = gatedMean (toLufs (absolute) - 10.0);
    return relative > 0.0 ? toLufs (relative) : -144.0;
}

} // namespace vftest
