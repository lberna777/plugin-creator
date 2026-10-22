#pragma once
// Spettro a 1/6 d'ottava (20 Hz – 20 kHz) con statistiche stereo per banda, calcolate dalla
// stessa FFT: potenza L, potenza R e cross-spettro Re(L·R*) → correlazione e rapporto Side/Mid.
//
// - Due canali reali in UNA FFT complessa (z = L + iR) e separazione per simmetria.
// - Finestra Hann, hop N/2; N scala con la sample rate (risoluzione ~5.4–5.9 Hz in tutti i casi).
// - Scala: la somma delle bande ≈ mean-square del segnale per canale (Parseval), quindi i livelli
//   di banda sono in dBFS veri e confrontabili fra due istanze alla stessa sample rate.
// - Media a lungo termine (da reset) + media "live" esponenziale (~1 s) per il disegno.
// - Gate: i frame sotto -60 dBFS (silenzio, pausa del trasporto) non entrano nella media.
// Tutta la memoria in prepare(); push() non alloca. Header senza JUCE.
#include "FFT.h"
#include <array>
#include <cmath>
#include <vector>

namespace mc
{
struct StereoStats
{
    double powerL = 0, powerR = 0, cross = 0;

    double level() const noexcept { return 0.5 * (powerL + powerR); }   // potenza media per canale
    double correlation() const noexcept
    {
        const double d = std::sqrt (powerL * powerR);
        return d > 1e-30 ? cross / d : 1.0;
    }
    double mid() const noexcept  { return 0.25 * (powerL + powerR + 2.0 * cross); }
    double side() const noexcept { return 0.25 * (powerL + powerR - 2.0 * cross); }
    // Side/Mid in dB: -inf = mono perfetto, 0 dB = tanto Side quanto Mid (molto largo)
    double sideToMidDb() const noexcept
    {
        const double m = mid(), s = side();
        if (s <= 1e-30) return -60.0;
        if (m <= 1e-30) return 60.0;
        return std::max (-60.0, std::min (60.0, 10.0 * std::log10 (s / m)));
    }
};

class SpectrumAnalyzer
{
public:
    static constexpr int numBands = 60;
    static constexpr double firstCentre = 20.0;

    static double bandCentre (int b) noexcept { return firstCentre * std::pow (2.0, b / 6.0); }
    static double bandLow (int b) noexcept    { return bandCentre (b) * std::pow (2.0, -1.0 / 12.0); }
    static double bandHigh (int b) noexcept   { return bandCentre (b) * std::pow (2.0, 1.0 / 12.0); }

    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        stereo = numChannels >= 2;
        fftSize = sampleRate <= 50000.0 ? 8192 : (sampleRate <= 100000.0 ? 16384 : 32768);
        hop = fftSize / 2;
        fft.prepare (fftSize);

