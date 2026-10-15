// Motore dell'RSU-10: voci, banchi, modo scala, ribbon, choke, uscite separate,
// anteprima del browser e campionamento dall'ingresso o dall'uscita.
// Tutto quello che gira in process() è senza allocazioni né lock.
#pragma once

#include "Sample.h"

namespace rsu
{
enum class RibbonMode { Scratch, Pitch, Filter, Reverse, Crossfade };

struct EngineGlobals
{
    float ribbon = 0.5f;          // 0..1
    bool ribbonTouch = false;
    RibbonMode ribbonMode = RibbonMode::Scratch;
    bool ribbonHold = false;
    float ribbonPitchRange = 12.0f;   // semitoni ai due estremi
    bool scale = false;
    int scaleRoot = 60;
    int scalePad = 0;
    float masterGain = 1.0f;
    double hostBpm = 0.0;         // 0 = sconosciuto
};

struct OutputPointers
{
    float* left[kNumOutputs] {};
    float* right[kNumOutputs] {};   // nullptr = bus spento: va sulla principale
};

// Filtro di stato TPT (Zavalishin): passa-basso, passa-alto, passa-banda da un solo stadio
struct Svf
{
    float ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0, k = 1;
    void set (float cutoff, float q, double rate) noexcept;
    void reset() noexcept { ic1 = ic2 = 0.0f; }
    float process (float x, FilterType type) noexcept;
};

class Engine
{
public:
    Engine (PadParamStore& params, SampleSlots& samples);
    ~Engine();

    void prepare (double sampleRate, int maxBlock);
    void process (const OutputPointers& out, const float* const* input, int numInputChannels,
                  int numSamples, const juce::MidiBuffer& midi, const EngineGlobals& g);

    // ---- dalla UI (thread dei messaggi), lock-free verso l'audio
    // semitones/note: modo tastiera (un pad su più note); note < 0 = il pad suona sé stesso
    void triggerFromUi (int pad, int velocity, bool on, float semitones = 0.0f, int note = -1);
    void panic();
    void startPreview (Sample::Ptr s);
    void stopPreview();
    void purgePreviews();

    // Campionamento (SU10: REC > pad > ENTER)
    enum class RecSource { Input = 0, Output = 1 };
    enum class RecState { Idle = 0, Armed, Recording, Done };
    void armRecording (RecSource src, float threshold);
    void stopRecording();
    RecState recState() const noexcept { return RecState (recStateAtomic.load()); }
    bool takeRecording (juce::AudioBuffer<float>& dest, double& rate);   // quando Done
    float recordedSeconds() const noexcept { return float (recPos.load()) / float (juce::jmax (1.0, sr)); }
    static constexpr double kMaxRecSeconds = 30.0;

    // ---- stato per la UI
    std::atomic<float> padHit[kNumPads] {};       // ultima velocity 0..1 (la UI la fa decadere)
    std::atomic<int> padVoices[kNumPads] {};
    std::atomic<float> padPosition[kNumPads] {};  // posizione dell'ultima voce, 0..1 sul campione
    std::atomic<int> padLastLayer[kNumPads] {};
    std::atomic<float> outPeak { 0.0f };
    std::atomic<bool> previewPlaying { false };
    std::atomic<float> ribbonShown { 0.5f };      // valore effettivo (anche da MIDI)
    std::atomic<bool> ribbonTouchedShown { false };

    // MUTE / SOLO della Maschine: il pad suona ma la sua uscita sfuma a zero
    std::atomic<bool> muted[kNumPads] {}, soloed[kNumPads] {};

    static constexpr int kMaxVoices = 48;

private:
    struct Voice;
    struct PadStage;
    struct UiEvent { int pad, velocity; bool on; float semitones; int note; };

    void handleMidi (const juce::MidiMessage& m, const EngineGlobals& g);
    void noteOn (int pad, int velocity, float semitoneOffset, int note);
    void noteOff (int pad, int note);
    void render (int start, int num, const EngineGlobals& g);
    void updateRibbon (int numSamples, const EngineGlobals& g);
    Voice* allocateVoice();

    PadParamStore& params;
    SampleSlots& slots;
    double sr = 44100.0;
    int maxBlock = 512, blockSize = 0;

    std::vector<std::unique_ptr<Voice>> voices;
    std::vector<std::unique_ptr<PadStage>> stages;
    uint64_t voiceCounter = 0;

    // Ribbon: valore e tocco effettivi (parametro o MIDI), scratch in velocità
    float ribbonValue = 0.5f, ribbonPrev = 0.5f;
    bool ribbonLatched = false;
    float midiRibbon = 0.5f;
    int midiRibbonHold = 0;            // campioni rimasti di "tocco" dopo un CC
    bool midiRibbonOn = false;
    float scratchRate = 1.0f, handVelocity = 0.0f, pitchSemis = 0.0f, ribbonCutoff = 20000.0f, crossfade = 0.0f;
    bool reverseNow = false, scratchActive = false;
    RibbonMode lastMode = RibbonMode::Scratch;
    std::vector<float> scratchBuf;     // velocità dello scratch per campione

    juce::AbstractFifo uiFifo { 256 };
    std::array<UiEvent, 256> uiEvents {};

    // Anteprima del browser
    std::atomic<Sample*> previewRequest { nullptr };
    std::atomic<bool> previewStop { false };
    Sample::Ptr previewNow;            // tenuto dal thread audio
    double previewPos = 0.0;
    std::vector<std::pair<Sample::Ptr, double>> previewKeep;   // thread dei messaggi

    // Registrazione
    juce::AudioBuffer<float> recBuffer;
    std::atomic<int> recStateAtomic { 0 };
    std::atomic<int> recPos { 0 };
    std::atomic<bool> recStopRequest { false };
    RecSource recSource = RecSource::Input;
    float recThreshold = 0.0f;

    // Mix di lavoro (principale) per crossfade e ricampionamento
    juce::AudioBuffer<float> mixL;
};

// Fattore di stretch: frame sorgente da leggere per campione dell'host
double stretchHeadRate (Stretch mode, double regionSeconds, double regionFrames, double hostBpm, double hostRate);
} // namespace rsu
