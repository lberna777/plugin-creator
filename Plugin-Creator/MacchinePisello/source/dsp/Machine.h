#pragma once
#include "Blocks.h"
#include "../Params.h"

namespace mp
{
// Parametro continuo: legge l'atomic dell'APVTS una volta per blocco, smussa per-sample.
template <typename SmoothingType = juce::ValueSmoothingTypes::Linear>
struct SmoothedParam
{
    std::atomic<float>* source = nullptr;
    juce::SmoothedValue<float, SmoothingType> value;

    void bind (juce::AudioProcessorValueTreeState& s, const juce::String& id) { source = s.getRawParameterValue (id); }
    void prepare (double fs, double rampSeconds)
    {
        value.reset (fs, rampSeconds);
        value.setCurrentAndTargetValue (source->load());
    }
    void beginBlock() noexcept { value.setTargetValue (source->load()); }
    float next() noexcept      { return value.getNextValue(); }
};

using Smoothed    = SmoothedParam<>;
using SmoothedLog = SmoothedParam<juce::ValueSmoothingTypes::Multiplicative>;

//==============================================================================
// La macchina: voce mono stereo-spread, sequencer, FX. Tutto per-sample, zero allocazioni in process().
class Machine
{
public:
    void bind (juce::AudioProcessorValueTreeState& apvts);
    void prepare (double sampleRate);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, juce::AudioPlayHead* playHead);

    // ---- DSP → UI (lock-free) ----
    static constexpr int scopeSize = 1024;
    std::atomic<int>   uiStep { -1 };
    std::atomic<float> uiGate { 0.0f };
    std::atomic<float> uiPeak { 0.0f };
    std::array<std::atomic<float>, scopeSize> uiScope {};
    std::atomic<int>   uiScopeWrite { 0 };

private:
    void readBlockParams() noexcept;
    void render (juce::AudioBuffer<float>& buffer, int start, int end) noexcept;
    void handleMidi (const juce::MidiMessage& m) noexcept;
    void clockSequencer (int sampleIndex) noexcept;
    void triggerStep (int step) noexcept;
    void stopSequencer() noexcept;

    double fs = 44100.0;
    Rng rng;

    // parametri continui (smussati)
    Smoothed    sWave, sTune, sDetune, sGlide, sSub, sNoise, sFold, sDrift;
    SmoothedLog sCutoff;
    Smoothed    sReso, sFilterEnv, sDrive;
    SmoothedLog sAttack, sDecay;
    Smoothed    sSustain, sDrone;
    SmoothedLog sLfoRate;
    Smoothed    sLfoPitch, sLfoCutoff, sLfoFold;
    SmoothedLog sDelayTime;
    Smoothed    sDelayFb, sDelayMix, sSpaceSize, sSpaceMix, sWidth, sVolume;

    // parametri discreti o campionati al trigger
    std::atomic<float>* pLfoShape = nullptr, *pChaos = nullptr;
    std::atomic<float>* pSeqOn = nullptr, *pSeqRate = nullptr, *pSeqScale = nullptr, *pSeqLength = nullptr;
    std::atomic<float>* pSeqProb = nullptr, *pSeqMutate = nullptr, *pSeqRatchet = nullptr, *pSeqSwing = nullptr;
    std::array<std::atomic<float>*, pid::numSteps> pSteps {};

    int   lfoShape = 0, seqRateIndex = 3, seqScale = 2, seqLength = 8;
    bool  seqOn = true;
    float chaos = 0, seqProb = 1, seqMutate = 0, seqRatchet = 0, seqSwing = 0;

    // voce
    Oscillator osc1, osc2, sub;
    Ladder     ladderL, ladderR;
    Envelope   env;
    Lfo        lfo;
    SmoothRandom drift1, drift2, drift3;
    float currentNote = 48.0f, targetNote = 48.0f, accent = 1.0f;
    float chaosCutoffOct = 0.0f, chaosDecayScale = 1.0f;
    float pitchBend = 0.0f, modWheel = 0.0f;

    // MIDI tenute (stack fisso, niente allocazioni)
    std::array<int, 16> held {};
    int   heldCount = 0;
    float rootNote = 48.0f;

    // sequencer
    static constexpr int64_t noTick = std::numeric_limits<int64_t>::min();
    bool    hostPlaying = false, seqRunning = false, seqHostDriven = false;
    double  blockPpq = 0.0, ppqPerSample = 0.0, freePpq = 0.0, bpm = 120.0;
    int64_t lastTick = noTick;
    int     gateTimer = 0, ratchetLeft = 0, ratchetTimer = 0, ratchetInterval = 0;
    std::array<int, pid::numSteps> mutation {};

    // FX
    TapeDelay delay;
    Space     space;

    float dcL = 0.0f, dcR = 0.0f, dcCoef = 0.0f;   // DC blocker dopo il VCA (fold/tanh asimmetrici)
    float peakHold = 0.0f;
    int   scopeDecimator = 0, scopeWrite = 0;
};
} // namespace mp
