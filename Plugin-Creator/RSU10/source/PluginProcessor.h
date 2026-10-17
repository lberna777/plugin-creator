#pragma once

#include "Engine.h"
#include "Scales.h"

namespace ParamID
{
// Parametri del DAW (automatizzabili): CONGELATI dopo il primo rilascio
inline constexpr const char* master      = "master";
inline constexpr const char* ribbon      = "ribbon";
inline constexpr const char* ribbonTouch = "ribbonTouch";
inline constexpr const char* ribbonMode  = "ribbonMode";
inline constexpr const char* ribbonHold  = "ribbonHold";
inline constexpr const char* ribbonRange = "ribbonRange";
inline constexpr const char* scale       = "scale";
inline constexpr const char* scaleRoot   = "scaleRoot";
inline constexpr const char* keyRoot     = "keyRoot";     // tonalità della tastiera (C..B)
inline constexpr const char* keyScale    = "keyScale";    // scala della tastiera (scales::kScales)
}

// Funzioni della Maschine: i tasti che cambiano cosa fa un colpo sul pad
enum class PadAction { None = 0, Mute, Solo, Select, Duplicate, Erase };
enum class PadMode { Normal = 0, Velocity16, Keyboard };   // KEYBOARD = modo scala dell'SU10

// Comandi dei tasti (dalla UI o dai tasti della Mikro via Mikro Bridge), eseguiti sul thread dei messaggi
enum class Cmd
{
    Play, Rec, Top, Stop, Bwd, Fwd, Enter, Exit, Bank, PrevPad, NextPad, NextPage,
    Scratch, Rev, NextRibbonMode, Hold, NextPadMode, FixedVel, RepeatRate,
    Undo, Redo, SampleMinus, SamplePlus, ClearSelected, CopyPad, PastePad, ActionEnd, Chop
};

