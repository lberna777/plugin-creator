#include "Sample.h"

namespace rsu
{
// ---------------------------------------------------------------- Sample

Sample::Sample (juce::AudioBuffer<float>&& audio, double rate, juce::String n, juce::File source)
    : data (std::move (audio)), sampleRate (rate > 0.0 ? rate : 44100.0), sampleName (std::move (n)), sourceFile (std::move (source))
{
    peakData.assign (kPeakBins, { 0.0f, 0.0f });
    const int len = data.getNumSamples();
    if (len == 0)
        return;

    for (int b = 0; b < kPeakBins; ++b)
    {
        const int s0 = int ((int64_t) b * len / kPeakBins);
        const int s1 = juce::jmax (s0 + 1, int ((int64_t) (b + 1) * len / kPeakBins));
        float lo = 0.0f, hi = 0.0f;
        for (int ch = 0; ch < data.getNumChannels(); ++ch)
        {
            auto r = data.findMinMax (ch, s0, juce::jmin (s1, len) - s0);
            lo = juce::jmin (lo, r.getStart());
            hi = juce::jmax (hi, r.getEnd());
        }
        peakData[size_t (b)] = { lo, hi };
    }
}

int Sample::guessBeats (double secs) noexcept
{
    // Il numero di battute (potenza di 2) che dà un tempo tra 80 e 160 BPM
    for (int beats = 1; beats <= 64; beats *= 2)
    {
        const double bpm = bpmFor (secs, beats);
        if (bpm >= 80.0 && bpm < 160.0)
            return beats;
    }
    return 4;
}

juce::AudioFormatManager& Sample::formats()
{
    static juce::AudioFormatManager fm;
    static std::once_flag once;
    std::call_once (once, [] { fm.registerBasicFormats(); });
    return fm;
}

bool Sample::isAudioFile (const juce::File& f)
{
    return f.existsAsFile() && f.hasFileExtension ("wav;wave;aif;aiff;flac;ogg;mp3;m4a;caf");
}

Sample::Ptr Sample::loadFromFile (const juce::File& f, juce::String* error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats().createReaderFor (f));
    if (reader == nullptr)
    {
        if (error != nullptr)
            *error = "Formato non letto: " + f.getFileName();
        return nullptr;
    }

    constexpr double maxSeconds = 600.0;
    const auto len = (int) juce::jmin<int64_t> (reader->lengthInSamples, int64_t (maxSeconds * reader->sampleRate));
    const int chans = juce::jlimit (1, 2, (int) reader->numChannels);
    if (len <= 0)
    {
        if (error != nullptr)
            *error = "File vuoto: " + f.getFileName();
        return nullptr;
    }

    juce::AudioBuffer<float> buf (chans, len);
    reader->read (&buf, 0, len, 0, true, chans > 1);
    return new Sample (std::move (buf), reader->sampleRate, f.getFileNameWithoutExtension(), f);
}

// ---------------------------------------------------------------- SampleSlots

SampleSlots::SampleSlots()
{
    for (auto& pad : slots)
        for (auto& s : pad)
            s.store (nullptr);
}

int SampleSlots::layerCount (int pad) const noexcept
{
    int n = 0;
    for (int l = 0; l < kMaxLayers; ++l)
        if (get (pad, l) != nullptr)
            n = l + 1;
    return n;
}

void SampleSlots::set (int pad, int layer, Sample::Ptr s)
{
    const juce::ScopedLock sl (lock);
    if (s != nullptr)
        kept.push_back ({ s, false, 0.0 });
    auto* old = slots[size_t (pad)][size_t (layer)].exchange (s.get(), std::memory_order_acq_rel);

    // Il vecchio campione resta vivo finché nessun posto lo usa e il thread audio l'ha mollato
    if (old != nullptr)
        for (auto& k : kept)
            if (k.sample.get() == old && ! k.retired)
            {
                k.retired = true;
                k.retiredAt = juce::Time::getMillisecondCounterHiRes();
                break;
            }
    ++generation;
}

