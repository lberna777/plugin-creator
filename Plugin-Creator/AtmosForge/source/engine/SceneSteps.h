#pragma once
#include "Scene.h"

namespace af
{

// PASSI — un passo non è un colpo solo: è tallone + punta, e il materiale decide tutto.
//   legno    → botta smorzata + due modi risonanti bassi
//   ghiaia   → nuvola di micro-grani sparsi su ~150 ms
//   neve     → scricchiolio acuto e compresso, coda corta, niente riverbero
//   cemento  → click secco, quasi niente coda
//   metallo  → modi lunghi e metallici sopra il colpo
// "motion" = andatura (passi al minuto), "density" = peso/decisione del passo.
class SceneSteps final : public Scene
{
public:
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setSeed (uint32_t seed) override;
    void render (float* left, float* right, int n, const SceneParams& p) override;
    void trigger (float velocity, bool big) override;

private:
    // Un "grano": burst di rumore filtrato con inviluppo brevissimo, eventualmente ritardato.
    struct Grain
    {
        AREnv  env;
        SvfTpt band;
        WhiteNoise noise;
        float gL = 0.7f, gR = 0.7f;
        float delaySamples = 0.0f;    // attesa prima di partire (tallone → punta, grani di ghiaia)
        float amp = 1.0f;
        bool  pending = false, active = false;
    };

    struct Body      // corpo risonante del passo (asse di legno, lamiera)
    {
        ModalRes m1, m2, m3;
        float gL = 0.7f, gR = 0.7f;
        float amount = 0.0f;
        bool  active = false;
    };

    static constexpr int kNumGrains = 96;
    static constexpr int kNumBodies = 6;

    void step (float velocity, bool heavy, int surface);
    Grain* freeGrain();

    Grain grains[kNumGrains];
    Body  bodies[kNumBodies];
    int   nextBody = 0;

    Rng   rng;
    float samplesToNextStep = 0.0f;
    float panSpread = 0.7f;
    float stepPan = 0.5f;      // alterna leggermente destra/sinistra: piede destro, piede sinistro
    int   lastSurface = 0;     // materiale corrente, per i passi innescati da MIDI
    bool  rightFoot = false;
};

} // namespace af
