#include "SceneFire.h"

namespace af
{

void SceneFire::prepare (double sampleRate, int)
{
    sr = sampleRate;
    const float fs = (float) sampleRate;

    roarLp.prepare (fs);
    hissHp.prepare (fs);
    breathLp.prepare (fs);
    breathSmooth.setTimeMs (180.0f, fs);

    for (auto& c : crackles)
        c.band.prepare (fs);

    reset();
}

void SceneFire::reset()
{
    roarLp.reset(); hissHp.reset(); breathLp.reset(); breathSmooth.reset (0.5f);
    for (auto& c : crackles) { c.active = false; c.band.reset(); c.body.y1 = c.body.y2 = 0.0f; }
    samplesToNext = 0.0f;
}

void SceneFire::setSeed (uint32_t seed)
{
    rng.seed (seed ^ 0xF17E0001u);
    roarNoise.rng.seed (seed ^ 0xF17E0002u);
    hissNoise.rng.seed (seed ^ 0xF17E0003u);
    breathNoise.rng.seed (seed ^ 0xF17E0004u);
    for (int i = 0; i < kNumCrackles; ++i)
        crackles[i].noise.rng.seed (seed ^ (0xF17E0100u + (uint32_t) i));
}

void SceneFire::fire (float velocity, bool big)
{
    auto& c = crackles[nextCrackle];
    nextCrackle = (nextCrackle + 1) % kNumCrackles;

    const float fs = (float) sr;
    if (big)
    {
        // schiocco: banda bassa larga + corpo di legno che risuona
        c.band.setCutoff (rng.range (220.0f, 900.0f), rng.range (1.5f, 4.0f));
        c.env.setTimes (0.4f, rng.range (45.0f, 140.0f), fs);
        c.body.set (rng.range (90.0f, 260.0f), rng.range (60.0f, 180.0f), fs);
        c.bodyAmount = rng.range (0.25f, 0.6f);
        c.body.strike (velocity * c.bodyAmount);
        c.env.trigger (velocity * rng.range (0.6f, 1.0f));
    }
    else
    {
        c.band.setCutoff (rng.range (900.0f, 6500.0f), rng.range (3.0f, 14.0f));
        c.env.setTimes (0.2f, rng.range (2.5f, 35.0f), fs);
        c.bodyAmount = 0.0f;
        c.env.trigger (velocity * rng.range (0.2f, 1.0f));
    }
    panGains (0.5f + rng.bi() * 0.5f * panSpread, c.gL, c.gR);
    c.active = true;
}

void SceneFire::trigger (float velocity, bool big) { fire (velocity, big); }

void SceneFire::render (float* left, float* right, int n, const SceneParams& p)
{
    const float fs = (float) sr;

    // timbro: più "tone" = fiamma viva e brillante; più "depth" = fuoco lontano/ovattato
    const float bright   = 0.25f + 0.75f * p.tone;
    const float distance = p.depth;
    const float roarCut  = lerp (140.0f, 520.0f, bright) * lerp (1.0f, 0.55f, distance);
    const float hissCut  = lerp (1800.0f, 5200.0f, bright) * lerp (1.0f, 0.45f, distance);
    roarLp.setCutoff (roarCut, 0.7f);
    hissHp.setCutoff (hissCut, 0.7f);
    breathLp.setCutoff (2.5f, 0.7f);   // random walk lentissimo = respiro della fiamma

    const float roarGain = lerp (0.42f, 0.22f, distance);
    const float hissGain = lerp (0.10f, 0.04f, distance) * bright;
    const float evtGain  = lerp (1.0f, 0.45f, distance);

    // densità → intervallo medio fra crepitii (Poisson: intervalli esponenziali)
    const float perSec = lerp (1.5f, 45.0f, p.density * p.density);
    const float bigProb = 0.06f + 0.10f * (1.0f - p.tone);
    panSpread = p.width;

    for (int i = 0; i < n; ++i)
    {
        // --- schedulazione eventi
        if (p.autoGen)
        {
            samplesToNext -= 1.0f;
            while (samplesToNext <= 0.0f)
            {
                const float u = std::max (rng.uni(), 1.0e-6f);
                samplesToNext += (-std::log (u) / perSec) * fs;
                fire (rng.range (0.35f, 1.0f), rng.uni() < bigProb);
            }
        }

        // --- letto: soffio + sibilo
        const float breath = 0.55f + 0.45f * breathSmooth.process (breathLp.lowpass (breathNoise.next()) * 6.0f);
        float bed = roarLp.lowpass (roarNoise.next()) * roarGain * std::min (breath, 1.4f)
                  + hissHp.highpass (hissNoise.next()) * hissGain;

        // il letto è quasi mono (decorrelato solo di poco): il fuoco sta lì, fermo
        float l = bed * 0.98f;
        float r = bed * 1.02f;

        // --- crepitii
        for (auto& c : crackles)
        {
            if (! c.active) continue;
            const float e = c.env.next();
            if (! c.env.isActive() && ! (c.bodyAmount > 0.0f && c.body.isActive()))
            {
                c.active = false;
                continue;
            }
            float s = c.band.bandpass (c.noise.next()) * e;
            if (c.bodyAmount > 0.0f) s += c.body.next() * 0.8f;
            s *= evtGain;
            l += s * c.gL;
            r += s * c.gR;
        }

        left[i]  += l;
        right[i] += r;
    }
}

} // namespace af
