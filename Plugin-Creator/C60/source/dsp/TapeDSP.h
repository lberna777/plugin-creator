#pragma once
// Blocchi DSP del registratore a cassetta. C++ puro (niente JUCE), zero allocazioni dopo prepare():
// si testano anche fuori dal plugin (tests/measure.cpp).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace c60
{
constexpr double pi = 3.14159265358979323846;

//==============================================================================
// xorshift32: veloce, deterministico, senza allocazioni.
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    explicit Rng (uint32_t seed = 0x9E3779B9u) : s (seed ? seed : 1u) {}
    float uni()     { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) s * (1.0f / 4294967296.0f); } // [0,1)
    float bipolar() { return uni() * 2.0f - 1.0f; }
    float gauss()   { return (uni() + uni() + uni() + uni() - 2.0f) * 1.7320508f; }                          // var ≈ 1
};

//==============================================================================
// Biquad TDF-II con formule RBJ: i coefficienti si ricalcolano senza toccare lo stato né allocare.
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    inline double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setLowPass (double fs, double f, double q)
    {
        const double w = 2 * pi * f / fs, cw = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + al, -2 * cw, 1 - al);
    }
    void setHighPass (double fs, double f, double q)
    {
        const double w = 2 * pi * f / fs, cw = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + al, -2 * cw, 1 - al);
    }
    void setPeak (double fs, double f, double q, double dB)
    {
        const double A = std::pow (10.0, dB / 40), w = 2 * pi * f / fs, cw = std::cos (w), al = std::sin (w) / (2 * q);
        set (1 + al * A, -2 * cw, 1 - al * A, 1 + al / A, -2 * cw, 1 - al / A);
    }
    void setHighShelf (double fs, double f, double dB)  // pendenza S = 1
    {
        const double A = std::pow (10.0, dB / 40), w = 2 * pi * f / fs, cw = std::cos (w);
        const double al = std::sin (w) / 2 * std::sqrt (2.0), sA = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * cw + sA), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - sA),
             (A + 1) - (A - 1) * cw + sA, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sA);
    }

private:
    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2)
    {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }
};

//==============================================================================
// Shelf del primo ordine H(s) = (1 + s·τz) / (1 + s·τp), bilineare.
// Pre-enfasi e de-enfasi sono l'una l'esatto reciproco dell'altra (zero e polo scambiati):
// a piccolo segnale la cascata è piatta; ad alto livello gli acuti, enfatizzati prima del nastro,
// saturano prima → la compressione degli acuti tipica della cassetta.
struct FirstOrderShelf
{
    double b0 = 1, b1 = 0, a1 = 0, z1 = 0;

    void reset() { z1 = 0; }
    void set (double fs, double tauZero, double tauPole)
    {
        const double K = 2 * fs, a0 = 1 + tauPole * K;
        b0 = (1 + tauZero * K) / a0;
        b1 = (1 - tauZero * K) / a0;
        a1 = (1 - tauPole * K) / a0;
    }
    inline double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y;
        return y;
    }
};

//==============================================================================
// Isteresi magnetica di Jiles-Atherton, risolta con Runge-Kutta 2 (approccio di J. Chowdhury,
// "Real-time physical modelling for analog tape machines", DAFx 2019). Va fatta girare sovracampionata.
class Hysteresis
{
public:
    void prepare (double sampleRate) { T = 1.0 / sampleRate; reset(); }
    void reset() { M = Hn1 = Hdn1 = 0; }

    // sat 0..1: quanto presto satura;  width 0..1: quanto è "stretto" il ciclo (più bias → più largo/lineare)
    void setParams (double sat, double width)
    {
        width = std::clamp (width, 0.0, 0.99);
        Ms   = 0.5 + 1.5 * (1.0 - std::clamp (sat, 0.0, 1.0));
        a    = Ms / (0.01 + 6.0 * width);
        c    = std::sqrt (1.0 - width) - 0.01;
        MsOa = Ms / a;
    }

    double getMs() const { return Ms; }

