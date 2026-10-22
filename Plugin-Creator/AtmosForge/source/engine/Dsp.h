#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

// Mattoncini DSP del motore procedurale. Tutto POD, nessuna allocazione:
// ogni oggetto vive in un array a dimensione fissa dentro la scena che lo usa.

namespace af
{

constexpr float kPi  = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

// ---------------------------------------------------------------- RNG
// xorshift32: deterministico dal seed (stessa scena + stesso seed = stesso render).
struct Rng
{
    uint32_t state = 0x9E3779B9u;

    void seed (uint32_t s) noexcept { state = (s == 0 ? 0x9E3779B9u : s); }

    uint32_t nextUInt() noexcept
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return state;
    }
    // [0,1)
    float uni() noexcept { return (float) (nextUInt() >> 8) * (1.0f / 16777216.0f); }
    // [-1,1)
    float bi() noexcept { return uni() * 2.0f - 1.0f; }
    float range (float lo, float hi) noexcept { return lo + (hi - lo) * uni(); }
    int   intRange (int lo, int hi) noexcept { return lo + (int) (uni() * (float) (hi - lo + 1)); }
};

// ---------------------------------------------------------------- rumori
struct WhiteNoise
{
    Rng rng;
    float next() noexcept { return rng.bi(); }
};

// Pink (Paul Kellet, economico e stabile).
struct PinkNoise
{
    Rng rng;
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;

    float next() noexcept
    {
        const float w = rng.bi();
        b0 = 0.99886f * b0 + w * 0.0555179f;
        b1 = 0.99332f * b1 + w * 0.0750759f;
        b2 = 0.96900f * b2 + w * 0.1538520f;
        b3 = 0.86650f * b3 + w * 0.3104856f;
        b4 = 0.55000f * b4 + w * 0.5329522f;
        b5 = -0.7616f * b5 - w * 0.0168980f;
        const float out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
        b6 = w * 0.115926f;
        return out * 0.11f;
    }
};

// Brown: white integrato con leakage (niente deriva DC).
struct BrownNoise
{
    Rng rng;
    float last = 0.0f;

    float next() noexcept
    {
        last = 0.998f * last + 0.02f * rng.bi();
        return last * 3.5f;
    }
};

// ---------------------------------------------------------------- filtri TPT
// State Variable TPT: coefficienti ricalcolati solo quando cambia il cutoff
// (mai per-sample su un parametro fermo, mai allocazioni).
struct SvfTpt
{
    float g = 0.1f, k = 1.0f, a1 = 0, a2 = 0, a3 = 0;
    float ic1 = 0, ic2 = 0;
    float sampleRate = 44100.0f;
    float lastCut = -1.0f, lastRes = -1.0f;

    void prepare (float sr) noexcept { sampleRate = sr; reset(); setCutoff (1000.0f, 0.707f); }
    void reset() noexcept { ic1 = ic2 = 0.0f; }

    void setCutoff (float hz, float q) noexcept
    {
        hz = std::min (std::max (hz, 12.0f), sampleRate * 0.48f);
        q  = std::max (q, 0.05f);
        // confronto esatto voluto: si ricalcola solo quando il valore cambia davvero
        if (std::abs (hz - lastCut) < 1.0e-6f && std::abs (q - lastRes) < 1.0e-6f) return;
        lastCut = hz; lastRes = q;
        g  = std::tan (kPi * hz / sampleRate);
        k  = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    // Un passo: restituisce lp/bp/hp insieme (il chiamante prende ciò che serve).
    inline void tick (float x, float& lp, float& bp, float& hp) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }

    inline float lowpass  (float x) noexcept { float l, b, h; tick (x, l, b, h); return l; }
    inline float bandpass (float x) noexcept { float l, b, h; tick (x, l, b, h); return b; }
    inline float highpass (float x) noexcept { float l, b, h; tick (x, l, b, h); return h; }
};

// One-pole per smoothing di controllo (non audio).
struct OnePole
{
    float z = 0.0f, a = 0.01f;
    void setTimeMs (float ms, float sr) noexcept
    {
        a = 1.0f - std::exp (-1.0f / std::max (1.0f, ms * 0.001f * sr));
    }
    inline float process (float x) noexcept { z += a * (x - z); return z; }
    void reset (float v = 0.0f) noexcept { z = v; }
};

