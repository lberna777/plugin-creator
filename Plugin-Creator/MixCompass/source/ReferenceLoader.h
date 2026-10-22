#pragma once
#include <JuceHeader.h>
#include "analysis/MixAnalyzer.h"
#include <functional>

// Un riferimento pronto all'uso: audio già alla sample rate dell'host + analisi completa.
// Immutabile dopo la creazione: il thread audio lo legge, nessuno lo modifica.
struct ReferenceTrack
{
    juce::AudioBuffer<float> audio;   // sempre 2 canali (un file mono è duplicato)
    double sampleRate = 0;
    mc::Snapshot analysis;
    juce::File file;
};

// Thread di caricamento: decodifica (WAV/AIFF/MP3/M4A…), ricampiona alla SR dell'host, analizza.
// Tutto fuori dal thread audio. Una nuova richiesta interrompe quella in corso.
class ReferenceLoader : private juce::Thread
{
public:
    using Done   = std::function<void (std::unique_ptr<ReferenceTrack>)>;
    using Failed = std::function<void (const juce::File&, const juce::String& reason)>;

    static constexpr double maxSeconds = 15.0 * 60.0;

    ReferenceLoader (Done onDone, Failed onFailed);
    ~ReferenceLoader() override;

    void request (const juce::File& file, double hostSampleRate);
    bool isBusy() const noexcept { return busy.load(); }

    static juce::String supportedWildcard();

private:
    void run() override;
    std::unique_ptr<ReferenceTrack> load (const juce::File& file, double sampleRate, juce::String& error);
    bool cancelled() { return threadShouldExit() || hasNewRequest.load(); }

    juce::AudioFormatManager formats;
    Done onDone;
    Failed onFailed;

    juce::CriticalSection requestLock;   // solo fra thread non audio
    juce::File pendingFile;
    double pendingRate = 0;
    std::atomic<bool> hasNewRequest { false }, busy { false };
};
