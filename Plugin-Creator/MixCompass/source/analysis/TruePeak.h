#pragma once
// True peak (BS.1770-4, Annex 2): sovracampionamento 4x con FIR polifase (sinc finestrata Kaiser,
// 24 tap per fase) e massimo del valore assoluto. Il filtro si progetta in prepare(), non a ogni
// campione. Nessuna allocazione dopo prepare(). Header senza JUCE.
#include <array>
#include <cmath>

namespace mc
{
class TruePeakMeter
{
public:
    static constexpr int factor = 4;
    static constexpr int tapsPerPhase = 24;
    static constexpr int numTaps = factor * tapsPerPhase;
    static constexpr int maxChannels = 2;

    TruePeakMeter() { design(); }

    void prepare (int numChannels) noexcept
    {
        channels = numChannels < 1 ? 1 : (numChannels > maxChannels ? maxChannels : numChannels);
        reset();
    }

    void reset() noexcept
    {
        for (auto& h : history) h.fill (0.0f);
        pos = 0;
        peak = 0.0f;
        samplePeak = 0.0f;
    }

    void process (const float* const* data, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                const float x = data[ch][i];
                auto& h = history[(size_t) ch];
                h[(size_t) pos] = x;

                const float ax = std::abs (x);
                if (ax > samplePeak) samplePeak = ax;

                for (int p = 0; p < factor; ++p)
                {
                    float y = 0.0f;
                    int idx = pos;
                    for (int k = 0; k < tapsPerPhase; ++k)
                    {
                        y += coeffs[(size_t) p][(size_t) k] * h[(size_t) idx];
                        idx = idx == 0 ? tapsPerPhase - 1 : idx - 1;
                    }
                    const float ay = std::abs (y);
                    if (ay > peak) peak = ay;
                }
            }
            pos = (pos + 1) % tapsPerPhase;
        }
    }

    // Valore lineare (1.0 = 0 dBTP). Il true peak non è mai sotto il sample peak.
    float getTruePeak() const noexcept   { return peak > samplePeak ? peak : samplePeak; }
    float getSamplePeak() const noexcept { return samplePeak; }

    static double toDb (float linear) noexcept
    {
        return linear > 0.0f ? 20.0 * std::log10 ((double) linear) : -std::numeric_limits<double>::infinity();
    }

private:
    static double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    void design() noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr double beta = 8.0;
        // taglio a 0.5 della Nyquist originale *0.94: banda passante fino a ~0.47 fs originale
        const double fc = 0.5 / factor * 0.94;
        const double centre = (numTaps - 1) * 0.5;
        const double i0b = besselI0 (beta);

        std::array<double, numTaps> h {};
        for (int n = 0; n < numTaps; ++n)
        {
            const double t = n - centre;
            const double sinc = t == 0.0 ? 2.0 * fc : std::sin (2.0 * pi * fc * t) / (pi * t);
            const double r = t / centre;
            const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
            h[(size_t) n] = sinc * w;
        }
        // ogni fase ha guadagno DC = 1 (normalizzazione per fase: niente ondulazione a DC)
        for (int p = 0; p < factor; ++p)
        {
            double s = 0.0;
            for (int k = 0; k < tapsPerPhase; ++k) s += h[(size_t) (p + factor * k)];
            for (int k = 0; k < tapsPerPhase; ++k)
                coeffs[(size_t) p][(size_t) k] = (float) (h[(size_t) (p + factor * k)] / s);
        }
    }

    int channels = 2;
    std::array<std::array<float, tapsPerPhase>, factor> coeffs {};
    std::array<std::array<float, tapsPerPhase>, maxChannels> history {};
    int pos = 0;
    float peak = 0.0f, samplePeak = 0.0f;
};
} // namespace mc
