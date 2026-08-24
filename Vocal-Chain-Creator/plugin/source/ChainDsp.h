#pragma once
#include <juce_dsp/juce_dsp.h>
#include <atomic>

/*  VOCAL FORGE — la catena audio.

    Ordine FISSO (knowledge/CHAIN_ARCHITECTURE.md): i moduli si accendono e si tarano, non si riordinano.
    Riverbero e delay NON sono in serie: sono due bus paralleli alimentati dall'uscita della catena.

    Vincoli di Plugin-Creator/CLAUDE.md rispettati qui:
      - zero allocazioni in processBlock (tutto preallocato in prepare)
      - filtri TPT / coefficienti ricalcolati per-blocco, mai per-sample
      - tutti i continui passano da SmoothedValue
      - la UI legge solo std::atomic
*/
namespace vf
{

struct ChainSettings
{
    // globali
    float inTrim = 0.0f, outGain = 0.0f, mix = 100.0f;
    bool  polarity = false;

    // gate
    bool  gateOn = true;
    float gateThresh = -40.0f, gateRange = 12.0f, gateAtk = 2.0f, gateRel = 150.0f,
          gateKeyLo = 120.0f, gateKeyHi = 4000.0f;

    // hpf
    bool  hpfOn = true;
    float hpfFreq = 90.0f;
    int   hpfSlope = 24;

    // room tamer (3 notch dinamici)
    bool  roomOn = true;
    float roomFreq[3] { 92.0f, 184.0f, 246.0f };
    float roomQ[3]    { 6.0f, 5.0f, 5.0f };
    float roomDepth[3] { -4.0f, -4.0f, -3.5f };
    float roomThresh = -28.0f;

    // eq sottrattiva / tonale
    bool  eqSubOn = true, eqToneOn = true, airOn = false;
    float subFreq[3] { 240.0f, 1000.0f, 2800.0f };
    float subQ[3]    { 2.0f, 3.0f, 2.5f };
    float subGain[3] { -3.0f, -2.0f, -2.0f };
    float toneFreq[3] { 180.0f, 3200.0f, 8000.0f };
    float toneQ[3]    { 0.8f, 1.2f, 1.0f };
    float toneGain[3] { 0.0f, 0.0f, 0.0f };
    float airGain = 0.0f;

    // de-esser 1 e 2
    bool  ds1On = true, ds2On = true;
    float ds1Freq = 6650.0f, ds1Thresh = -20.0f, ds1Range = 6.0f;
    float ds2Freq = 7350.0f, ds2Thresh = -16.0f, ds2Range = 5.0f;
    bool  ds1Split = true, ds2Split = false;

    // compressori
    bool  c1On = true, c2On = true;
    float c1Thresh = -20.0f, c1Ratio = 3.0f, c1Atk = 8.0f, c1Rel = 90.0f, c1Knee = 6.0f, c1Makeup = 3.0f;
    float c2Thresh = -20.0f, c2Ratio = 2.0f, c2Atk = 30.0f, c2Rel = 250.0f, c2Knee = 8.0f, c2Makeup = 2.0f;

    // saturazione
    bool  satOn = true;
    float satDrive = 30.0f, satTilt = 0.0f;
    int   satType = 0;                      // 0 tape, 1 tube, 2 transistor

    // limiter
    bool  limOn = true;
    float limCeiling = -1.0f, limRelease = 100.0f;

    // mandate (bus paralleli)
    bool  revOn = true;
    float revDecay = 1.4f, revPredelay = 20.0f, revSize = 55.0f, revHpf = 300.0f,
          revLpf = 7000.0f, revWidth = 85.0f, revDuck = 5.0f, revSend = -24.0f;
    bool  dlyOn = true, dlySync = true;
    float dlyTime = 375.0f, dlyFeedback = 25.0f, dlyHpf = 350.0f, dlyLpf = 4000.0f,
          dlyDuck = 6.0f, dlySend = -24.0f;

