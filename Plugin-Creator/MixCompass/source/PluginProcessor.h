#pragma once
#include <JuceHeader.h>
#include "LockFree.h"
#include "ReferenceLoader.h"
#include "analysis/MixAnalyzer.h"

namespace ParamIds
{
    // ID CONGELATI (PLUGIN_SPEC §3): mai rinominare dopo il rilascio
    inline constexpr const char* listenRef  = "listenRef";
    inline constexpr const char* levelMatch = "levelMatch";
}

class MixCompassProcessor : public juce::AudioProcessor
{
public:
    MixCompassProcessor();
    ~MixCompassProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return MC_PRODUCT_NAME; }
    bool acceptsMidi() const override { return false; }
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

    //==== per la UI (thread dei messaggi) ======================================
    enum class RefState { none, loading, ready, error };
    struct RefStatus
    {
        RefState state = RefState::none;
        juce::String name, message;
        std::shared_ptr<const mc::Snapshot> analysis;
    };

    void loadReference (const juce::File& file);
    RefStatus getReferenceStatus() const;
    const mc::Snapshot* readMixSnapshot() noexcept { return mixSnapshots.readLatest(); }   // nullptr = niente di nuovo
    void requestReset() noexcept { resetRequested = true; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    //==== thread di analisi: legge il FIFO, analizza, pubblica lo Snapshot ======
    class AnalysisWorker : public juce::Thread
    {
    public:
        explicit AnalysisWorker (MixCompassProcessor& p) : juce::Thread ("MixCompass analysis"), owner (p) {}
        void run() override;
    private:
        MixCompassProcessor& owner;
    };

    void pushToFifo (const juce::AudioBuffer<float>& buffer, int numSamples) noexcept;
    void drainFifo() noexcept;

    //==== riferimento condiviso col thread audio (puntatore atomico + hazard) =====
    ReferenceTrack* acquireReference() noexcept;
    void releaseReference() noexcept { referenceInUse.store (nullptr); }
    void installReference (std::unique_ptr<ReferenceTrack>);
    void referenceFailed (const juce::File&, const juce::String&);

    std::atomic<float>* listenRefParam = nullptr;
    std::atomic<float>* levelMatchParam = nullptr;

    double currentSampleRate = 0;
    int analysisChannels = 2;

    // audio → analisi
    static constexpr int fifoSize = 1 << 17;
    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoL, fifoR;
    std::atomic<int> droppedSamples { 0 };

    std::unique_ptr<mc::MixAnalyzer> analyzer;   // usato SOLO dal thread di analisi
    TripleBuffer<mc::Snapshot> mixSnapshots;
    std::atomic<bool> resetRequested { false };
    std::atomic<float> mixLoudnessForMatch { std::numeric_limits<float>::quiet_NaN() };
    AnalysisWorker worker { *this };

    // A/B
    std::atomic<ReferenceTrack*> currentReference { nullptr };
    std::atomic<ReferenceTrack*> referenceInUse { nullptr };
    juce::int64 referencePosition = 0;
    juce::SmoothedValue<float> abAmount;                                         // 0 = mix, 1 = riferimento
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> refGain { 1.0f };

    // stato del riferimento per la UI (solo thread non audio)
    mutable juce::CriticalSection statusLock;
    RefStatus status;
    juce::File referenceFile;
    std::unique_ptr<ReferenceLoader> loader;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixCompassProcessor)
};
