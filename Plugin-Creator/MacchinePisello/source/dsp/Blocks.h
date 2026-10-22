#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <vector>

// Blocchi DSP elementari. Regola: nessuna allocazione fuori da prepare().
namespace mp
{
constexpr float kPi    = juce::MathConstants<float>::pi;
constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

inline float onePoleCoef (float seconds, double fs) noexcept
{
    return seconds <= 0.0f ? 1.0f : 1.0f - std::exp (-1.0f / (seconds * (float) fs));
}

inline float semitonesToHz (float midiNote) noexcept
{
    return 440.0f * std::exp ((midiNote - 69.0f) * 0.057762265f);   // ln(2)/12
}

//==============================================================================
// xorshift32: deterministico (seed in prepare) → render ripetibili, test di determinismo possibili.
struct Rng
{
    uint32_t state = 0x9E3779B9u;
    void  seed (uint32_t s) noexcept { state = s != 0 ? s : 1u; }
    float next01() noexcept
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return (float) (state >> 8) * (1.0f / 16777216.0f);
    }
    float bipolar() noexcept { return next01() * 2.0f - 1.0f; }
};

//==============================================================================
// Random walk levigato: il "respiro" di un VCO analogico che non sta mai fermo.
struct SmoothRandom
{
    float value = 0.0f, target = 0.0f, coef = 0.0f;
    int   counter = 0, period = 1;

    void prepare (double fs, float periodSeconds, float smoothSeconds)
    {
        period  = juce::jmax (1, (int) (periodSeconds * fs));
        coef    = onePoleCoef (smoothSeconds, fs);
        counter = 0;
        value = target = 0.0f;
    }

    float process (Rng& rng) noexcept
    {
        if (--counter <= 0) { counter = period; target = rng.bipolar(); }
        value += (target - value) * coef;
        return value;
    }
};

//==============================================================================
inline float polyBlep (float t, float dt) noexcept
{
    if (t < dt)        { t /= dt;               return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt;   return t * t + t + t + 1.0f; }
    return 0.0f;
}

inline float blepSaw (float phase, float dt) noexcept
{
    return 2.0f * phase - 1.0f - polyBlep (phase, dt);
}

// VCO con morph continuo: triangolo → dente di sega → quadra → impulso stretto.
struct Oscillator
{
    float phase = 0.0f;

    void reset (float startPhase = 0.0f) noexcept { phase = startPhase; }

    float process (float dt, float wave) noexcept
    {
        const float saw = blepSaw (phase, dt);
        float out;

        if (wave < 1.0f / 3.0f)
        {
            const float tri = 1.0f - 4.0f * std::abs (phase - 0.5f);
            out = tri + (saw - tri) * (wave * 3.0f);
        }
        else if (wave < 2.0f / 3.0f)
        {
            const float square = saw - blepSaw (wrap (phase + 0.5f), dt);
            out = saw + (square - saw) * ((wave - 1.0f / 3.0f) * 3.0f);
        }
        else
        {
            const float pw = 0.5f - 0.44f * ((wave - 2.0f / 3.0f) * 3.0f);
            out = saw - blepSaw (wrap (phase + pw), dt);
        }

        phase = wrap (phase + dt);
        return out;
    }

    float processSquare (float dt) noexcept
    {
        const float out = blepSaw (phase, dt) - blepSaw (wrap (phase + 0.5f), dt);
        phase = wrap (phase + dt);
        return out;
    }

    static float wrap (float p) noexcept { return p >= 1.0f ? p - 1.0f : p; }
};

//==============================================================================
// Wavefolder sinusoidale (stile West Coast). amount = 0 → trasparente.
inline float wavefold (float x, float amount) noexcept
{
    if (amount <= 0.0005f)
        return x;
    const float gain   = 1.0f + 7.0f * amount;
    const float folded = std::sin (0.5f * kPi * gain * x);
    return x + (folded - x) * juce::jmin (1.0f, amount * 5.0f);
}

//==============================================================================
// Ladder 24 dB/oct a ritardo zero (TPT, Zavalishin), saturazione tanh sull'ingresso retroazionato.
// Coefficienti calcolati per-sample dal cutoff modulato: nessuna allocazione, sempre stabile.
struct Ladder
{
    std::array<float, 4> s {};

    void reset() noexcept { s.fill (0.0f); }