// ---------------------------------------------------------------- inviluppi
// Attack/decay esponenziale monocolpo: basta per transienti (crepitio, passo, goccia).
struct AREnv
{
    float value = 0.0f, target = 0.0f;
    float atkCoef = 0.5f, decCoef = 0.999f;
    bool  attacking = false, running = false;

    void setTimes (float attackMs, float decayMs, float sr) noexcept
    {
        atkCoef = 1.0f - std::exp (-1.0f / std::max (1.0f, attackMs * 0.001f * sr));
        decCoef = std::exp (-1.0f / std::max (1.0f, decayMs * 0.001f * sr));
    }
    void trigger (float peak) noexcept { target = peak; value = 0.0f; attacking = true; running = true; }

    inline float next() noexcept
    {
        if (! running) return 0.0f;
        if (attacking)
        {
            value += atkCoef * (target * 1.05f - value);
            if (value >= target) { value = target; attacking = false; }
        }
        else
        {
            value *= decCoef;
            if (value < 1.0e-5f) { value = 0.0f; running = false; }
        }
        return value;
    }
    bool isActive() const noexcept { return running; }
};

// Blocca la continua e il sub inudibile (sotto ~20 Hz) che i rumori integrati accumulano.
struct DcBlock
{
    float x1 = 0.0f, y1 = 0.0f, R = 0.9975f;
    void prepare (float sr) noexcept { R = 1.0f - (kTwoPi * 18.0f / sr); }
    void reset() noexcept { x1 = y1 = 0.0f; }
    inline float process (float x) noexcept
    {
        const float y = x - x1 + R * y1;
        x1 = x; y1 = y;
        return y;
    }
};

// Modo risonante (corpo che risuona: legno, metallo). Oscillatore smorzato.
struct ModalRes
{
    float cosW = 1.0f, sinW = 0.0f, decay = 0.999f;
    float y1 = 0.0f, y2 = 0.0f;   // stato dell'oscillatore
    float amp = 0.0f;

    void set (float freq, float decayMs, float sr) noexcept
    {
        const float w = kTwoPi * std::min (freq, sr * 0.45f) / sr;
        cosW = std::cos (w); sinW = std::sin (w);
        decay = std::exp (-1.0f / std::max (1.0f, decayMs * 0.001f * sr));
    }
    void strike (float a) noexcept { amp = a; y1 = 0.0f; y2 = a; }

    inline float next() noexcept
    {
        // oscillatore rotazionale smorzato (stabile, niente accumulo di fase)
        const float x = cosW * y2 - sinW * y1;
        const float y = sinW * y2 + cosW * y1;
        y1 = y * decay; y2 = x * decay;
        return y1;
    }
    bool isActive() const noexcept { return std::abs (y1) + std::abs (y2) > 1.0e-5f; }
};

// Oscillatore sinusoidale a fase accumulata (per clacson e motori).
struct SineOsc
{
    float phase = 0.0f, inc = 0.0f;
    void setFreq (float hz, float sr) noexcept { inc = kTwoPi * hz / sr; }
    inline float next() noexcept
    {
        phase += inc;
        if (phase >= kTwoPi) phase -= kTwoPi;
        return std::sin (phase);
    }
};

// ---------------------------------------------------------------- util
// Soft clip cubico: unitario fino a ±1, saturazione dolce, mai oltre ±1.
inline float softClip (float x) noexcept
{
    if (x >=  1.5f) return  1.0f;
    if (x <= -1.5f) return -1.0f;
    return x - (x * x * x) * (1.0f / 6.75f);
}

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

// Pan equal-power: 0 = sinistra, 0.5 = centro, 1 = destra.
inline void panGains (float pan, float& gL, float& gR) noexcept
{
    pan = std::min (std::max (pan, 0.0f), 1.0f);
    const float a = pan * kPi * 0.5f;
    gL = std::cos (a); gR = std::sin (a);
}

} // namespace af