void SampleSlots::clearPad (int pad)
{
    for (int l = 0; l < kMaxLayers; ++l)
        set (pad, l, nullptr);
}

void SampleSlots::clearAll()
{
    for (int p = 0; p < kNumPads; ++p)
        clearPad (p);
}

void SampleSlots::purge()
{
    const juce::ScopedLock sl (lock);
    const double now = juce::Time::getMillisecondCounterHiRes();

    auto inUse = [this] (const Sample* s)
    {
        for (auto& pad : slots)
            for (auto& slot : pad)
                if (slot.load() == s)
                    return true;
        return false;
    };

    kept.erase (std::remove_if (kept.begin(), kept.end(), [&] (Kept& k)
    {
        if (! k.retired || now - k.retiredAt < 1000.0)
            return false;
        if (inUse (k.sample.get()))
        {
            k.retired = false;          // rimesso in un posto (es. undo, copia): resta
            return false;
        }
        return k.sample->getReferenceCount() == 1;   // nessuna voce lo sta suonando
    }), kept.end());
}

// ---------------------------------------------------------------- kit di prova

namespace
{
struct Synth
{
    double rate;
    juce::Random rng { 1995 };

    juce::AudioBuffer<float> make (double seconds) const
    {
        juce::AudioBuffer<float> b (1, juce::jmax (1, int (seconds * rate)));
        b.clear();
        return b;
    }
    float noise() { return rng.nextFloat() * 2.0f - 1.0f; }
    static float expEnv (double t, double tau) { return (float) std::exp (-t / tau); }
};

Sample::Ptr finish (juce::AudioBuffer<float>&& b, double rate, const char* name, float gain = 0.9f)
{
    const float peak = b.getMagnitude (0, b.getNumSamples());
    if (peak > 0.0f)
        b.applyGain (gain / peak);
    // micro-fade finale contro i click
    const int n = juce::jmin (64, b.getNumSamples());
    b.applyGainRamp (b.getNumSamples() - n, n, 1.0f, 0.0f);
    return new Sample (std::move (b), rate, name);
}

// Sinusoide con glissando esponenziale f0 -> f1 (costante di tempo tau)
void sweepSine (juce::AudioBuffer<float>& b, double rate, double f0, double f1, double tauPitch, double tauAmp, float amp = 1.0f)
{
    double phase = 0.0;
    auto* d = b.getWritePointer (0);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / rate;
        const double f = f1 + (f0 - f1) * std::exp (-t / tauPitch);
        phase += juce::MathConstants<double>::twoPi * f / rate;
        d[i] += amp * (float) std::sin (phase) * Synth::expEnv (t, tauAmp);
    }
}

void addNoise (Synth& s, juce::AudioBuffer<float>& b, double tauAmp, float amp, float hp, double delay = 0.0)
{
    auto* d = b.getWritePointer (0);
    float prev = 0.0f, lp = 0.0f;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / s.rate - delay;
        if (t < 0.0)
            continue;
        const float n = s.noise();
        const float h = n - prev;              // passa-alto grezzo
        prev = n;
        lp += 0.5f * (n - lp);
        d[i] += amp * (hp * h + (1.0f - hp) * lp) * Synth::expEnv (t, tauAmp);
    }
}
} // namespace

