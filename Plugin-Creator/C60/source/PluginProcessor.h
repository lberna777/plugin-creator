#pragma once
#include <JuceHeader.h>
#include "dsp/TapeDSP.h"
#include "dsp/NoiseReduction.h"
#include "dsp/Playback.h"
#include "dsp/TapeMedium.h"

class C60AudioProcessor : public juce::AudioProcessor
{
public:
    C60AudioProcessor();
    ~C60AudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
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

    juce::AudioParameterBool* getBypassParameter() const override { return bypassParam; }

    //==========================================================================
    // Comandi della piastra (thread dei messaggi)
    void setPunchInHere()  { punchIn.store (tapePos.load()); }
    void setPunchOutHere() { punchOut.store (tapePos.load()); }
    void setMemoHere()     { memoPos.store (tapePos.load()); }
    void setPunchRange (int64_t in, int64_t out) { punchIn.store (in); punchOut.store (out); }
    bool undoLastPass();
    void eraseTape();
    void calibrate();                                   // calibra il tipo di nastro corrente
    bool exportTape (const juce::File& wav, juce::String& error);

    // Stato per la UI (lock-free)
    struct Deck
    {
        int64_t position = 0, in = 0, out = 0, memo = 0, used = 0, length = 0;
        double sampleRate = 48000;
        bool playing = false, recording = false, canUndo = false, calibrated = false;
        uint16_t currentPass = 0;
    };
    Deck getDeckState() const;
    const c60::TapeMedium& getTape() const { return tape; }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> outLevelL { 0.f }, outLevelR { 0.f };

private:
    static constexpr int chunk = 32;
    static constexpr int maxCh = 2;
    static constexpr int biasSteps = 21;
    static constexpr int hxRingSize = 1024;

    void updateFormula (int type, int speedIdx);
    void calibrateTables();
    void processChunk (float* const* ch, int numCh, int n, bool playing, int64_t dawStart, bool tapeLocked);
    void processDry (juce::AudioBuffer<float>&);
    double hissGainFor (double hiss01) const;
    void applyPendingTape();

    // parametri
    std::atomic<float>* pInput; std::atomic<float>* pTape; std::atomic<float>* pSpeed; std::atomic<float>* pBias;
    std::atomic<float>* pWow;   std::atomic<float>* pFlutter; std::atomic<float>* pHiss; std::atomic<float>* pAge;
    std::atomic<float>* pOutput; std::atomic<float>* pAutoGain; std::atomic<float>* pMix; std::atomic<float>* pBypass;
    std::atomic<float>* pMonitor; std::atomic<float>* pRec; std::atomic<float>* pAutoPunch; std::atomic<float>* pRecMute;
    std::atomic<float>* pFade; std::atomic<float>* pPitch; std::atomic<float>* pNr; std::atomic<float>* pHx;
    std::atomic<float>* pAzimuth; std::atomic<float>* pAutoAz;
    juce::AudioParameterBool* bypassParam = nullptr;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inGain { 1.f }, outGain { 1.f };
    juce::SmoothedValue<float> wet { 1.f }, wowAmt, flutAmt, hissAmt, ageAmt, biasAmt, rate { 1.f }, monitorTape;

    double fs = 48000.0;
    int curType = -1, curSpeed = -1;
    int latency = 0, osLatency = 0, center = 0;

    // registrazione
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::AudioBuffer<float> chunkBuf;
    c60::Hysteresis hyst[maxCh];
    c60::FirstOrderShelf pre[maxCh];
    c60::NoiseReduction nrEnc[maxCh];
    c60::OnePole hxSplit[maxCh], hxRestore[maxCh];
    c60::EnvFollower hxEnv[maxCh];
    float hxRing[maxCh][hxRingSize] {};
    int hxW = 0;

    // nastro e trasporto
    c60::TapeMedium tape;
    c60::PlaybackChain play;
    c60::FracDelay dryDelay[maxCh];
    double readPos = 0;            // posizione di lettura (con pitch)
    int64_t expectedDaw = -1;      // per riconoscere salti e ripartenze del DAW
    float gRec = 0, gErase = 0;    // rampe delle testine di registrazione e cancellazione
    bool wasRecordingTarget = false;

    // stato della piastra
    std::atomic<int64_t> tapePos { 0 }, punchIn { 0 }, punchOut { 0 }, memoPos { 0 };
    std::atomic<bool> playingFlag { false }, recordingFlag { false };
    std::atomic<bool> calibrated[3] { { false }, { false }, { false } };

    // nastro caricato da un progetto prima di prepareToPlay
    juce::AudioBuffer<float> pendingTape;
    std::vector<uint16_t> pendingCells;
    double pendingFs = 0;
    std::atomic<bool> hasPending { false };

    // calibrazioni (prepareToPlay)
    double hystNorm[3][biasSteps] {};
    double hissCal[3][2] {};
    double hystDrive = 1.0, hissTypeGain = 1.0;
    double calibratedFs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (C60AudioProcessor)
};
