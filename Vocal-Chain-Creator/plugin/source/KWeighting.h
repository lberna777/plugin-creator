#pragma once
#include <cmath>

/*  VOCAL FORGE — pesatura K (ITU-R BS.1770-4), a qualunque sample rate.

    Due biquad in serie: uno shelf alto (+4 dB sopra ~1.5 kHz, la "testa") e un passa-alto
    a 38 Hz (RLB). I coefficienti sono quelli della norma riportati a ogni fs con la
    trasformata bilineare, come fa pyloudnorm. Serve a due cose:
      - il level match del DSP: catena accesa e spenta si confrontano in LOUDNESS, non in RMS;
      - il meter dei test, che deve misurare con la STESSA pesatura che il DSP insegue.

    Nessuna dipendenza da JUCE: lo usa anche il self test offline.
*/
namespace vf::kweighting
{

struct Coefficients { double b0, b1, b2, a1, a2; };   // già normalizzati per a0

/** Shelf alto della pesatura K. */
inline Coefficients shelf (double sampleRate) noexcept
{
    constexpr double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
    const double a = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * f0 / sampleRate;
    const double alpha = std::sin (w0) / (2.0 * q);
    const double cosw = std::cos (w0), sqrtA = std::sqrt (a);

    const double a0 = (a + 1.0) - (a - 1.0) * cosw + 2.0 * sqrtA * alpha;
    return { a * ((a + 1.0) + (a - 1.0) * cosw + 2.0 * sqrtA * alpha) / a0,
             -2.0 * a * ((a - 1.0) + (a + 1.0) * cosw) / a0,
             a * ((a + 1.0) + (a - 1.0) * cosw - 2.0 * sqrtA * alpha) / a0,
             2.0 * ((a - 1.0) - (a + 1.0) * cosw) / a0,
             ((a + 1.0) - (a - 1.0) * cosw - 2.0 * sqrtA * alpha) / a0 };
}

/** Passa-alto RLB della pesatura K. */
inline Coefficients highPass (double sampleRate) noexcept
{
    constexpr double f0 = 38.13547087602444, q = 0.5003270373238773;
    const double w0 = 2.0 * 3.14159265358979323846 * f0 / sampleRate;
    const double alpha = std::sin (w0) / (2.0 * q);
    const double cosw = std::cos (w0);

    const double a0 = 1.0 + alpha;
    return { (1.0 + cosw) * 0.5 / a0, -(1.0 + cosw) / a0, (1.0 + cosw) * 0.5 / a0,
             -2.0 * cosw / a0, (1.0 - alpha) / a0 };
}

/** Un canale di pesatura K: shelf poi passa-alto, in double (le code a 38 Hz sono lunghe). */
struct Filter
{
    Coefficients s {}, h {};
    double s1 = 0.0, s2 = 0.0, h1 = 0.0, h2 = 0.0;

    void prepare (double sampleRate) noexcept { s = shelf (sampleRate); h = highPass (sampleRate); reset(); }
    void reset() noexcept { s1 = s2 = h1 = h2 = 0.0; }

    double process (double x) noexcept
    {
        const double y = s.b0 * x + s1;
        s1 = s.b1 * x - s.a1 * y + s2;
        s2 = s.b2 * x - s.a2 * y;

        const double z = h.b0 * y + h1;
        h1 = h.b1 * y - h.a1 * z + h2;
        h2 = h.b2 * y - h.a2 * z;
        return z;
    }
};

/** -0.691 dB: la costante della norma che porta un sinusoide a 1 kHz a 0 dBFS a -3.01 LUFS su un canale. */
constexpr double kOffsetDb = -0.691;

/** Blocchi della norma: 400 ms, uno nuovo ogni 100 ms (sovrapposizione 75 %), gate relativo a -10 LU. */
constexpr double kBlockSeconds = 0.4, kHopSeconds = 0.1, kRelativeGateLu = 10.0;
constexpr int    kHopsPerBlock = 4;

inline double toLufs (double meanSquare) noexcept
{
    return kOffsetDb + 10.0 * std::log10 (meanSquare > 1.0e-20 ? meanSquare : 1.0e-20);
}

/*  Loudness "integrata" che segue il segnale: gli stessi blocchi e gli stessi due gate di BS.1770,
    ma con memoria esponenziale invece che su tutta la durata. Senza allocazioni: la usa il DSP.

    Il gate relativo è quello che conta per una catena vocale: un gate di rumore abbassa le code
    delle frasi, la norma le esclude dalla misura, e una media di potenza semplice no. Misurare
    in un altro modo vuol dire pareggiare un numero diverso da quello che legge un meter. */
struct GatedLoudness
{
    double absoluteMean = 0.0, gatedMean = 0.0;
    double absoluteSeconds = 0.0, gatedSeconds = 0.0;

    void reset() noexcept { absoluteMean = gatedMean = absoluteSeconds = gatedSeconds = 0.0; }

    /** Un blocco da 400 ms appena chiuso. `windowSeconds`: la memoria della media esponenziale. */
    void addBlock (double meanSquare, double absoluteGateLufs, double windowSeconds) noexcept
    {
        if (toLufs (meanSquare) <= absoluteGateLufs) return;
        absoluteMean = update (absoluteMean, absoluteSeconds, meanSquare, windowSeconds);

        if (toLufs (meanSquare) <= toLufs (absoluteMean) - kRelativeGateLu) return;
        gatedMean = update (gatedMean, gatedSeconds, meanSquare, windowSeconds);
    }

    bool   hasValue() const noexcept { return gatedSeconds > 0.0; }

private:
    static double update (double mean, double& seconds, double value, double windowSeconds) noexcept
    {
        // all'inizio media cumulativa (si assesta in fretta), poi esponenziale sulla finestra
        seconds += kHopSeconds;
        const double cumulative = kHopSeconds / seconds;
        const double exponential = 1.0 - std::exp (-kHopSeconds / (windowSeconds > kHopSeconds ? windowSeconds : kHopSeconds));
        const double alpha = cumulative > exponential ? cumulative : exponential;
        return mean + alpha * (value - mean);
    }
};

} // namespace vf::kweighting