    float process (float x, float cutoffHz, float resonance, float drive, double fs) noexcept
    {
        const float g  = std::tan (kPi * cutoffHz / (float) fs);
        const float G  = g / (1.0f + g);
        const float oG = 1.0f - G;

        // Stima implicita dell'uscita: y4 = G^4·u + S
        const float S = G * G * G * oG * s[0] + G * G * oG * s[1] + G * oG * s[2] + oG * s[3];
        const float k = resonance * 4.1f;

        float u = (x * drive * (1.0f + 0.35f * k) - k * S) / (1.0f + k * G * G * G * G);
        u = std::tanh (u);

        for (auto& state : s)
        {
            const float v = (u - state) * G;
            const float y = v + state;
            state = y + v;
            u = y;
        }
        return u / std::sqrt (drive);
    }
};

//==============================================================================
// Inviluppo analogico A-D-S-R (release = decay). Retrigger dal livello corrente: niente click.
struct Envelope
{
    enum class Stage { idle, attack, decay, release };
    Stage stage = Stage::idle;
    float level = 0.0f;

    void reset() noexcept   { stage = Stage::idle; level = 0.0f; }
    void gateOn() noexcept  { stage = Stage::attack; }
    void gateOff() noexcept { if (stage != Stage::idle) stage = Stage::release; }
    bool isIdle() const noexcept { return stage == Stage::idle; }

    // Coefficienti one-pole. L'attacco punta a 1.3 (curva RC "analogica") e si ferma a 1.
    float process (float attackCoef, float decayCoef, float sustain) noexcept
    {
        switch (stage)
        {
            case Stage::attack:
                level += (1.3f - level) * attackCoef;
                if (level >= 1.0f) { level = 1.0f; stage = Stage::decay; }
                break;
            case Stage::decay:
                level += (sustain - level) * decayCoef;
                break;
            case Stage::release:
                level += (0.0f - level) * decayCoef;
                if (level < 1.0e-5f) { level = 0.0f; stage = Stage::idle; }
                break;
            case Stage::idle:
                break;
        }
        return level;
    }
};

//==============================================================================
struct Lfo
{
    enum Shape { sine, triangle, ramp, square, sampleHold, drift };

    float phase = 0.0f, held = 0.0f, smoothed = 0.0f;

    void reset() noexcept { phase = 0.0f; held = 0.0f; smoothed = 0.0f; }

    float process (float rateHz, int shape, double fs, Rng& rng) noexcept
    {
        const float fsf = (float) fs;
        phase += rateHz / fsf;
        const bool wrapped = phase >= 1.0f;
        if (wrapped)
        {
            phase -= std::floor (phase);
            held = rng.bipolar();
        }

        float raw = 0.0f;
        float smoothSeconds = 0.002f;   // de-click per le forme discontinue
        switch (shape)
        {
            case sine:       raw = std::sin (kTwoPi * phase);            break;
            case triangle:   raw = 1.0f - 4.0f * std::abs (phase - 0.5f); break;
            case ramp:       raw = 1.0f - 2.0f * phase;                  break;
            case square:     raw = phase < 0.5f ? 1.0f : -1.0f;          break;
            case sampleHold: raw = held;                                 break;
            case drift:      raw = held; smoothSeconds = 0.35f / juce::jmax (0.01f, rateHz); break;
            default:         break;
        }

        smoothed += (raw - smoothed) * (1.0f - std::exp (-1.0f / (smoothSeconds * fsf)));
        return smoothed;
    }
};

//==============================================================================
// Tape delay stereo: tempo modulato (wow), cross-feedback, feedback filtrato e saturato.
struct TapeDelay
{
    std::vector<float> bufL, bufR;
    int   size = 0, writePos = 0;
    float lpL = 0, lpR = 0, hpL = 0, hpR = 0;
    float lpCoef = 0, hpCoef = 0, wowPhase = 0, wowInc = 0;
    double fs = 44100.0;

    void prepare (double sampleRate)
    {
        fs   = sampleRate;
        size = (int) (sampleRate * 2.6) + 8;
        bufL.assign ((size_t) size, 0.0f);
        bufR.assign ((size_t) size, 0.0f);
        lpCoef = 1.0f - std::exp (-kTwoPi * 3800.0f / (float) fs);
        hpCoef = 1.0f - std::exp (-kTwoPi * 90.0f / (float) fs);
        wowInc = 0.31f / (float) fs;
        reset();
    }

