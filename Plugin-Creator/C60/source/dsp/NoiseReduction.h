#pragma once
// Riduzione di rumore a compander, nello stile dei sistemi dell'epoca (B, C, S, dbx).
// Codifica in registrazione, decodifica in riproduzione. La decodifica è l'inversa ESATTA della
// codifica se il livello sul nastro è quello giusto: se la calibrazione di livello è sbagliata,
// il decoder "insegue" male (mistracking), proprio come su una piastra non calibrata.

#include "TapeDSP.h"

namespace c60
{
// Filtro del primo ordine (bilineare) con stato esposto: serve a risolvere la decodifica in forma chiusa.
struct OnePole
{
    double b0 = 1, b1 = 0, a1 = 0, s = 0;

    void setHighPass (double fs, double fc)
    {
        const double K = std::tan (pi * std::min (fc, 0.45 * fs) / fs), n = 1.0 / (1.0 + K);
        b0 = n; b1 = -n; a1 = (K - 1.0) * n;
    }
    void setLowPass (double fs, double fc)
    {
        const double K = std::tan (pi * std::min (fc, 0.45 * fs) / fs), n = 1.0 / (1.0 + K);
        b0 = K * n; b1 = K * n; a1 = (K - 1.0) * n;
    }
    void reset() { s = 0; }
    inline double process (double x)
    {
        const double y = b0 * x + s;
        s = b1 * x - a1 * y;
        return y;
    }
};

struct EnvFollower
{
    double att = 0.1, rel = 0.01, e = 0;
    void prepare (double fs, double attackMs, double releaseMs)
    {
        att = 1.0 - std::exp (-1.0 / (attackMs * 1e-3 * fs));
        rel = 1.0 - std::exp (-1.0 / (releaseMs * 1e-3 * fs));
        e = 0;
    }
    inline void process (double x)
    {
        const double v = std::abs (x);
        e += (v > e ? att : rel) * (v - e);
    }
};

// Stadio a scaffale scorrevole: y = x + g·F(x), con F passa-alto (acuti) o passa-basso (bassi)
// e g che cala quando il segnale nella banda è forte (a basso livello: spinta massima).
class SlidingStage
{
public:
    void prepare (double fs, bool highBand, double fc, double maxBoostDb, double thresholdLin)
    {
        if (highBand) f.setHighPass (fs, fc); else f.setLowPass (fs, fc);
        env.prepare (fs, 2.0, 60.0);
        gmax = std::pow (10.0, maxBoostDb / 20.0) - 1.0;
        invThr = 1.0 / thresholdLin;
        f.reset();
    }
    void reset() { f.reset(); env.e = 0; }

    inline double encode (double x)
    {
        const double g = gain();
        const double band = f.process (x);
        env.process (band);
        return x + g * band;
    }

    // y = x + g·(b0·x + s)  →  x = (y − g·s) / (1 + g·b0)
    inline double decode (double y)
    {
        const double g = gain();
        const double x = (y - g * f.s) / (1.0 + g * f.b0);
        env.process (f.process (x));
        return x;
    }

private:
    inline double gain() const
    {
        const double q = env.e * invThr;
        return gmax / (1.0 + q * q);
    }
    OnePole f;
    EnvFollower env;
    double gmax = 2.16, invThr = 250;
};

// Compander 2:1 a banda larga con enfasi (stile dbx II): niente fruscio, ma "pompa" se il nastro sbaglia.
class BroadbandCompander
{
public:
    void prepare (double fs)
    {
        // enfasi: +12 dB sugli acuti sopra ~1,6 kHz, esattamente invertibile
        const double tz = 1.0 / (2 * pi * 1600.0), tp = tz / 4.0;
        pre.set (fs, tz, tp);
        de.set (fs, tp, tz);
        rmsCoeff = 1.0 - std::exp (-1.0 / (0.015 * fs));
        reset();
    }
    void reset() { pre.reset(); de.reset(); msEnc = msDec = 0; }

    inline double encode (double x)
    {
        const double p = pre.process (x);
        msEnc += rmsCoeff * (p * p - msEnc);
        const double g = std::min (std::sqrt (ref / std::sqrt (std::max (msEnc, floorMs))), maxGain);
        return p * g;
    }
    inline double decode (double y)
    {
        msDec += rmsCoeff * (y * y - msDec);
        const double g = std::max (std::sqrt (std::max (msDec, floorMs * ref)) / ref, 1.0 / maxGain);
        return de.process (y * g);
    }

private:
    static constexpr double ref = 0.1, floorMs = 1e-12, maxGain = 31.6;   // ±30 dB
    FirstOrderShelf pre, de;
    double rmsCoeff = 0.001, msEnc = 0, msDec = 0;
};

// Selettore NR: 0 OFF · 1 B · 2 C · 3 S · 4 DBX. Livelli riferiti a 0 VU = -18 dBFS.
class NoiseReduction
{
public:
    void prepare (double sampleRate) { fs = sampleRate; setMode (mode, true); }

    void setMode (int newMode, bool force = false)
    {
        newMode = std::clamp (newMode, 0, 4);
        if (newMode == mode && ! force) return;
        mode = newMode;
        constexpr double dB40 = 0.0040, dB60 = 0.0004;   // soglie (inviluppo della banda)
        hfHigh.prepare (fs, true, 1500.0, 10.0, dB40);
        hfLow.prepare (fs, true, 1000.0, 10.0, dB60);
        lf.prepare (fs, false, 200.0, 10.0, 0.002);
        dbx.prepare (fs);
    }
    int getMode() const { return mode; }

    inline double encode (double x)
    {
        switch (mode)
        {
            case 1: return hfHigh.encode (x);
            case 2: return hfLow.encode (hfHigh.encode (x));
            case 3: return lf.encode (hfLow.encode (hfHigh.encode (x)));
            case 4: return dbx.encode (x);
            default: return x;
        }
    }
    inline double decode (double y)
    {
        switch (mode)
        {
            case 1: return hfHigh.decode (y);
            case 2: return hfHigh.decode (hfLow.decode (y));
            case 3: return hfHigh.decode (hfLow.decode (lf.decode (y)));
            case 4: return dbx.decode (y);
            default: return y;
        }
    }

private:
    double fs = 48000;
    int mode = 0;
    SlidingStage hfHigh, hfLow, lf;
    BroadbandCompander dbx;
};

} // namespace c60