    inline double process (double H)
    {
        const double Hd = (H - Hn1) / T;
        const double k1 = T * dMdt (M, Hn1, Hdn1);
        const double k2 = T * dMdt (M + 0.5 * k1, 0.5 * (H + Hn1), 0.5 * (Hd + Hdn1));
        double Mn = M + k2;
        if (! std::isfinite (Mn)) Mn = 0;                        // rete di sicurezza: mai NaN a valle
        M = std::clamp (Mn, -Ms, Ms);
        Hn1 = H; Hdn1 = Hd;
        return M;
    }

private:
    inline double dMdt (double m, double H, double Hd) const
    {
        const double Q = (H + alpha * m) / a;
        double L, Lp;
        if (std::abs (Q) > 1.0e-4)
        {
            const double cothQ = 1.0 / std::tanh (Q);
            L  = cothQ - 1.0 / Q;
            Lp = 1.0 / (Q * Q) - cothQ * cothQ + 1.0;
        }
        else
        {
            L = Q / 3.0; Lp = 1.0 / 3.0;
        }
        const double Mdiff  = Ms * L - m;
        const double delta  = Hd >= 0 ? 1.0 : -1.0;
        const double deltaM = ((delta > 0) == (Mdiff > 0)) ? 1.0 : 0.0;
        const double t1 = ((1 - c) * deltaM * Mdiff) / ((1 - c) * delta * k - alpha * Mdiff) * Hd;
        const double t2 = c * MsOa * Hd * Lp;
        return (t1 + t2) / (1.0 - c * alpha * MsOa * Lp);
    }

    static constexpr double alpha = 1.6e-3, k = 0.47875;
    double Ms = 1, a = 0.3, c = 0.5, MsOa = 3, T = 1.0 / 192000.0;
    double M = 0, Hn1 = 0, Hdn1 = 0;
};

//==============================================================================
// Rumore liscio e limitato in [-1, 1]: un nuovo bersaglio casuale ogni ~1/rate s, doppio passa-basso.
class SmoothRandom
{
public:
    void prepare (double fs, double rateHz, uint32_t seed)
    {
        rng = Rng (seed);
        period = fs / rateHz;
        coeff = 1.0 - std::exp (-2 * pi * rateHz / fs);
        counter = 0; s1 = s2 = target = 0;
    }
    inline double next()
    {
        if (--counter <= 0) { target = rng.bipolar(); counter = period * (0.5 + rng.uni()); }
        s1 += coeff * (target - s1);
        s2 += coeff * (s1 - s2);
        return s2;
    }
private:
    Rng rng;
    double period = 1, coeff = 0.01, counter = 0, s1 = 0, s2 = 0, target = 0;
};

//==============================================================================
// Wow & flutter: una sola modulazione per tutti i canali (stesso capstan, stesso nastro).
// Restituisce lo scostamento di ritardo in campioni, a media nulla.
// Calibrazione: ampiezza di ritardo A = deviazione_di_pitch / (2π f).
class WowFlutter
{
public:
    void prepare (double sampleRate, double speedFactor)
    {
        fs = sampleRate;
        speed = speedFactor;
        wowRnd.prepare (fs, 1.1, 0x1234567u);
        drift.prepare (fs, 0.25, 0x2345678u);
        flutRnd.prepare (fs, 22.0, 0x3456789u);
        phW = 0.3; phF1 = 1.1; phF2 = 2.3;
    }

    // wow, flutter: 0..1
    inline double next (double wow, double flutter)
    {
        // a 9,5 cm/s il trasporto è più stabile e le frequenze di rotazione raddoppiano
        const double fW = 0.55 * (1.0 + 0.18 * drift.next());
        const double fF1 = 6.3 * speed, fF2 = 11.7 * speed;
        phW  += 2 * pi * fW / fs;  if (phW  > 2 * pi) phW  -= 2 * pi;
        phF1 += 2 * pi * fF1 / fs; if (phF1 > 2 * pi) phF1 -= 2 * pi;
        phF2 += 2 * pi * fF2 / fs; if (phF2 > 2 * pi) phF2 -= 2 * pi;

        const double devW = wowDepth * wow / speed, devF = flutDepth * flutter / speed;
        const double dW = devW * (0.75 * std::sin (phW) / (2 * pi * 0.55) + 0.9 * wowRnd.next() / (2 * pi * 0.5));
        const double dF = devF * (0.6 * std::sin (phF1) / (2 * pi * fF1) + 0.3 * std::sin (phF2) / (2 * pi * fF2)
                                  + 0.5 * flutRnd.next() / (2 * pi * 9.0));
        return (dW + dF) * fs;
    }

