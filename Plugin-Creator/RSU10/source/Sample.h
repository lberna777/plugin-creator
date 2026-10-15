// Un campione in memoria + i posti dei campioni dei 64 pad (4 strati ciascuno).
// Il thread audio legge i posti con un load atomico e tiene vivo il campione con il conteggio
// dei riferimenti; i campioni tolti si cancellano solo sul thread dei messaggi, dopo un attimo.
#pragma once

#include "PadParams.h"

namespace rsu
{
class Sample final : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<Sample>;

    Sample (juce::AudioBuffer<float>&& audio, double rate, juce::String sampleName, juce::File source = {});

    const juce::AudioBuffer<float>& audio() const noexcept { return data; }
    int length() const noexcept                { return data.getNumSamples(); }
    int channels() const noexcept              { return data.getNumChannels(); }
    double rate() const noexcept               { return sampleRate; }
    double seconds() const noexcept            { return length() / sampleRate; }
    const juce::String& name() const noexcept  { return sampleName; }
    const juce::File& file() const noexcept    { return sourceFile; }

    // Battute stimate per un loop (l'SU10 mostrava il tempo accanto ai punti di loop)
    static int guessBeats (double seconds) noexcept;
    static double bpmFor (double seconds, int beats) noexcept { return seconds > 0.0 ? 60.0 * beats / seconds : 0.0; }

    // Forma d'onda ridotta per lo schermo: coppie min/max su kPeakBins colonne
    static constexpr int kPeakBins = 2048;
    const std::vector<std::pair<float, float>>& peaks() const noexcept { return peakData; }

    int demoIndex = -1;    // >= 0: suono del kit di prova (si rigenera, non ha file)

    static Ptr loadFromFile (const juce::File& f, juce::String* error = nullptr);
    static bool isAudioFile (const juce::File& f);
    static juce::AudioFormatManager& formats();

private:
    juce::AudioBuffer<float> data;
    double sampleRate;
    juce::String sampleName;
    juce::File sourceFile;
    std::vector<std::pair<float, float>> peakData;
};

class SampleSlots
{
public:
    SampleSlots();

    Sample* get (int pad, int layer) const noexcept { return slots[size_t (pad)][size_t (layer)].load (std::memory_order_acquire); }
    int layerCount (int pad) const noexcept;

    // Thread dei messaggi o di caricamento (mai l'audio)
    void set (int pad, int layer, Sample::Ptr s);
    void clearPad (int pad);
    void clearAll();
    void purge();          // libera i campioni tolti da più di un secondo e non più suonati

    std::atomic<uint32_t> generation { 0 };

private:
    std::array<std::array<std::atomic<Sample*>, kMaxLayers>, kNumPads> slots;
    struct Kept { Sample::Ptr sample; bool retired = false; double retiredAt = 0.0; };
    std::vector<Kept> kept;
    juce::CriticalSection lock;
};

// Kit di prova sintetizzato (16 suoni), per suonare subito senza caricare nulla
std::vector<Sample::Ptr> makeDemoKit (double rate = 44100.0);
} // namespace rsu
