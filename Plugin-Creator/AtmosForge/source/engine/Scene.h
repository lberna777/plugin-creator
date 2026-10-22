#pragma once
#include "Dsp.h"

namespace af
{

// Parametri del motore, già smussati dal processor: la scena li legge e basta.
// Sono valori "macro": ogni scena li interpreta nel proprio dominio.
struct SceneParams
{
    float density  = 0.5f;   // quanti eventi (gocce, crepitii, auto, passi)
    float motion   = 0.5f;   // velocità: transito auto, vento, andatura
    float tone     = 0.5f;   // timbro: scuro ↔ brillante (in Weather: neve ↔ pioggia)
    float depth    = 0.5f;   // distanza/ambienza: vicino e asciutto ↔ lontano e ovattato
    float width    = 0.7f;   // ampiezza stereo degli eventi
    int   surface  = 0;      // materiale (passi, pneumatici): legno/ghiaia/neve/cemento/metallo
    bool  autoGen  = true;   // eventi automatici oppure solo da MIDI
};

enum class Surface { Wood = 0, Gravel, Snow, Concrete, Metal, numSurfaces };

// Una scena scrive IN SOMMA su L/R (il buffer arriva già azzerato dal motore).
class Scene
{
public:
    virtual ~Scene() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;
    virtual void setSeed (uint32_t seed) = 0;

    // Rende n campioni. Nessuna allocazione qui dentro, mai.
    virtual void render (float* left, float* right, int n, const SceneParams& p) = 0;

    // Evento singolo su richiesta (nota MIDI). big = versione "grossa" dell'evento
    // (tuono, camion, ciocco che scoppia, passo pesante).
    virtual void trigger (float velocity, bool big) = 0;

protected:
    double sr = 44100.0;
};

} // namespace af
