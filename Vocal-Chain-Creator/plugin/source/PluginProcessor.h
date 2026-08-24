#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ChainDsp.h"
#include "RulesEngine.h"

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
    bool isBusesLayoutSupported (const BusesProperties&) const;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return "VOCAL FORGE"; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 4.0; }

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

    /** Testo della ricetta per i bus aux di Logic (modo sendsMode = logic). */
    juce::String getLogicSendsRecipe() const;

    std::function<void()> onPresetGenerated;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    vf::ChainSettings currentSettings() const;
    void writePresetToParameters (const vf::Preset&);

    vf::ChainDsp chain;
    vf::Preset   lastPreset;
    juce::ValueTree promptState { "vfPrompt" };
    std::atomic<bool> chainGenerated { false }, touchedByHand { false }, writingPreset { false };
    std::atomic<bool> settingsDirty { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalForgeProcessor)
};
