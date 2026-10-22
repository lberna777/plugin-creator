#include "SceneRoad.h"

namespace af
{

// Aggiornamento dei parametri "di scena" (Doppler, distanza, filtri) ogni 32 campioni:
// il movimento è lento rispetto all'audio, e così tan()/sin() non stanno nel loop stretto.
static constexpr int kCtrlBlock = 32;
static constexpr float kSpeedOfSound = 343.0f;

void SceneRoad::prepare (double sampleRate, int)
{
    sr = sampleRate;
    const float fs = (float) sampleRate;

    rumbleLp.prepare (fs);
    for (auto& v : vehicles)
    {
        v.tyreBp.prepare (fs);
        v.engineLp.prepare (fs);
        v.rough.setTimeMs (25.0f, fs);
    }
    reset();
}

void SceneRoad::reset()
{
    rumbleLp.reset();
    for (auto& v : vehicles) { v.active = false; v.tyreBp.reset(); v.engineLp.reset(); v.rough.reset(); }
    for (auto& h : horns) h.active = false;
    samplesToNextVehicle = 0.0f;
    samplesToNextHorn = (float) sr * 8.0f;
}

void SceneRoad::setSeed (uint32_t seed)
{
    rng.seed (seed ^ 0x20AD0001u);
    rumbleNoise.rng.seed (seed ^ 0x20AD0002u);
    for (int i = 0; i < kNumVehicles; ++i)
    {
        vehicles[i].tyreNoise.rng.seed (seed ^ (0x20AD0100u + (uint32_t) i));
        vehicles[i].roughNoise.rng.seed (seed ^ (0x20AD0200u + (uint32_t) i));
    }
}

void SceneRoad::spawnVehicle (float velocity, bool heavy)
{
    Vehicle* slot = nullptr;
    for (auto& v : vehicles)
        if (! v.active) { slot = &v; break; }
    if (slot == nullptr) return;          // strada piena: l'evento si perde, meglio che rubare una voce

    auto& v = *slot;
    const bool leftToRight = rng.uni() < 0.5f;

    v.heavy = heavy;
    v.speed = (heavy ? rng.range (11.0f, 19.0f) : rng.range (13.0f, 27.0f)) * (leftToRight ? 1.0f : -1.0f);
    v.x = leftToRight ? -70.0f : 70.0f;
    v.y = heavy ? rng.range (5.0f, 11.0f) : rng.range (3.0f, 12.0f);
    v.baseFreq = heavy ? rng.range (38.0f, 58.0f) : rng.range (72.0f, 125.0f);
    v.amp = velocity * (heavy ? rng.range (0.9f, 1.2f) : rng.range (0.55f, 0.95f));
    v.tyreAmount = heavy ? rng.range (0.7f, 1.0f) : rng.range (0.4f, 0.8f);
    v.h1.phase = rng.uni() * kTwoPi;
    v.h2.phase = rng.uni() * kTwoPi;
    v.h3.phase = rng.uni() * kTwoPi;
    v.dist = std::sqrt (v.x * v.x + v.y * v.y);
    panGains (0.5f + (v.x > 0.0f ? 0.5f : -0.5f) * panSpread, v.gL, v.gR);
    v.active = true;
}

void SceneRoad::fireHorn (float velocity)
{
    auto& h = horns[nextHorn];
    nextHorn = (nextHorn + 1) % 3;

    const float f = rng.range (330.0f, 500.0f);
    h.a.setFreq (f, (float) sr);
    h.b.setFreq (f * rng.range (1.18f, 1.26f), (float) sr);   // il clacson è due trombe stonate
    h.env.setTimes (18.0f, rng.range (280.0f, 900.0f), (float) sr);
    h.env.trigger (velocity * 0.45f);
    panGains (0.5f + rng.bi() * 0.5f * panSpread, h.gL, h.gR);
    h.active = true;
}

void SceneRoad::trigger (float velocity, bool big)
{
    if (big) spawnVehicle (velocity, true);
    else     fireHorn (velocity);
}

void SceneRoad::render (float* left, float* right, int n, const SceneParams& p)
{
    const float fs = (float) sr;
    panSpread = p.width;

    const float distance = p.depth;
    const float bright   = 0.3f + 0.7f * p.tone;

    rumbleLp.setCutoff (lerp (90.0f, 240.0f, bright) * lerp (1.0f, 0.6f, distance), 0.7f);
    const float rumbleGain = lerp (0.05f, 0.16f, p.density) * lerp (1.0f, 0.6f, distance);
    const float evtGain    = lerp (1.0f, 0.42f, distance);

    // densità → transiti al secondo; motion → quanto vanno veloci (scala la velocità)
    const float passPerSec = lerp (0.12f, 2.2f, p.density * p.density);
    const float speedScale = lerp (0.45f, 1.9f, p.motion);
    const float hornPerSec = lerp (0.0f, 0.12f, p.density);
    const float wetRoad    = (p.surface == (int) Surface::Gravel) ? 1.0f : 0.0f;  // ghiaia = asfalto ruvido

    int ctrl = 0;
    for (int i = 0; i < n; ++i)
    {
        // --- scheduling
        if (p.autoGen)
        {
            samplesToNextVehicle -= 1.0f;
            while (samplesToNextVehicle <= 0.0f)
            {
                const float u = std::max (rng.uni(), 1.0e-6f);
                samplesToNextVehicle += (-std::log (u) / passPerSec) * fs;
                spawnVehicle (rng.range (0.6f, 1.0f), rng.uni() < 0.22f);
            }
            if (hornPerSec > 0.001f)
            {
                samplesToNextHorn -= 1.0f;
                if (samplesToNextHorn <= 0.0f)
                {
                    const float u = std::max (rng.uni(), 1.0e-6f);
                    samplesToNextHorn = (-std::log (u) / hornPerSec) * fs;
                    fireHorn (rng.range (0.5f, 1.0f));
                }
            }
        }

        float l = 0.0f, r = 0.0f;

        // --- rombo di fondo
        const float rumble = rumbleLp.lowpass (rumbleNoise.next()) * rumbleGain;
        l += rumble * 0.98f;
        r += rumble * 1.02f;

        const bool doCtrl = (ctrl == 0);
        ctrl = (ctrl + 1) % kCtrlBlock;

        // --- veicoli in transito
        for (auto& v : vehicles)
        {
            if (! v.active) continue;

            if (doCtrl)
            {
                const float vx = v.speed * speedScale;
                v.x += vx * (float) kCtrlBlock / fs;
                if ((vx > 0.0f && v.x > 75.0f) || (vx < 0.0f && v.x < -75.0f)) { v.active = false; continue; }

                const float dist = std::sqrt (v.x * v.x + v.y * v.y);
                // velocità radiale: >0 se si avvicina → Doppler verso l'alto
                const float radial = -(v.x * vx) / std::max (dist, 0.5f);
                const float dop = kSpeedOfSound / std::max (kSpeedOfSound - radial, 40.0f);

                v.h1.setFreq (v.baseFreq * dop, fs);
                v.h2.setFreq (v.baseFreq * 2.0f * dop, fs);
                v.h3.setFreq (v.baseFreq * 3.0f * dop, fs);

                // più è lontano, più l'aria si mangia gli acuti
                const float air = 1.0f / (1.0f + dist * 0.06f);
                v.engineLp.setCutoff (lerp (300.0f, 2600.0f, bright) * air + 120.0f, 0.9f);
                v.tyreBp.setCutoff (lerp (700.0f, 3200.0f, bright) * air + 250.0f, 1.1f);

                v.dist = dist;
                // panning geometrico: il lato dipende da dove si trova rispetto all'ascoltatore
                const float lateral = std::max (-1.0f, std::min (1.0f, v.x / (v.y * 2.0f + 5.0f)));
                panGains (0.5f + lateral * 0.5f * panSpread, v.gL, v.gR);
            }

            const float atten = v.amp / (1.0f + (v.dist / 6.0f) * (v.dist / 6.0f) * 0.55f);
            if (atten < 1.0e-4f) continue;

            // motore: tre armoniche con un filo di irregolarità
            const float rough = 0.85f + 0.3f * v.rough.process (v.roughNoise.next());
            float engine = v.h1.next() * 0.55f + v.h2.next() * 0.28f + v.h3.next() * 0.14f;
            engine = v.engineLp.lowpass (engine * rough) * (v.heavy ? 0.9f : 0.6f);

            // pneumatici: rumore di rotolamento, più forte su strada ruvida/bagnata
            const float tyre = v.tyreBp.bandpass (v.tyreNoise.next())
                             * v.tyreAmount * (0.8f + 0.6f * wetRoad);

            const float s = (engine + tyre) * atten * evtGain;
            l += s * v.gL;
            r += s * v.gR;
        }

        // --- clacson
        for (auto& h : horns)
        {
            if (! h.active) continue;
            const float e = h.env.next();
            if (! h.env.isActive()) { h.active = false; continue; }
            const float s = (h.a.next() * 0.6f + h.b.next() * 0.5f) * e * evtGain;
            l += s * h.gL;
            r += s * h.gR;
        }

        left[i]  += l;
        right[i] += r;
    }
}

} // namespace af
