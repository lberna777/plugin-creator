#pragma once
#include <JuceHeader.h>
#include "dsp/TapeDSP.h"

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

    juce::AudioProcessorValueTreeState apvts;

    // DSP → UI (lock-free): livello in uscita per i VU della futura UI
    std::atomic<float> outLevelL { 0.f }, outLevelR { 0.f };

private:
    static constexpr int chunk = 32;   // passo di aggiornamento dei coefficienti (campioni)
    static constexpr int maxCh = 2;
    static constexpr int biasSteps = 21;

    void updateFormula (int type, int speedIdx);
    void updateChunkCoefficients (double ageNow, double biasNow);
    void calibrate();
    void processChunk (float* const* ch, int numCh, int n);
    void processDry (juce::AudioBuffer<float>&);

    // parametri grezzi
    std::atomic<float>* pInput; std::atomic<float>* pTape; std::atomic<float>* pSpeed; std::atomic<float>* pBias;
    std::atomic<float>* pWow;   std::atomic<float>* pFlutter; std::atomic<float>* pHiss; std::atomic<float>* pAge;
    std::atomic<float>* pOutput; std::atomic<float>* pAutoGain; std::atomic<float>* pMix; std::atomic<float>* pBypass;
    juce::AudioParameterBool* bypassParam = nullptr;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inGain { 1.f }, outGain { 1.f };
    juce::SmoothedValue<float> wet { 1.f }, wowAmt, flutAmt, hissAmt, ageAmt, biasAmt;

    double fs = 48000.0;
    int curType = -1, curSpeed = -1;
    int latency = 0;
    float wowCenter = 0.f;

    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::AudioBuffer<float> chunkBuf;

    c60::Hysteresis hyst[maxCh];
    c60::FirstOrderShelf pre[maxCh], de[maxCh];
    c60::Biquad bump[maxCh], dip[maxCh], hp[maxCh], lp[maxCh], biasShelf[maxCh];
    double dropLpState[maxCh] {}, dropLpCoeff = 0.3;
    c60::FracDelay transport[maxCh];
    c60::FracDelay dryDelay[maxCh];
    c60::WowFlutter wf;
    c60::Dropouts drops;
    c60::SmoothRandom azimuthDrift;
    c60::Rng hissRng[maxCh] { c60::Rng (0xA5A5A5u), c60::Rng (0x5A5A5Au) };

    // calibrazioni calcolate in prepareToPlay (non sul thread audio)
    double hystNorm[3][biasSteps] {};
    double hissCal[3][2] {};
    double hystDrive = 1.0, hissTypeGain = 1.0;
    double calibratedFs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (C60AudioProcessor)
};
