#pragma once
#include <JuceHeader.h>
#include "engine/Engine.h"

// ATMOS FORGE — suite di generazione di ambienti sonori procedurali.
// Nessun sample su disco: tutto è sintesi, quindi ogni render è diverso e lungo quanto vuoi.

namespace ParamID
{
    static constexpr const char* scene   = "scene";
    static constexpr const char* density = "density";
    static constexpr const char* motion  = "motion";
    static constexpr const char* tone    = "tone";
    static constexpr const char* depth   = "depth";
    static constexpr const char* width   = "width";
    static constexpr const char* level   = "level";
    static constexpr const char* surface = "surface";
    static constexpr const char* seed    = "seed";
    static constexpr const char* autoGen = "autogen";
}

// Nomi di scene e materiali: una sola fonte di verità, condivisa con la UI.
const juce::StringArray& sceneNames();
const juce::StringArray& surfaceNames();

class AtmosForgeAudioProcessor final : public juce::AudioProcessor
{
public:
    AtmosForgeAudioProcessor();
    ~AtmosForgeAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- per la UI
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    float getPeakLevel() const noexcept { return peakLevel.load(); }   // UI ← DSP: solo atomici
    void  triggerManualEvent (bool big) noexcept;                     // pulsante "innesca" in UI

    // Render offline di un sample WAV (gira sul message thread, NON nel path audio).
    // Ritorna false se non riesce a scrivere il file.
    bool renderToFile (const juce::File& destination, double seconds, double sampleRate);

    // Nome suggerito per il file esportato: "ATMOS_Camino_s1234_12s.wav"
    juce::String suggestedFileName (double seconds) const;

private:
    af::SceneParams currentParams() const;

    af::Engine engine;

    juce::SmoothedValue<float> smDensity, smMotion, smTone, smDepth, smWidth, smGain;
    std::atomic<float> peakLevel { 0.0f };

    // eventi richiesti dalla UI, consumati dal path audio (lock-free)
    std::atomic<int> pendingSmallEvents { 0 }, pendingBigEvents { 0 };

    uint32_t lastSeed = 0;

    juce::AudioBuffer<float> scratch;   // buffer interno (mono→stereo, allocato in prepareToPlay)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AtmosForgeAudioProcessor)
};
