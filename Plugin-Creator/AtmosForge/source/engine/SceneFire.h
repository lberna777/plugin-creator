#pragma once
#include "Scene.h"

namespace af
{

// CAMINO — tre strati:
//   1) soffio della fiamma: brown noise passa-basso, respiro lento e irregolare
//   2) sibilo della legna che asciuga: pink noise filtrato in alto
//   3) crepitii: burst brevissimi risonanti; ogni tanto uno schiocco con corpo (ciocco)
class SceneFire final : public Scene
{
public:
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setSeed (uint32_t seed) override;
    void render (float* left, float* right, int n, const SceneParams& p) override;
    void trigger (float velocity, bool big) override;

private:
    struct Crackle
    {
        AREnv    env;
        SvfTpt   band;
        ModalRes body;      // solo per gli schiocchi "grossi"
        WhiteNoise noise;
        float gL = 0.7f, gR = 0.7f;
        float bodyAmount = 0.0f;
        bool  active = false;
    };

    static constexpr int kNumCrackles = 48;

    void fire (float velocity, bool big);

    Crackle crackles[kNumCrackles];
    int     nextCrackle = 0;

    BrownNoise roarNoise;
    PinkNoise  hissNoise;
    SvfTpt     roarLp, hissHp, breathLp;
    WhiteNoise breathNoise;
    OnePole    breathSmooth;
    Rng        rng;

    float samplesToNext = 0.0f;
    float panSpread = 0.7f;
};

} // namespace af