std::vector<Sample::Ptr> makeDemoKit (double rate)
{
    Synth s { rate };
    std::vector<Sample::Ptr> kit;

    { auto b = s.make (0.6);  sweepSine (b, rate, 160, 45, 0.03, 0.18); addNoise (s, b, 0.003, 0.4f, 1.0f); kit.push_back (finish (std::move (b), rate, "Cassa")); }
    { auto b = s.make (0.3);  sweepSine (b, rate, 240, 180, 0.02, 0.05, 0.7f); addNoise (s, b, 0.07, 0.8f, 0.6f); kit.push_back (finish (std::move (b), rate, "Rullante")); }
    {
        auto b = s.make (0.4);
        for (int k = 0; k < 3; ++k)
            addNoise (s, b, 0.008, 0.8f, 0.7f, k * 0.011);
        addNoise (s, b, 0.09, 0.6f, 0.7f, 0.033);
        kit.push_back (finish (std::move (b), rate, "Clap"));
    }
    { auto b = s.make (0.08); addNoise (s, b, 0.018, 1.0f, 1.0f); kit.push_back (finish (std::move (b), rate, "Charleston chiuso", 0.6f)); }
    { auto b = s.make (0.6);  addNoise (s, b, 0.16, 1.0f, 1.0f); kit.push_back (finish (std::move (b), rate, "Charleston aperto", 0.6f)); }
    { auto b = s.make (0.08); sweepSine (b, rate, 1800, 1600, 0.01, 0.012); addNoise (s, b, 0.004, 0.5f, 1.0f); kit.push_back (finish (std::move (b), rate, "Rim")); }
    { auto b = s.make (0.5);  sweepSine (b, rate, 130, 95, 0.06, 0.14); kit.push_back (finish (std::move (b), rate, "Tom basso")); }
    { auto b = s.make (0.4);  sweepSine (b, rate, 200, 150, 0.05, 0.11); kit.push_back (finish (std::move (b), rate, "Tom alto")); }
    {
        auto b = s.make (1.8);
        sweepSine (b, rate, 70, 49, 0.08, 0.6);
        auto* d = b.getWritePointer (0);
        for (int i = 0; i < b.getNumSamples(); ++i)
            d[i] = std::tanh (2.5f * d[i]);
        kit.push_back (finish (std::move (b), rate, "808"));
    }
    { auto b = s.make (2.0);  addNoise (s, b, 0.6, 1.0f, 0.95f); kit.push_back (finish (std::move (b), rate, "Crash", 0.5f)); }
    {
        auto b = s.make (0.14);
        addNoise (s, b, 0.05, 1.0f, 1.0f);
        b.applyGainRamp (0, juce::jmin (b.getNumSamples(), int (0.02 * rate)), 0.0f, 1.0f);
        kit.push_back (finish (std::move (b), rate, "Shaker", 0.5f));
    }
    {
        auto b = s.make (0.35);
        auto* d = b.getWritePointer (0);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / rate;
            const float sq = (std::sin (6.2831853 * 545 * t) > 0 ? 1.0f : -1.0f) + (std::sin (6.2831853 * 815 * t) > 0 ? 1.0f : -1.0f);
            d[i] = 0.5f * sq * Synth::expEnv (t, 0.07);
        }
        kit.push_back (finish (std::move (b), rate, "Campanaccio", 0.6f));
    }
    { auto b = s.make (0.1);  sweepSine (b, rate, 950, 900, 0.01, 0.025); kit.push_back (finish (std::move (b), rate, "Blip")); }
    { auto b = s.make (0.25); sweepSine (b, rate, 3200, 90, 0.03, 0.08); kit.push_back (finish (std::move (b), rate, "Zap")); }
    {
        auto b = s.make (0.7);
        auto* d = b.getWritePointer (0);
        const double freqs[] { 261.63, 311.13, 392.0, 523.25 };   // Do minore
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / rate;
            float v = 0.0f;
            for (double f : freqs)
                v += (float) (2.0 * std::fmod (t * f, 1.0) - 1.0);   // dente di sega
            d[i] = v * 0.25f * Synth::expEnv (t, 0.18);
        }
        kit.push_back (finish (std::move (b), rate, "Accordo", 0.7f));
    }
    { auto b = s.make (0.12); addNoise (s, b, 0.012, 1.0f, 0.5f); sweepSine (b, rate, 2200, 1800, 0.01, 0.01, 0.4f); kit.push_back (finish (std::move (b), rate, "Schiocco")); }

    for (size_t i = 0; i < kit.size(); ++i)
        kit[i]->demoIndex = int (i);
    return kit;
}
} // namespace rsu
