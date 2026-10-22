#include "SceneWeather.h"

namespace af
{

void SceneWeather::prepare (double sampleRate, int)
{
    sr = sampleRate;
    const float fs = (float) sampleRate;

    bedHp.prepare (fs); bedLp.prepare (fs);
    windBp.prepare (fs); windMoveLp.prepare (fs);
    windSmooth.setTimeMs (900.0f, fs);
    windAmpSmooth.setTimeMs (600.0f, fs);

    for (auto& d : drops) d.band.prepare (fs);
    thunder.lp.prepare (fs); thunder.rumbleBp.prepare (fs);

    reset();
}

void SceneWeather::reset()
{
    bedHp.reset(); bedLp.reset(); windBp.reset(); windMoveLp.reset();
    windSmooth.reset (0.0f); windAmpSmooth.reset (0.3f);
    for (auto& d : drops) { d.active = false; d.band.reset(); }
    thunder.active = false; thunder.lp.reset(); thunder.rumbleBp.reset();
    samplesToNextDrop = 0.0f;
    samplesToNextThunder = (float) sr * 12.0f;
}

void SceneWeather::setSeed (uint32_t seed)
{
    rng.seed (seed ^ 0x2A1E0001u);
    bedNoise.rng.seed (seed ^ 0x2A1E0002u);
    softNoise.rng.seed (seed ^ 0x2A1E0003u);
    windNoise.rng.seed (seed ^ 0x2A1E0004u);
    windMoveNoise.rng.seed (seed ^ 0x2A1E0005u);
    thunder.noise.rng.seed (seed ^ 0x2A1E0006u);
    for (int i = 0; i < kNumDrops; ++i)
        drops[i].noise.rng.seed (seed ^ (0x2A1E0100u + (uint32_t) i));
}

void SceneWeather::fireDrop (float velocity, bool big)
{
    auto& d = drops[nextDrop];
    nextDrop = (nextDrop + 1) % kNumDrops;

    const float fs = (float) sr;
    if (big)   // goccia grossa che cade su una superficie: più bassa e più lunga
    {
        d.band.setCutoff (rng.range (600.0f, 1800.0f), rng.range (6.0f, 16.0f));
        d.env.setTimes (0.2f, rng.range (12.0f, 45.0f), fs);
        d.env.trigger (velocity * rng.range (0.5f, 1.0f));
    }
    else
    {
        d.band.setCutoff (rng.range (1800.0f, 9000.0f), rng.range (8.0f, 26.0f));
        d.env.setTimes (0.1f, rng.range (1.5f, 12.0f), fs);
        d.env.trigger (velocity * rng.range (0.15f, 0.8f));
    }
    panGains (0.5f + rng.bi() * 0.5f * panSpread, d.gL, d.gR);
    d.active = true;
}

void SceneWeather::fireThunder (float velocity)
{
    const float fs = (float) sr;
    thunder.env.setTimes (rng.range (120.0f, 400.0f), rng.range (1800.0f, 4500.0f), fs);
    thunder.lp.setCutoff (rng.range (180.0f, 420.0f), 0.8f);
    thunder.rumbleBp.setCutoff (rng.range (45.0f, 95.0f), 1.6f);
    thunder.env.trigger (velocity);
    panGains (0.5f + rng.bi() * 0.35f, thunder.gL, thunder.gR);
    thunder.active = true;
}

void SceneWeather::trigger (float velocity, bool big)
{
    if (big) fireThunder (velocity);
    else     fireDrop (velocity, false);
}

void SceneWeather::render (float* left, float* right, int n, const SceneParams& p)
{
    const float fs = (float) sr;
    panSpread = p.width;

    // tone: 0 = neve (tutto ovattato, niente gocce), 1 = pioggia battente
    const float wet      = p.tone;
    const float distance = p.depth;

    // letto: in pioggia una banda larga e brillante; in neve un fruscio bassissimo
    bedHp.setCutoff (lerp (180.0f, 380.0f, wet) * lerp (1.0f, 0.5f, distance), 0.7f);
    bedLp.setCutoff (lerp (1400.0f, 11000.0f, wet) * lerp (1.0f, 0.4f, distance), 0.7f);

    const float bedGain  = lerp (0.05f, 0.30f, wet) * lerp (0.35f, 1.0f, p.density);
    const float softGain = lerp (0.16f, 0.03f, wet) * lerp (0.4f, 1.0f, p.density);  // manto nevoso
    const float dropGain = wet * lerp (1.0f, 0.4f, distance);
    const float windGain = lerp (0.03f, 0.26f, p.motion) * lerp (1.0f, 0.7f, distance);

    // gocce: quante al secondo (in neve tendono a zero)
    const float dropsPerSec = lerp (2.0f, 160.0f, p.density * p.density) * wet;
    // tuoni: solo con pioggia forte, intervallo medio 10..60 s
    const float thunderPerSec = (wet > 0.55f && p.density > 0.5f)
                                ? lerp (0.008f, 0.09f, (p.density - 0.5f) * 2.0f) : 0.0f;

    for (int i = 0; i < n; ++i)
    {
        if (p.autoGen)
        {
            if (dropsPerSec > 0.01f)
            {
                samplesToNextDrop -= 1.0f;
                while (samplesToNextDrop <= 0.0f)
                {
                    const float u = std::max (rng.uni(), 1.0e-6f);
                    samplesToNextDrop += (-std::log (u) / dropsPerSec) * fs;
                    fireDrop (rng.range (0.3f, 1.0f), rng.uni() < 0.12f);
                }
            }
            if (thunderPerSec > 0.0f)
            {
                samplesToNextThunder -= 1.0f;
                if (samplesToNextThunder <= 0.0f)
                {
                    const float u = std::max (rng.uni(), 1.0e-6f);
                    samplesToNextThunder = (-std::log (u) / thunderPerSec) * fs;
                    fireThunder (rng.range (0.5f, 1.0f));
                }
            }
        }

        // --- letto di precipitazione (stereo decorrelato: due estrazioni indipendenti)
        const float bn = bedNoise.next();
        float bed = bedLp.lowpass (bedHp.highpass (bn)) * bedGain
                  + softNoise.next() * softGain;

        // --- vento: banda stretta che vaga, ampiezza che respira
        windMoveLp.setCutoff (0.35f + 1.2f * p.motion, 0.7f);
        const float walk = windSmooth.process (windMoveLp.lowpass (windMoveNoise.next()) * 8.0f);
        const float windCentre = lerp (280.0f, 1600.0f, std::min (1.0f, std::max (0.0f, 0.5f + walk)));
        windBp.setCutoff (windCentre, 1.8f);
        const float windAmp = 0.45f + 0.55f * windAmpSmooth.process (std::abs (walk) * 2.0f);
        const float wind = windBp.bandpass (windNoise.next()) * windGain * std::min (windAmp, 1.6f);

        float l = bed + wind * 0.9f;
        float r = bed * 0.96f + wind * 1.05f;

        // --- gocce
        for (auto& d : drops)
        {
            if (! d.active) continue;
            const float e = d.env.next();
            if (! d.env.isActive()) { d.active = false; continue; }
            const float s = d.band.bandpass (d.noise.next()) * e * dropGain;
            l += s * d.gL;
            r += s * d.gR;
        }

        // --- tuono
        if (thunder.active)
        {
            const float e = thunder.env.next();
            if (! thunder.env.isActive()) thunder.active = false;
            const float nse = thunder.noise.next();
            const float s = (thunder.lp.lowpass (nse) * 0.8f + thunder.rumbleBp.bandpass (nse) * 1.4f) * e;
            l += s * thunder.gL;
            r += s * thunder.gR;
        }

        left[i]  += l;
        right[i] += r;
    }
}

} // namespace af