    void reset() noexcept
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        writePos = 0;
        lpL = lpR = hpL = hpR = wowPhase = 0.0f;
    }

    float read (const std::vector<float>& b, float delaySamples) const noexcept
    {
        float rp = (float) writePos - delaySamples;
        if (rp < 0.0f) rp += (float) size;
        const int   i1 = (int) rp;
        const float f  = rp - (float) i1;
        const int   i0 = i1 == 0 ? size - 1 : i1 - 1;
        const int   i2 = i1 + 1 >= size ? i1 + 1 - size : i1 + 1;
        const int   i3 = i2 + 1 >= size ? i2 + 1 - size : i2 + 1;
        const float y0 = b[(size_t) i0], y1 = b[(size_t) i1], y2 = b[(size_t) i2], y3 = b[(size_t) i3];
        // Hermite 4 punti
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    void process (float& l, float& r, float timeMs, float feedback, float mix) noexcept
    {
        wowPhase += wowInc;
        if (wowPhase >= 1.0f) wowPhase -= 1.0f;
        const float wow = (std::sin (kTwoPi * wowPhase) + 0.3f * std::sin (kTwoPi * 3.7f * wowPhase))
                          * 0.0009f * (float) fs;

        const float maxD = (float) size - 4.0f;
        const float d    = timeMs * 0.001f * (float) fs;
        const float dl   = read (bufL, juce::jlimit (2.0f, maxD, d + wow));
        const float dr   = read (bufR, juce::jlimit (2.0f, maxD, d * 1.011f - wow));

        const float cross = 0.3f;
        float fl = dl * (1.0f - cross) + dr * cross;
        float fr = dr * (1.0f - cross) + dl * cross;
        lpL += (fl - lpL) * lpCoef;  hpL += (lpL - hpL) * hpCoef;  fl = lpL - hpL;
        lpR += (fr - lpR) * lpCoef;  hpR += (lpR - hpR) * hpCoef;  fr = lpR - hpR;

        bufL[(size_t) writePos] = l + std::tanh (fl * feedback);
        bufR[(size_t) writePos] = r + std::tanh (fr * feedback);
        if (++writePos >= size) writePos = 0;

        l += dl * mix;
        r += dr * mix;
    }
};

//==============================================================================
// "SPACE": FDN a 8 linee (Hadamard) con diffusione in ingresso e linee modulate.
// Il size cambia il tempo di decadimento (0.6 s → 40 s), non le lunghezze: niente click.
struct Space
{
    static constexpr int N = 8, numAllpass = 4;

    std::array<std::vector<float>, N> lines;
    std::array<int, N>   lineSize {}, writePos {};
    std::array<float, N> baseDelay {}, gain {}, damp {}, modPhase {}, modInc {};
    std::array<std::vector<float>, numAllpass> ap;
    std::array<int, numAllpass> apPos {};
    float dampCoef = 0, modDepth = 0, dcL = 0, dcR = 0, dcCoef = 0;
    int   gainCounter = 0;
    double fs = 44100.0;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        static constexpr float lineMs[N]      { 58.3f, 69.7f, 81.1f, 94.9f, 107.3f, 121.9f, 137.1f, 153.7f };
        static constexpr float modHz[N]       { 0.11f, 0.13f, 0.17f, 0.19f, 0.23f, 0.29f, 0.31f, 0.37f };
        static constexpr float apMs[numAllpass] { 4.7f, 3.6f, 12.7f, 9.3f };

        modDepth = 0.0006f * (float) fs;
        for (int i = 0; i < N; ++i)
        {
            baseDelay[(size_t) i] = lineMs[i] * 0.001f * (float) fs;
            lineSize[(size_t) i]  = (int) (baseDelay[(size_t) i] + modDepth) + 8;
            lines[(size_t) i].assign ((size_t) lineSize[(size_t) i], 0.0f);
            modInc[(size_t) i]    = modHz[i] / (float) fs;
            modPhase[(size_t) i]  = (float) i / (float) N;
        }
        for (int i = 0; i < numAllpass; ++i)
            ap[(size_t) i].assign ((size_t) juce::jmax (1, (int) (apMs[i] * 0.001f * (float) fs)), 0.0f);

