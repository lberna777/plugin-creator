#pragma once
#include "Scene.h"

namespace af
{

// STRADA — il suono di un'auto che passa è tutto nel movimento:
//   posizione lungo la strada → distanza → volume, panning, brillantezza e Doppler.
//   1) rombo di fondo della città: brown noise passa-basso
//   2) veicoli in transito: motore (armoniche) + pneumatici (rumore filtrato), entrambi
//      spostati in frequenza dal Doppler e attenuati dalla distanza
//   3) clacson occasionale (o su nota MIDI)
class SceneRoad final : public Scene
{
public:
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setSeed (uint32_t seed) override;
    void render (float* left, float* right, int n, const SceneParams& p) override;
    void trigger (float velocity, bool big) override;

private:
    struct Vehicle
    {
        // geometria: x = posizione lungo la strada in metri, y = distanza laterale
        float x = -120.0f, y = 8.0f, speed = 20.0f;   // m/s
        float baseFreq = 90.0f;                        // frequenza di base del motore
        float amp = 1.0f, tyreAmount = 0.6f;
        float dist = 100.0f, gL = 0.7f, gR = 0.7f;   // ricalcolati a control-rate
        bool  heavy = false, active = false;

        SineOsc h1, h2, h3;
        SvfTpt  tyreBp, engineLp;
        WhiteNoise tyreNoise;
        OnePole rough;         // irregolarità del motore
        WhiteNoise roughNoise;
    };

    struct Horn
    {
        SineOsc a, b;
        AREnv   env;
        float   gL = 0.7f, gR = 0.7f;
        bool    active = false;
    };

    static constexpr int kNumVehicles = 6;

    void spawnVehicle (float velocity, bool heavy);
    void fireHorn (float velocity);

    Vehicle vehicles[kNumVehicles];
    Horn    horns[3];
    int     nextHorn = 0;

    BrownNoise rumbleNoise;
    SvfTpt     rumbleLp;
    Rng        rng;

    float samplesToNextVehicle = 0.0f;
    float samplesToNextHorn = 0.0f;
    float panSpread = 0.7f;
};

} // namespace af
