#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ChainDsp.h"
#include "RulesEngine.h"
#include <vector>

/*  VOCAL FORGE — processore.

    Il prompt NON è un parametro automatizzabile: è stato. Scrive i parametri veri,
    che restano tutti automatizzabili e ritoccabili a mano (badge "modificato" in UI).
*/
class VocalForgeProcessor : public juce::AudioProcessor,
                            private juce::AudioProcessorValueTreeState::Listener
{
public:
    VocalForgeProcessor();
    ~VocalForgeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return "VOCAL FORGE"; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return chain.getTailSeconds(); }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return "VOCAL FORGE"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    /** Compila il prompt e scrive i parametri. È l'unica strada per generare una chain. */
    void applyPrompt (const juce::String& prompt, const juce::String& profileId);

    juce::AudioProcessorValueTreeState apvts;
    vf::RulesEngine   rulesEngine;
    vf::ChainMeters&  getMeters() noexcept                   { return chain.meters; }

    juce::String getPrompt() const;
    juce::String getProfileId() const;
    const vf::Preset& getLastPreset() const noexcept         { return lastPreset; }
    bool  hasGeneratedChain() const noexcept                 { return chainGenerated; }
    bool  wasTouchedByHand() const noexcept                  { return touchedByHand; }
    juce::String getRulesVersion() const                     { return rulesEngine.getRulesVersion(); }

    //==========================================================================
    /*  ASCOLTA — trim misurato invece che presunto.

        Il thread audio registra qualche secondo di voce VERA (finestre sopra il gate) sul segnale
        GREZZO, prima del trim. Quando ha finito, il message thread calcola il picco di lavoro
        (percentile dei picchi di finestra) e lo passa alle regole: l'inTrim esce da rules.json
        come sempre, ma con il picco misurato al posto di quello presunto dal profilo.
        Tutto preallocato: nel thread audio solo somme, massimi e una scrittura in un array. */
    enum class ListenState : int { idle = 0, listening = 1, done = 2, cancelRequested = 3 };

    /** Avvia l'ascolto. false se ce n'è già uno in corso. */
    bool  startListening();
    void  cancelListening();
    ListenState getListenState() const noexcept     { return static_cast<ListenState> (listenState.load()); }
    /** 0…1: quanta voce utile è stata raccolta. */
    float getListenProgress() const noexcept;
    /** Message thread: se l'ascolto è finito applica il trim misurato e torna true. */
    bool  finishListeningIfReady();
    /** L'ultima misura (valid == false se non si è mai ascoltato). */
    vf::Measurement getMeasurement() const;

    /** Testo della ricetta per i bus aux di Logic (modo sendsMode = logic). */
    juce::String getLogicSendsRecipe() const;

    /** Cambia a ogni preset generato o stato caricato: la UI se ne accorge dal suo timer.
        Una std::function scritta da un thread e letta da un altro era una corsa. (R11) */
    std::atomic<int> presetRevision { 0 };

    /** Lo stato che il DSP riceve adesso. Pubblico perché il self test verifica che ogni
        parametro dichiarato arrivi davvero fin qui: un knob che non muove nulla è un knob finto. */
    vf::ChainSettings currentSettings() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    /*  Puntatori ai valori dei parametri, risolti UNA volta nel costruttore.
        Prima `currentSettings()` costruiva 27 juce::String per blocco e faceva ~90 lookup:
        allocazioni sul thread audio, cioè un lock globale a ogni buffer.
    */
    struct ParamCache
    {
        void build (juce::AudioProcessorValueTreeState&);
        float get (const char* id) const noexcept;
        bool  flag (const char* id) const noexcept { return get (id) > 0.5f; }
        std::vector<std::pair<juce::String, std::atomic<float>*>> entries;
    };
    ParamCache params;
    void writePresetToParameters (const vf::Preset&);
    void listen (const juce::AudioBuffer<float>& rawInput) noexcept;

    vf::RulesEngine::LevelMatchSettings levelMatch;
    vf::RulesEngine::ListenSettings     listenSettings;
    std::atomic<int> listenState { 0 };
    std::vector<float> listenWindowPeaks;         // preallocato: un picco per finestra di voce
    std::atomic<int> listenWindows { 0 };         // finestre di voce raccolte (la UI legge il progresso)
    int    listenNeededWindows = 0, listenWindowLength = 0, listenWindowFill = 0;
    double listenWindowSumSquares = 0.0, listenVoicedSumSquares = 0.0;
    float  listenWindowPeak = 0.0f;
    juce::int64 listenVoicedSamples = 0;

    vf::ChainDsp chain;
    vf::Preset   lastPreset;
    juce::ValueTree promptState { "vfPrompt" };
    std::atomic<bool> chainGenerated { false }, touchedByHand { false }, writingPreset { false };
    std::atomic<bool> settingsDirty { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalForgeProcessor)
};