    // scostamento massimo possibile, in secondi (dimensiona il ritardo centrale)
    static constexpr double maxExcursionSeconds = 0.004;

private:
    static constexpr double wowDepth = 0.008, flutDepth = 0.003;   // deviazione di pitch di picco a 100 %
    SmoothRandom wowRnd, drift, flutRnd;
    double fs = 48000, speed = 1, phW = 0, phF1 = 0, phF2 = 0;
};

//==============================================================================
// Linea di ritardo a lettura frazionaria (Hermite a 4 punti). Buffer allocato solo in prepare().
class FracDelay
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 1;
        while (size < maxDelaySamples + 8) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1; w = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }

    // ritardo in campioni rispetto all'ultimo campione scritto (≥ 1)
    inline float read (double delay) const
    {
        const double pos = (double) (w - 1) - delay;
        const double fl = std::floor (pos);
        const float t = (float) (pos - fl);
        const int i = (int) fl;
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }
    inline float readInt (int delay) const { return at (w - 1 - delay); }

private:
    inline float at (int i) const { return buf[(size_t) (i & mask)]; }
    std::vector<float> buf;
    int mask = 0, w = 0;
};

//==============================================================================
// Dropout: brevi cali di livello e di acuti dovuti a polvere/usura dell'ossido.
// Evento condiviso tra i canali, profondità leggermente diversa per canale.
class Dropouts
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        rng = Rng (0xC0FFEEu);
        attack = 1.0 - std::exp (-1.0 / (0.003 * fs));
        release = 1.0 - std::exp (-1.0 / (0.012 * fs));
        env = 0; remaining = 0; depth = 0;
    }

    // age 0..1 → intensità del dropout corrente, 0..1
    inline double next (double age)
    {
        if (remaining <= 0)
        {
            const double perSecond = 2.5 * age * age * age;     // rari finché il nastro è in buono stato
            if (age > 0 && rng.uni() < perSecond / fs)
            {
                remaining = (int) (fs * (0.015 + 0.12 * rng.uni()));
                depth = (0.25 + 0.75 * rng.uni()) * (0.35 + 0.65 * age);
                skewR = 0.6 + 0.4 * rng.uni();
            }
        }
        const double tgt = remaining > 0 ? depth : 0.0;
        if (remaining > 0) --remaining;
        env += (tgt > env ? attack : release) * (tgt - env);
        return env;
    }
    double channelSkew (int ch) const { return ch == 0 ? 1.0 : skewR; }

private:
    Rng rng;
    double fs = 48000, attack = 0.01, release = 0.01, env = 0, depth = 0, skewR = 1;
    int remaining = 0;
};

//==============================================================================
// Tipo di nastro: cosa cambia fisicamente tra ferro, cromo e metallo.
struct TapeFormula
{
    double emphasisTau;   // costante di tempo IEC della curva di riproduzione (s)
    double sat, width;    // isteresi
    double drive;         // campo H per un segnale a fondo scala
    double hfCorner;      // -3 dB delle perdite di testina a 4,76 cm/s (Hz)
    double hissDb;        // rumore relativo (dB)
};

inline const TapeFormula& tapeFormula (int type)
{
    static const TapeFormula f[3] = {
        // drive tarato perché a -12 dBFS (1 kHz) la THD sia ≈ 1,5 % / 1,0 % / 0,6 %: il metallo ha più headroom
        { 120e-6, 0.62, 0.42, 1.462, 12500.0,  0.0 },   // Type I  · ferro (normal)
        {  70e-6, 0.52, 0.50, 0.865, 15000.0, -3.0 },   // Type II · cromo
        {  70e-6, 0.38, 0.58, 0.359, 16500.0, -4.0 },   // Type IV · metallo
    };
    return f[std::clamp (type, 0, 2)];
}

//==============================================================================
// Basse frequenze in riproduzione: "head bump" (risonanza della testina, sale con la velocità)
// e perdita sotto il bump. speed = 1 a 4,76 cm/s, 2 a 9,5 cm/s.
inline void setLowEnd (Biquad& hp, Biquad& bump, Biquad& dip, double fs, double speed)
{
    hp.setHighPass (fs, 18.0 * std::sqrt (speed), 0.6);
    bump.setPeak (fs, 62.0 * speed, 1.1, speed > 1 ? 2.0 : 2.5);
    dip.setPeak (fs, 62.0 * speed * 2.3, 1.4, -1.0);
}

} // namespace c60
