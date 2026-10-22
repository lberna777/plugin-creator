#include "Engine.h"

namespace af
{

Engine::Engine() {}

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    road.prepare (sampleRate, maxBlockSize);
    fire.prepare (sampleRate, maxBlockSize);
    weather.prepare (sampleRate, maxBlockSize);
    steps.prepare (sampleRate, maxBlockSize);
    dcL.prepare ((float) sampleRate); dcR.prepare ((float) sampleRate);
    setSeed (currentSeed);
}

void Engine::reset()
{
    road.reset(); fire.reset(); weather.reset(); steps.reset();
    dcL.reset(); dcR.reset();
}

void Engine::setScene (SceneId s)
{
    if (s == current) return;
    current = s;
    active()->reset();   // la scena che entra parte pulita: niente code della precedente
}

void Engine::setSeed (uint32_t seed)
{
    currentSeed = seed;
    road.setSeed (seed); fire.setSeed (seed); weather.setSeed (seed); steps.setSeed (seed);
}

Scene* Engine::active() noexcept
{
    switch (current)
    {
        case SceneId::Road:    return &road;
        case SceneId::Fire:    return &fire;
        case SceneId::Weather: return &weather;
        case SceneId::Steps:   return &steps;
        case SceneId::numScenes:
        default:               return &fire;
    }
}

void Engine::triggerNote (int midiNote, float velocity)
{
    // sotto il DO centrale: versione "grossa" dell'evento (tuono, camion, ciocco, passo pesante)
    active()->trigger (velocity, midiNote < 60);
}

void Engine::render (float* left, float* right, int n, const SceneParams& p)
{
    for (int i = 0; i < n; ++i) { left[i] = 0.0f; right[i] = 0.0f; }
    active()->render (left, right, n, p);

    // niente continua né sub inudibile in uscita: solo headroom sprecato
    for (int i = 0; i < n; ++i) { left[i] = dcL.process (left[i]); right[i] = dcR.process (right[i]); }
}

} // namespace af