        dampCoef = 1.0f - std::exp (-kTwoPi * 5200.0f / (float) fs);
        dcCoef   = 1.0f - std::exp (-kTwoPi * 20.0f / (float) fs);
        reset();
        updateGains (0.5f);
    }

    void reset() noexcept
    {
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        for (auto& a : ap)    std::fill (a.begin(), a.end(), 0.0f);
        writePos.fill (0); apPos.fill (0); damp.fill (0.0f);
        dcL = dcR = 0.0f;
        gainCounter = 0;
    }

    void updateGains (float size) noexcept
    {
        const float rt60 = 0.6f * std::pow (40.0f / 0.6f, size);
        for (int i = 0; i < N; ++i)
            gain[(size_t) i] = std::pow (10.0f, -3.0f * baseDelay[(size_t) i] / (rt60 * (float) fs));
    }

    void process (float& l, float& r, float size, float mix) noexcept
    {
        if (--gainCounter <= 0) { gainCounter = 16; updateGains (size); }

        float x = 0.5f * (l + r);
        for (int i = 0; i < numAllpass; ++i)
        {
            auto& b = ap[(size_t) i];
            auto& p = apPos[(size_t) i];
            const float stored = b[(size_t) p];
            const float y = -0.7f * x + stored;
            b[(size_t) p] = x + 0.7f * y;
            if (++p >= (int) b.size()) p = 0;
            x = y;
        }

        std::array<float, N> y {};
        for (int i = 0; i < N; ++i)
        {
            const auto idx = (size_t) i;
            modPhase[idx] += modInc[idx];
            if (modPhase[idx] >= 1.0f) modPhase[idx] -= 1.0f;
            float rp = (float) writePos[idx] - (baseDelay[idx] + modDepth * std::sin (kTwoPi * modPhase[idx]));
            if (rp < 0.0f) rp += (float) lineSize[idx];
            const int   i0 = (int) rp;
            const int   i1 = i0 + 1 >= lineSize[idx] ? 0 : i0 + 1;
            const float f  = rp - (float) i0;
            const float v  = lines[idx][(size_t) i0] + (lines[idx][(size_t) i1] - lines[idx][(size_t) i0]) * f;
            damp[idx] += (v - damp[idx]) * dampCoef;
            y[idx] = damp[idx];
        }

        const float wetL = y[0] - y[2] + y[4] - y[6] + 0.5f * (y[1] - y[7]);
        const float wetR = y[1] - y[3] + y[5] - y[7] + 0.5f * (y[2] - y[4]);

        // Hadamard 8×8 in-place, normalizzato (conserva l'energia)
        auto h = y;
        for (int len = 1; len < N; len <<= 1)
            for (int i = 0; i < N; i += len << 1)
                for (int j = i; j < i + len; ++j)
                {
                    const float a = h[(size_t) j], b = h[(size_t) (j + len)];
                    h[(size_t) j] = a + b;
                    h[(size_t) (j + len)] = a - b;
                }

        const float norm = 1.0f / std::sqrt ((float) N);
        for (int i = 0; i < N; ++i)
        {
            const auto idx = (size_t) i;
            const float inject = (i % 2 == 0 ? x + 0.25f * l : -x + 0.25f * r) * 0.5f;
            lines[idx][(size_t) writePos[idx]] = h[idx] * norm * gain[idx] + inject;
            if (++writePos[idx] >= lineSize[idx]) writePos[idx] = 0;
        }

        dcL += (wetL - dcL) * dcCoef;
        dcR += (wetR - dcR) * dcCoef;
        l += (wetL - dcL) * 0.45f * mix;
        r += (wetR - dcR) * 0.45f * mix;
    }
};

//==============================================================================
// Quantizzazione alla scala: grado più vicino, a parità si scende.
inline int quantizeToScale (float semis, int scale) noexcept
{
    static constexpr bool masks[8][12] {
        { 1,1,1,1,1,1,1,1,1,1,1,1 },   // Chromatic
        { 1,0,1,0,1,1,0,1,0,1,0,1 },   // Major
        { 1,0,1,1,0,1,0,1,1,0,1,0 },   // Minor
        { 1,0,1,1,0,1,0,1,0,1,1,0 },   // Dorian
        { 1,1,0,1,0,1,0,1,1,0,1,0 },   // Phrygian
        { 1,0,0,1,0,1,0,1,0,0,1,0 },   // Pent. Minor
        { 1,0,1,0,1,0,1,0,1,0,1,0 },   // Whole Tone
        { 1,0,1,1,0,0,0,1,1,0,0,0 },   // Hirajoshi
    };
    const auto& m = masks[juce::jlimit (0, 7, scale)];
    const int n = juce::roundToInt (semis);
    auto inScale = [&m] (int v) { return m[((v % 12) + 12) % 12]; };
    for (int d = 0; d < 12; ++d)
    {
        if (inScale (n - d)) return n - d;
        if (inScale (n + d)) return n + d;
    }
    return n;
}
} // namespace mp
