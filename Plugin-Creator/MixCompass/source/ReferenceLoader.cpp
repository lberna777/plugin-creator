#include "ReferenceLoader.h"

ReferenceLoader::ReferenceLoader (Done d, Failed f)
    : juce::Thread ("MixCompass reference loader"), onDone (std::move (d)), onFailed (std::move (f))
{
    formats.registerBasicFormats();   // WAV, AIFF, FLAC, Ogg, MP3 (JUCE_USE_MP3AUDIOFORMAT), CoreAudio su macOS
    startThread (juce::Thread::Priority::low);
}

ReferenceLoader::~ReferenceLoader()
{
    stopThread (5000);
}

juce::String ReferenceLoader::supportedWildcard()
{
    return "*.wav;*.wave;*.aif;*.aiff;*.mp3;*.m4a;*.aac;*.flac;*.caf";
}

void ReferenceLoader::request (const juce::File& file, double hostSampleRate)
{
    {
        const juce::ScopedLock sl (requestLock);
        pendingFile = file;
        pendingRate = hostSampleRate;
    }
    busy = true;
    hasNewRequest = true;
    notify();
}

void ReferenceLoader::run()
{
    while (! threadShouldExit())
    {
        if (! hasNewRequest.load())
        {
            busy = false;
            wait (-1);
            continue;
        }

        juce::File file;
        double rate;
        {
            const juce::ScopedLock sl (requestLock);
            file = pendingFile;
            rate = pendingRate;
            hasNewRequest = false;
        }

        juce::String error;
        auto track = load (file, rate, error);

        if (threadShouldExit()) return;
        if (hasNewRequest.load()) continue;   // superato da una richiesta più nuova: si butta

        if (track != nullptr) onDone (std::move (track));
        else                  onFailed (file, error);
    }
}

std::unique_ptr<ReferenceTrack> ReferenceLoader::load (const juce::File& file, double sampleRate, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "File non trovato: " + file.getFullPathName();
        return nullptr;
    }
    if (sampleRate <= 0)
    {
        error = "Il plugin non è ancora attivo: premi play e riprova.";
        return nullptr;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Formato non riconosciuto. Usa WAV, AIFF, MP3 o M4A.";
        return nullptr;
    }

    const auto length = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) std::numeric_limits<int>::max() / 2);
    if (length <= 0 || reader->sampleRate <= 0)
    {
        error = "Il file è vuoto.";
        return nullptr;
    }
    if ((double) length / reader->sampleRate > maxSeconds)
    {
        error = "Il file è troppo lungo (massimo 15 minuti).";
        return nullptr;
    }

    // 1) decodifica (un file mono finisce su entrambi i canali)
    constexpr int pad = 256;   // il ricampionatore legge qualche campione oltre la fine
    juce::AudioBuffer<float> source (2, length + pad);
    source.clear();
    reader->read (&source, 0, length, 0, true, true);
    if (reader->numChannels == 1)
        source.copyFrom (1, 0, source, 0, 0, length);
    if (cancelled()) return nullptr;

    // 2) ricampionamento alla sample rate dell'host
    auto track = std::make_unique<ReferenceTrack>();
    track->file = file;
    track->sampleRate = sampleRate;
    const double ratio = reader->sampleRate / sampleRate;   // campioni sorgente per campione d'uscita
    if (std::abs (ratio - 1.0) < 1e-9)
    {
        track->audio.setSize (2, length);
        for (int ch = 0; ch < 2; ++ch)
            track->audio.copyFrom (ch, 0, source, ch, 0, length);
    }
    else
    {
        const int outLength = (int) std::floor ((double) length / ratio);
        track->audio.setSize (2, outLength);
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::WindowedSincInterpolator interpolator;
            interpolator.process (ratio, source.getReadPointer (ch), track->audio.getWritePointer (ch), outLength);
            if (cancelled()) return nullptr;
        }
    }

    // 3) analisi completa, con gli stessi algoritmi del mix
    auto analyzer = std::make_unique<mc::MixAnalyzer>();
    analyzer->prepare (sampleRate, 2);
    const int total = track->audio.getNumSamples();
    for (int pos = 0; pos < total; pos += 8192)
    {
        const int n = juce::jmin (8192, total - pos);
        const float* data[2] = { track->audio.getReadPointer (0, pos), track->audio.getReadPointer (1, pos) };
        analyzer->process (data, n);
        if (cancelled()) return nullptr;
    }
    analyzer->fill (track->analysis);

    if (! track->analysis.valid)
    {
        error = "Il riferimento è troppo corto o quasi muto: servono almeno 5 secondi di musica.";
        return nullptr;
    }
    return track;
}
