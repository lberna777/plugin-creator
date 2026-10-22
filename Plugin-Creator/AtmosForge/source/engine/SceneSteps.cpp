#include "SceneSteps.h"

namespace af
{

void SceneSteps::prepare (double sampleRate, int)
{
    sr = sampleRate;
    const float fs = (float) sampleRate;
    for (auto& g : grains) g.band.prepare (fs);
    reset();
}

void SceneSteps::reset()
{
    for (auto& g : grains) { g.active = false; g.pending = false; g.band.reset(); }
    for (auto& b : bodies) { b.active = false; b.m1.y1 = b.m1.y2 = b.m2.y1 = b.m2.y2 = b.m3.y1 = b.m3.y2 = 0.0f; }
    samplesToNextStep = 0.0f;
    rightFoot = false;
}

void SceneSteps::setSeed (uint32_t seed)
{
    rng.seed (seed ^ 0x57E90001u);
    for (int i = 0; i < kNumGrains; ++i)
        grains[i].noise.rng.seed (seed ^ (0x57E90100u + (uint32_t) i));
}

SceneSteps::Grain* SceneSteps::freeGrain()
{
    for (auto& g : grains)
        if (! g.active && ! g.pending) return &g;
    return nullptr;
}

void SceneSteps::step (float velocity, bool heavy, int surface)
{
    const float fs = (float) sr;
    const float weight = heavy ? 1.0f : 0.72f;

    // il piede alterna: leggero spostamento stereo, come chi cammina davanti a te
    rightFoot = ! rightFoot;
    stepPan = 0.5f + (rightFoot ? 1.0f : -1.0f) * 0.18f * panSpread + rng.bi() * 0.04f;
    float pL, pR;
    panGains (stepPan, pL, pR);

    // --- parametri per materiale
    int   numGrains   = 2;
    float spreadMs    = 12.0f;     // quanto si allarga la nuvola di grani
    float loHz = 400.0f, hiHz = 2500.0f;
    float decMin = 12.0f, decMax = 40.0f;
    float q = 3.0f;
    float bodyAmt = 0.0f;
    float bodyF1 = 150.0f, bodyF2 = 280.0f, bodyF3 = 520.0f, bodyDecay = 90.0f;
    float level = 1.0f;

    switch ((Surface) surface)
    {
        case Surface::Wood:
            numGrains = 3;  spreadMs = 18.0f; loHz = 300.0f; hiHz = 1800.0f;
            decMin = 15.0f; decMax = 55.0f; q = 2.2f;
            bodyAmt = 0.55f; bodyF1 = rng.range (120.0f, 200.0f); bodyF2 = bodyF1 * rng.range (1.7f, 2.3f);
            bodyF3 = bodyF1 * rng.range (3.1f, 4.2f); bodyDecay = rng.range (70.0f, 150.0f);
            level = 1.0f;
            break;

        case Surface::Gravel:
            numGrains = rng.intRange (16, 34); spreadMs = rng.range (90.0f, 170.0f);
            loHz = 1800.0f; hiHz = 7500.0f; decMin = 1.5f; decMax = 14.0f; q = 7.0f;
            bodyAmt = 0.12f; bodyF1 = rng.range (90.0f, 150.0f); bodyDecay = 45.0f;
            level = 0.85f;
            break;

        case Surface::Snow:
            numGrains = rng.intRange (10, 22); spreadMs = rng.range (70.0f, 130.0f);
            loHz = 2600.0f; hiHz = 9000.0f; decMin = 2.0f; decMax = 16.0f; q = 4.5f;
            bodyAmt = 0.18f; bodyF1 = rng.range (70.0f, 120.0f); bodyDecay = 35.0f;   // neve compressa sotto
            level = 0.8f;
            break;

        case Surface::Concrete:
            numGrains = 2; spreadMs = 8.0f; loHz = 900.0f; hiHz = 4200.0f;
            decMin = 3.0f; decMax = 18.0f; q = 3.5f;
            bodyAmt = 0.15f; bodyF1 = rng.range (180.0f, 300.0f); bodyDecay = 30.0f;
            level = 0.95f;
            break;

        case Surface::Metal:
            numGrains = 2; spreadMs = 10.0f; loHz = 1200.0f; hiHz = 6000.0f;
            decMin = 4.0f; decMax = 20.0f; q = 4.0f;
            bodyAmt = 0.7f; bodyF1 = rng.range (700.0f, 1100.0f); bodyF2 = bodyF1 * rng.range (1.9f, 2.6f);
            bodyF3 = bodyF1 * rng.range (3.4f, 4.8f); bodyDecay = rng.range (220.0f, 600.0f);
            level = 0.9f;
            break;

        case Surface::numSurfaces:
        default: break;
    }

    // --- nuvola di grani (il primo è il tallone: sempre a 0 ms e più forte)
    for (int i = 0; i < numGrains; ++i)
    {
        auto* gp = freeGrain();
        if (gp == nullptr) break;
        auto& g = *gp;

        const bool heel = (i == 0);
        const float t = heel ? 0.0f : rng.uni() * rng.uni() * spreadMs;   // addensati all'inizio
        g.delaySamples = t * 0.001f * fs;
        g.band.setCutoff (rng.range (loHz, hiHz), q * rng.range (0.7f, 1.4f));
        g.env.setTimes (0.15f, rng.range (decMin, decMax), fs);
        g.amp = velocity * weight * level * (heel ? 1.0f : rng.range (0.12f, 0.5f)) / std::sqrt ((float) numGrains) * 2.2f;
        g.gL = pL; g.gR = pR;
        g.pending = true;
        g.active = false;
    }

    // --- corpo risonante
    if (bodyAmt > 0.001f)
    {
        auto& b = bodies[nextBody];
        nextBody = (nextBody + 1) % kNumBodies;
        b.m1.set (bodyF1, bodyDecay, fs);
        b.m2.set (bodyF2, bodyDecay * 0.7f, fs);
        b.m3.set (bodyF3, bodyDecay * 0.5f, fs);
        const float a = velocity * weight * bodyAmt * 0.6f;
        b.m1.strike (a);
        b.m2.strike (a * 0.5f);
        b.m3.strike (a * 0.25f);
        b.amount = 1.0f;
        b.gL = pL; b.gR = pR;
        b.active = true;
    }
}

void SceneSteps::trigger (float velocity, bool big) { step (velocity, big, lastSurface); }

void SceneSteps::render (float* left, float* right, int n, const SceneParams& p)
{
    const float fs = (float) sr;
    panSpread = p.width;
    lastSurface = p.surface;

    const float distance = p.depth;
    const float evtGain  = lerp (2.0f, 0.8f, distance) * lerp (0.7f, 1.15f, p.tone);

    // andatura: da passeggiata lenta (~50 passi/min) a corsa (~190)
    const float stepsPerSec = lerp (50.0f, 190.0f, p.motion) / 60.0f;
    const float heavyProb = p.density;   // "density" qui = peso/decisione del passo

    for (int i = 0; i < n; ++i)
    {
        if (p.autoGen)
        {
            samplesToNextStep -= 1.0f;
            if (samplesToNextStep <= 0.0f)
            {
                // il passo umano è regolare ma non a metronomo: ±8%
                samplesToNextStep = (fs / stepsPerSec) * rng.range (0.92f, 1.08f);
                step (rng.range (0.6f, 1.0f) * lerp (0.6f, 1.0f, p.density),
                      rng.uni() < heavyProb, p.surface);
            }
        }

        float l = 0.0f, r = 0.0f;

        for (auto& g : grains)
        {
            if (g.pending)
            {
                g.delaySamples -= 1.0f;
                if (g.delaySamples <= 0.0f) { g.pending = false; g.active = true; g.env.trigger (g.amp); }
                else continue;
            }
            if (! g.active) continue;

            const float e = g.env.next();
            if (! g.env.isActive()) { g.active = false; continue; }
            const float s = g.band.bandpass (g.noise.next()) * e * evtGain;
            l += s * g.gL;
            r += s * g.gR;
        }

        for (auto& b : bodies)
        {
            if (! b.active) continue;
            if (! b.m1.isActive() && ! b.m2.isActive() && ! b.m3.isActive()) { b.active = false; continue; }
            const float s = (b.m1.next() + b.m2.next() * 0.6f + b.m3.next() * 0.35f) * evtGain;
            l += s * b.gL;
            r += s * b.gR;
        }

        left[i]  += l;
        right[i] += r;
    }
}

} // namespace af