class RSU10AudioProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    RSU10AudioProcessor();
    ~RSU10AudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---------------------------------------------------------------- per la UI
    juce::AudioProcessorValueTreeState apvts;
    rsu::PadParamStore pads;
    rsu::SampleSlots samples;
    rsu::Engine engine { pads, samples };

    // Stato della UI salvato col progetto
    std::atomic<int> selectedPad { 0 }, page { 0 }, scalePad { 0 }, recSource { 0 };
    juce::File browserFolder;
    juce::String kitName { "KIT DI PROVA" };
    juce::File kitFile;

    // Caricamento dei campioni (in un thread a parte)
    void loadSample (int pad, int layer, const juce::File& f);
    void loadSamplesFrom (int firstPad, const juce::Array<juce::File>& files);   // un file per pad
    void stepSampleInFolder (int pad, int delta);    // -1/+1: campione precedente/successivo della cartella
    void clearPad (int pad);                         // campioni e parametri
    void clearSamples (int pad);                     // solo i campioni
    void copyPad (int from, int to);
    void loadDemoKit();

    // Kit
    bool saveKit (const juce::File& file, juce::String& error);
    bool loadKit (const juce::File& file, juce::String& error);

    // Campionamento nel pad selezionato
    void startRecording();
    void stopRecording();
    int recordingPad() const noexcept { return recPad.load(); }

    // Messaggi per lo schermo (errori, conferme)
    void postMessage (const juce::String& m);
    juce::String getMessage (double& ageMs) const;

    static juce::File userFolder();   // ~/Music/RSU-10

    // ---------------------------------------------------------------- funzioni Maschine
    // Il tasto d'azione vale finché è agganciato col mouse (uiAction) o tenuto sulla Mikro (heldAction)
    PadAction action() const noexcept
    {
        const int h = heldAction.load();
        return PadAction (h != 0 ? h : uiAction.load());
    }
    void toggleUiAction (PadAction a);
    void padAction (int pad, PadAction a);            // MUTE/SOLO/SELECT/DUPLICATE/ERASE sul pad
    PadMode padMode() const noexcept;
    int keyboardSemitones (int slot) const noexcept;   // KEYBOARD: semitoni dall'originale del grado `slot`
    juce::String keyboardNoteName (int slot) const;
    juce::String scaleName() const;                    // es. "D MINORE NATURALE"
    void setPadMode (PadMode m);
    void runCommand (Cmd c);
    std::function<bool (Cmd)> editorCommand;          // l'editor gestisce i comandi del browser

    std::atomic<int> uiAction { 0 };
    std::atomic<bool> fixedVelocity { false };
    std::atomic<bool> repeatUi { false }, repeatHeld { false };
    std::atomic<int> repeatRate { 3 };                 // indice in kRepeatRateNames
    std::atomic<int> uiHeldPad { -1 }, uiHeldVelocity { 0 };   // pad tenuto col mouse, per il NOTE REPEAT
    int duplicateSource = -1;

    static constexpr int kRepeatRates = 6;
    static constexpr const char* kRepeatRateNames[kRepeatRates] { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    static constexpr double kRepeatBeats[kRepeatRates] { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };

    // CHOP: il campione del pad diviso in fette sui pad del suo banco (una fetta = un pad).
    // Ritorna il numero di fette. Transienti: tagli sui colpi; Equal: 16 parti uguali.
    enum class ChopMode { Transients, Equal };
    int chopPad (int pad, ChopMode mode);
    static std::vector<double> findSlices (const juce::AudioBuffer<float>& audio, double rate, int from, int to, int maxSlices);

    // UNDO / REDO delle modifiche ai pad (cancella, copia, carica, registra)
    void pushUndo (int pad, int group = 0);   // group != 0: si annulla insieme (es. le 16 fette del chop)
    void undo();
    void redo();

    void drainPosted();                                         // esegue azioni e comandi arrivati dal MIDI
    const juce::MidiBuffer& lastMidi() const noexcept { return midiWork; }   // MIDI dato al motore (test)

protected:
    static BusesProperties buses();

private:
    void timerCallback() override;
    juce::ValueTree kitToTree (const juce::File& relativeTo) const;
    void kitFromTree (const juce::ValueTree& kit, const juce::File& relativeTo);
    void finishRecording();

    // MIDI in arrivo -> MIDI per il motore: tasti della Mikro, azioni, modi dei pad, note repeat
    void transformMidi (const juce::MidiBuffer& in, int numSamples, double bpm, const juce::Optional<double>& ppq, bool playing);
    bool controllerButton (int cc, int value);
    void post (int code);   // >= 0: azione su pad (pad | azione << 8); < 0: comando -(cmd + 1)
    void postCommand (Cmd c) { post (-(int (c) + 1)); }

    struct PadSnapshot
    {
        int pad = 0, group = 0;
        std::array<float, rsu::kNumPP> values {};
        std::array<rsu::Sample::Ptr, rsu::kMaxLayers> layers;
    };
    PadSnapshot snapshot (int pad) const;
    void restore (const PadSnapshot&);
    std::vector<PadSnapshot> undoStack, redoStack;
    int undoGroupCounter = 0;
    struct ChopMemory { rsu::Sample::Ptr sample; int pad = -1; float start = 0, end = 1; double at = -1.0e9; ChopMode mode = ChopMode::Transients; };
    ChopMemory lastChop;   // ripremendo CHOP subito sullo stesso campione si passa ai tagli uguali
    int clipboardPad = -1;

    std::atomic<int> heldAction { 0 };
    std::atomic<int> padModeStored { 0 };   // Normal o Velocity16 (Keyboard = parametro scale)
    juce::MidiBuffer midiWork;
    juce::AbstractFifo postFifo { 256 };
    std::array<int, 256> postCodes {};
    std::array<bool, 128> swallowed {};
    std::array<int, 128> mappedNote {}, heldVelocity {};
    double repeatPhase = 0.0, preparedRate = 44100.0;
    bool repeatWasOn = false;

    std::atomic<float>* pMaster; std::atomic<float>* pRibbon; std::atomic<float>* pTouch;
    std::atomic<float>* pMode;   std::atomic<float>* pHold;   std::atomic<float>* pRange;
    std::atomic<float>* pScale;  std::atomic<float>* pRoot;
    std::atomic<float>* pKeyRoot; std::atomic<float>* pKeyScale;

    juce::AudioBuffer<float> inputCopy;
    juce::SmoothedValue<float> master;
    juce::ThreadPool loader { 1 };
    std::atomic<int> recPad { -1 };

    mutable juce::CriticalSection messageLock;
    juce::String message;
    double messageTime = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RSU10AudioProcessor)
};
