#pragma once
#include "Scene.h"

namespace af
{

// PIOGGIA / NEVE — "tone" fa da meteo: 0 = nevicata ovattata, 1 = temporale.
//   1) letto di precipitazione: rumore filtrato (in neve quasi tutto tolto, resta l'ovattato)
//   2) gocce singole: micro-burst risonanti sparsi in stereo (solo lato pioggia)
//   3) vento: banda stretta che si muove, pilotata da "motion"
//   4) tuono: raro, boato passa-basso con coda lunga (anche da MIDI, nota grave)
class SceneWeather final : public Scene
{
public:
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setSeed (uint32_t seed) override;
    void render (float* left, float* right, int n, const SceneParams& p) override;
    void trigger (float velocity, bool big) override;

private:
    struct Drop
    {
        AREnv  env;
        SvfTpt band;
        WhiteNoise noise;
        float gL = 0.7f, gR = 0.7f;
        bool  active = false;
    };

    struct Thunder
    {
        AREnv  env;
        SvfTpt lp, rumbleBp;
        BrownNoise noise;
        float gL = 0.7f, gR = 0.7f;
        bool  active = false;
    };

    static constexpr int kNumDrops = 40;

    void fireDrop (float velocity, bool big);
    void fireThunder (float velocity);

    Drop    drops[kNumDrops];
    Thunder thunder;
    int     nextDrop = 0;

    WhiteNoise bedNoise, windNoise;
    PinkNoise  softNoise;
    SvfTpt     bedHp, bedLp, windBp, windMoveLp;
    WhiteNoise windMoveNoise;
    OnePole    windSmooth, windAmpSmooth;
    Rng        rng;

    float samplesToNextDrop = 0.0f;
    float samplesToNextThunder = 0.0f;
    float panSpread = 0.7f;
};

} // namespace af