        constexpr double pi = 3.14159265358979323846;
        window.resize ((size_t) fftSize);
        double sumW2 = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * pi * i / fftSize);   // Hann periodica
            sumW2 += window[(size_t) i] * window[(size_t) i];
        }
        binScale = 2.0 / ((double) fftSize * sumW2);   // un lato: somma bin = mean-square

        inL.assign ((size_t) fftSize, 0.0f);
        inR.assign ((size_t) fftSize, 0.0f);
        work.assign ((size_t) fftSize, FFT::Complex());

        // mappa bin → banda con sovrapposizione frazionaria (le bande basse sono più strette di un bin)
        const double binHz = sr / fftSize;
        mapStart.assign (numBands + 1, 0);
        mapBin.clear();
        mapWeight.clear();
        for (int b = 0; b < numBands; ++b)
        {
            mapStart[(size_t) b] = (int) mapBin.size();
            const double lo = bandLow (b), hi = std::min (bandHigh (b), sr * 0.5);
            if (hi > lo)
            {
                const int k0 = std::max (1, (int) std::floor (lo / binHz + 0.5));
                const int k1 = std::min (fftSize / 2 - 1, (int) std::ceil (hi / binHz + 0.5));
                for (int k = k0; k <= k1; ++k)
                {
                    const double bl = (k - 0.5) * binHz, bh = (k + 0.5) * binHz;
                    const double overlap = std::min (bh, hi) - std::max (bl, lo);
                    if (overlap > 0.0)
                    {
                        mapBin.push_back (k);
                        mapWeight.push_back (overlap / binHz);
                    }
                }
            }
        }
        mapStart[(size_t) numBands] = (int) mapBin.size();
        reset();
    }

    void reset() noexcept
    {
        std::fill (inL.begin(), inL.end(), 0.0f);
        std::fill (inR.begin(), inR.end(), 0.0f);
        fill = 0;
        frames = 0;
        for (auto& s : longTerm) s = {};
        for (auto& s : live) s = {};
        liveValid = false;
    }

    // Planare: data[0] = L (o mono), data[1] = R se stereo.
    void push (const float* const* data, int numSamples) noexcept
    {
        int i = 0;
        while (i < numSamples)
        {
            const int n = std::min (numSamples - i, fftSize - fill);
            for (int k = 0; k < n; ++k)
            {
                inL[(size_t) (fill + k)] = data[0][i + k];
                inR[(size_t) (fill + k)] = stereo ? data[1][i + k] : data[0][i + k];
            }
            fill += n;
            i += n;
            if (fill == fftSize)
            {
                processFrame();
                // sposta indietro di un hop (overlap 50%)
                std::copy (inL.begin() + hop, inL.end(), inL.begin());
                std::copy (inR.begin() + hop, inR.end(), inR.begin());
                fill = fftSize - hop;
            }
        }
    }

    long long getFrames() const noexcept { return frames; }
    double getSecondsAnalysed() const noexcept { return (double) frames * hop / sr; }
    bool isStereo() const noexcept { return stereo; }

    // Media a lungo termine (da reset) della banda b.
    StereoStats band (int b) const noexcept
    {
        StereoStats s = longTerm[(size_t) b];
        if (frames > 0) { const double f = (double) frames; s.powerL /= f; s.powerR /= f; s.cross /= f; }
        return s;
    }
    StereoStats liveBand (int b) const noexcept { return live[(size_t) b]; }

    // Somma delle bande con centro in [fLo, fHi): una "zona" (sub, bassi, low-mid…).
    StereoStats range (double fLo, double fHi) const noexcept
    {
        StereoStats r;
        for (int b = 0; b < numBands; ++b)
        {
            const double c = bandCentre (b);
            if (c >= fLo && c < fHi)
            {
                const auto s = band (b);
                r.powerL += s.powerL; r.powerR += s.powerR; r.cross += s.cross;
            }
        }
        return r;
    }

private:
    void processFrame() noexcept
    {
        double ms = 0.0;
        for (int k = 0; k < fftSize; ++k)
        {
            const double l = inL[(size_t) k], r = inR[(size_t) k];
            ms += l * l + r * r;
            work[(size_t) k] = FFT::Complex (l * window[(size_t) k], r * window[(size_t) k]);
        }
        ms /= (2.0 * fftSize);
        if (ms < 1e-6)   // -60 dBFS: silenzio, non conta
            return;

        fft.perform (work.data());

        std::array<StereoStats, numBands> frame {};
        for (int b = 0; b < numBands; ++b)
        {
            for (int m = mapStart[(size_t) b]; m < mapStart[(size_t) b + 1]; ++m)
            {
                const int k = mapBin[(size_t) m];
                const auto z  = work[(size_t) k];
                const auto zc = std::conj (work[(size_t) (fftSize - k)]);
                const auto L = 0.5 * (z + zc);
                const auto R = FFT::Complex (0.0, -0.5) * (z - zc);
                const double w = mapWeight[(size_t) m] * binScale;
                frame[(size_t) b].powerL += w * std::norm (L);
                frame[(size_t) b].powerR += w * std::norm (R);
                frame[(size_t) b].cross  += w * (L.real() * R.real() + L.imag() * R.imag());
            }
        }

        // media "live": esponenziale con costante ~1 s
        const double tau = 1.0, dt = (double) hop / sr;
        const double a = liveValid ? 1.0 - std::exp (-dt / tau) : 1.0;
        for (int b = 0; b < numBands; ++b)
        {
            auto& lt = longTerm[(size_t) b];
            const auto& f = frame[(size_t) b];
            lt.powerL += f.powerL; lt.powerR += f.powerR; lt.cross += f.cross;
            auto& lv = live[(size_t) b];
            lv.powerL += a * (f.powerL - lv.powerL);
            lv.powerR += a * (f.powerR - lv.powerR);
            lv.cross  += a * (f.cross - lv.cross);
        }
        liveValid = true;
        ++frames;
    }

    double sr = 48000.0;
    bool stereo = true;
    int fftSize = 8192, hop = 4096, fill = 0;
    double binScale = 1.0;
    FFT fft;
    std::vector<double> window;
    std::vector<float> inL, inR;
    std::vector<FFT::Complex> work;
    std::vector<int> mapStart, mapBin;
    std::vector<double> mapWeight;
    std::array<StereoStats, numBands> longTerm {}, live {};
    bool liveValid = false;
    long long frames = 0;
};
} // namespace mc
