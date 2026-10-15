#pragma once

#include "Engine.h"

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
}

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
    void clearPad (int pad);
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

protected:
    static BusesProperties buses();

private:
    void timerCallback() override;
    juce::ValueTree kitToTree (const juce::File& relativeTo) const;
    void kitFromTree (const juce::ValueTree& kit, const juce::File& relativeTo);
    void finishRecording();

    std::atomic<float>* pMaster; std::atomic<float>* pRibbon; std::atomic<float>* pTouch;
    std::atomic<float>* pMode;   std::atomic<float>* pHold;   std::atomic<float>* pRange;
    std::atomic<float>* pScale;  std::atomic<float>* pRoot;

    juce::AudioBuffer<float> inputCopy;
    juce::SmoothedValue<float> master;
    juce::ThreadPool loader { 1 };
    std::atomic<int> recPad { -1 };

    mutable juce::CriticalSection messageLock;
    juce::String message;
    double messageTime = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RSU10AudioProcessor)
};