    // doubler: terzo bus parallelo — allarga la voce senza toccare il centro
    bool  fxOn = false;
    float fxTimeL = 22.0f, fxTimeR = 32.0f, fxDetune = 8.0f, fxWidth = 70.0f,
          fxHpf = 300.0f, fxLpf = 7000.0f, fxDuck = 4.0f, fxSend = -14.0f;
};

/** Quello che la UI può leggere mentre l'audio gira: solo atomics, nessun lock. */
struct ChainMeters
{
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    std::atomic<float> gateGr { 0.0f }, ds1Gr { 0.0f }, comp1Gr { 0.0f },
                       comp2Gr { 0.0f }, ds2Gr { 0.0f }, limGr { 0.0f };
    std::atomic<float> outLufs { -70.0f };
};

class ChainDsp
{
public:
    ChainDsp() = default;

    void prepare (double sampleRate, int maximumBlockSize, int numChannels);
    void reset();

    /** Aggiorna i target dei parametri. Si chiama una volta per blocco, mai per-sample. */
    void setSettings (const ChainSettings& newSettings);

    /** Processa in place. Nessuna allocazione qui dentro. */
    void process (juce::AudioBuffer<float>& buffer, double bpm);

    ChainMeters meters;

    static float msToCoeff (float ms, double sampleRate) noexcept
    {
        return ms <= 0.0f ? 0.0f : static_cast<float> (std::exp (-1.0 / (ms * 0.001 * sampleRate)));
    }

private:
    struct Envelope
    {
        float value = 0.0f;
        float process (float input, float attackCoeff, float releaseCoeff) noexcept
        {
            const auto coeff = (input > value) ? attackCoeff : releaseCoeff;
            value = input + coeff * (value - input);
            return value;
        }
        void reset() noexcept { value = 0.0f; }
    };

    struct Compressor
    {
        Envelope env;
        float lastGrDb = 0.0f;

        float computeGain (float levelDb, float threshold, float ratio, float knee) noexcept;
        void reset() noexcept { env.reset(); lastGrDb = 0.0f; }
    };

    struct DeEsser
    {
        juce::dsp::LinkwitzRileyFilter<float> splitter;
        Envelope env;
        float lastGrDb = 0.0f;
        void prepare (const juce::dsp::ProcessSpec&);
        void reset();
    };

    using Filter  = juce::dsp::IIR::Filter<float>;
    using Coeffs  = juce::dsp::IIR::Coefficients<float>;

    void updateCoefficients();
    void processGate (juce::AudioBuffer<float>&);
    void processRoomTamer (juce::AudioBuffer<float>&);
    void processDeEsser (juce::AudioBuffer<float>&, DeEsser&, float freq, float threshDb,
                         float rangeDb, bool split, std::atomic<float>& meter);
    void processCompressor (juce::AudioBuffer<float>&, Compressor&, float thresh, float ratio,
                            float atkMs, float relMs, float knee, float makeupDb,
                            std::atomic<float>& meter);
    void processSaturation (juce::AudioBuffer<float>&);
    void processSends (const juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& destination, double bpm);

    ChainSettings settings;
    double sampleRate = 44100.0;
    int    numChannels = 2;

    // path principale
    juce::dsp::StateVariableTPTFilter<float> hpf1, hpf2, keyHp, keyLp;
    std::array<Filter, 2> roomFilters[3];
    std::array<Filter, 2> subFilters[3], toneFilters[3], airFilter;
    juce::dsp::StateVariableTPTFilter<float> roomDetector[3];
    Envelope roomEnv[3], gateEnv;
    float gateGainDb = 0.0f;

    Compressor comp1, comp2;
    DeEsser    deEsser1, deEsser2;
    juce::dsp::Limiter<float> limiter;

    juce::SmoothedValue<float> trimGain, outputGain, mixAmount, driveAmount,
                               revSendGain, dlySendGain, fxSendGain;

    // mandate (bus paralleli)
    juce::dsp::Reverb reverb;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 96000 };
    juce::dsp::StateVariableTPTFilter<float> revHp, revLp, dlyHp, dlyLp, fxHp, fxLp;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> doublerLine { 16384 };
    float doublerPhase = 0.0f;
    juce::AudioBuffer<float> dryScratch, revScratch, dlyScratch, preDelayScratch;
    Envelope duckEnv;
    float delayFeedbackState[2] { 0.0f, 0.0f };

    juce::dsp::BallisticsFilter<float> loudnessFollower;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainDsp)
};

} // namespace vf
