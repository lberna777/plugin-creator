#pragma once
// Loudness EBU R128 / ITU-R BS.1770-4: momentary (400 ms), short-term (3 s), integrated con
// gating assoluto (-70 LUFS) e relativo (-10 LU). Memoria FISSA: l'integrated usa un istogramma
// a 0.02 LU che conserva la somma esatta delle potenze per bin, quindi non cresce con la durata
// e il valore resta esatto (l'arrotondamento tocca solo quali blocchi stanno sul bordo del gate).
// Nessuna allocazione dopo prepare(). Header senza JUCE.
#include "KWeighting.h"
#include <array>
#include <cmath>
#include <limits>

namespace mc
{
inline double powerToLufs (double power) noexcept
{
    return power > 0.0 ? -0.691 + 10.0 * std::log10 (power) : -std::numeric_limits<double>::infinity();
}
inline double lufsToPower (double lufs) noexcept { return std::pow (10.0, (lufs + 0.691) / 10.0); }

class LoudnessMeter
{
public:
    static constexpr int maxChannels = 2;
    static constexpr double absGate = -70.0;

    void prepare (double sampleRate, int numChannels)
    {
        channels = numChannels < 1 ? 1 : (numChannels > maxChannels ? maxChannels : numChannels);
        for (auto& k : filters) k.prepare (sampleRate);
        subBlockLength = (int) std::lround (sampleRate * 0.1);   // 100 ms
        reset();
    }

    void reset() noexcept
    {
        for (auto& k : filters) k.reset();
        subBlocks.fill (0.0);
        subBlocksWritten = 0;
        acc = 0.0;
        accCount = 0;
        histPower.fill (0.0);
        histCount.fill (0);
        momentary = shortTerm = -std::numeric_limits<double>::infinity();
        maxMomentary = maxShortTerm = -std::numeric_limits<double>::infinity();
    }

    // Campioni in formato planare: data[ch][i]. numChannels deve corrispondere a prepare().
    void process (const float* const* data, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            double sum = 0.0;
            for (int ch = 0; ch < channels; ++ch)
            {
                const double y = filters[(size_t) ch].process ((double) data[ch][i]);
                sum += y * y;   // peso 1.0 per L, R e mono (BS.1770, canali frontali)
            }
            acc += sum;
            if (++accCount == subBlockLength)
                finishSubBlock();
        }
    }

    double getMomentary() const noexcept  { return momentary; }
    double getShortTerm() const noexcept  { return shortTerm; }
    double getMaxMomentary() const noexcept { return maxMomentary; }
    double getMaxShortTerm() const noexcept { return maxShortTerm; }

    // Secondi di audio sopra il gate assoluto contati nell'integrated (blocchi da 400 ms ogni 100 ms).
    double getGatedSeconds() const noexcept
    {
        long long n = 0;
        for (auto c : histCount) n += c;
        return (double) n * 0.1;
    }

    double getIntegrated() const noexcept
    {
        // 1) gate assoluto: tutti i blocchi nell'istogramma sono già sopra -70 LUFS
        double sumP = 0.0;
        long long n = 0;
        for (size_t b = 0; b < numBins; ++b) { sumP += histPower[b]; n += histCount[b]; }
        if (n == 0) return -std::numeric_limits<double>::infinity();

        // 2) gate relativo: -10 LU sotto la media dei blocchi sopra il gate assoluto
        const double relGate = powerToLufs (sumP / (double) n) - 10.0;
        const int firstBin = binFor (relGate);
        sumP = 0.0;
        n = 0;
        for (size_t b = (size_t) (firstBin < 0 ? 0 : firstBin); b < numBins; ++b)
        {
            sumP += histPower[b];
            n += histCount[b];
        }
        return n > 0 ? powerToLufs (sumP / (double) n) : -std::numeric_limits<double>::infinity();
    }

private:
    static constexpr double binWidth = 0.02;
    static constexpr double histMin = absGate;
    static constexpr double histMax = 10.0;
    static constexpr size_t numBins = (size_t) ((histMax - histMin) / binWidth);

    static int binFor (double lufs) noexcept
    {
        // bin il cui limite inferiore è >= lufs (gate "maggiore di")
        return (int) std::ceil ((lufs - histMin) / binWidth);
    }

    void finishSubBlock() noexcept
    {
        const double meanSquare = acc / (double) subBlockLength;
        acc = 0.0;
        accCount = 0;

        subBlocks[(size_t) (subBlocksWritten % 30)] = meanSquare;
        ++subBlocksWritten;

        if (subBlocksWritten >= 4)
        {
            double p = 0.0;
            for (long long k = subBlocksWritten - 4; k < subBlocksWritten; ++k) p += subBlocks[(size_t) (k % 30)];
            p *= 0.25;
            momentary = powerToLufs (p);
            if (momentary > maxMomentary) maxMomentary = momentary;

            if (momentary > absGate)
            {
                int b = (int) std::floor ((momentary - histMin) / binWidth);
                if (b < 0) b = 0;
                if (b >= (int) numBins) b = (int) numBins - 1;
                histPower[(size_t) b] += p;
                ++histCount[(size_t) b];
            }
        }
        if (subBlocksWritten >= 30)
        {
            double p = 0.0;
            for (auto v : subBlocks) p += v;
            shortTerm = powerToLufs (p / 30.0);
            if (shortTerm > maxShortTerm) maxShortTerm = shortTerm;
        }
    }

    int channels = 2;
    std::array<KWeighting, maxChannels> filters;
    int subBlockLength = 4800;
    std::array<double, 30> subBlocks {};
    long long subBlocksWritten = 0;
    double acc = 0.0;
    int accCount = 0;
    std::array<double, numBins> histPower {};
    std::array<long long, numBins> histCount {};
    double momentary = 0, shortTerm = 0, maxMomentary = 0, maxShortTerm = 0;
};
} // namespace mc
