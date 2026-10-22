#pragma once
#include "SceneRoad.h"
#include "SceneFire.h"
#include "SceneWeather.h"
#include "SceneSteps.h"

namespace af
{

enum class SceneId { Road = 0, Fire, Weather, Steps, numScenes };

// Il motore: sceglie la scena attiva, la rende, applica larghezza stereo e livello,
// e tiene i valori di picco per la UI. Nessuna allocazione dopo prepare().
// La stessa classe serve sia al processing in tempo reale sia al render offline in WAV.
class Engine
{
public:
    Engine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setScene (SceneId s);
    SceneId getScene() const noexcept { return current; }
    void setSeed (uint32_t seed);

    // Rende n campioni SOVRASCRIVENDO left/right, senza livello né larghezza:
    // quelli li applica il chiamante campione per campione (valori smussati).
    void render (float* left, float* right, int n, const SceneParams& p);

    // Evento su richiesta (nota MIDI): note basse = evento "grosso".
    void triggerNote (int midiNote, float velocity);

private:
    Scene* active() noexcept;

    SceneRoad    road;
    SceneFire    fire;
    SceneWeather weather;
    SceneSteps   steps;

    DcBlock  dcL, dcR;
    SceneId  current = SceneId::Fire;
    uint32_t currentSeed = 1234;
    double   sr = 44100.0;
};

} // namespace af
