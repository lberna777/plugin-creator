// synthtest — validazione DSP offline di Macchine Pisello + render di WAV dimostrativi.
//
//   ./synthtest [cartella_output]
//
// Per uno strumento il "null-test" diventa:
//   1. silenzio ESATTO senza MIDI e a transport fermo
//   2. determinismo: due istanze, stesso input → diff < -120 dBFS
//   3. invarianza al block size: 64 vs 512 vs variabile → diff < -120 dBFS
//   4. ZERO allocazioni dentro processBlock (contatore su operator new)
//   5. 44.1/48/96 kHz × mono/stereo × block 32..1024: finito, non silenzioso, niente clip folle
//   6. automazione aggressiva (cutoff/delay/drone a ogni blocco): nessun NaN, nessun salto
//   7. bypass = silenzio; state save/restore round-trip

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <cstdlib>
#include <new>

//==============================================================================
static thread_local bool gCounting = false;
static std::atomic<long> gAllocs { 0 };

void* operator new (std::size_t n)
{
    if (gCounting) ++gAllocs;
    if (auto* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

//==============================================================================
struct FakeHead : juce::AudioPlayHead
{
    bool playing = false;
    double bpm = 120.0, sampleRate = 44100.0;
    int64_t samplePos = 0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (playing);
        info.setTimeInSamples (samplePos);
        info.setPpqPosition ((double) samplePos * bpm / (60.0 * sampleRate));
        return info;
    }
};

struct Rig
{
    std::unique_ptr<MacchinePiselloProcessor> proc = std::make_unique<MacchinePiselloProcessor>();
    FakeHead head;
    int channels;
    double sr;

    Rig (double sampleRate, int numChannels, int maxBlock) : channels (numChannels), sr (sampleRate)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.outputBuses.add (numChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        const bool ok = proc->setBusesLayout (layout);
        jassert (ok); juce::ignoreUnused (ok);
        head.sampleRate = sampleRate;
        proc->setPlayHead (&head);
        proc->setRateAndBufferSizeDetails (sampleRate, maxBlock);
        proc->prepareToPlay (sampleRate, maxBlock);
    }

    void set (const juce::String& id, float plainValue)
    {
        auto* p = proc->apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
    }
};

using NoteEvent = std::tuple<int64_t, int, bool>;   // sample, nota, on

// Renderizza 'seconds' secondi. blockFn restituisce la dimensione del prossimo blocco.
static juce::AudioBuffer<float> render (Rig& rig, double seconds, std::function<int()> blockFn,
                                        const std::vector<NoteEvent>& notes = {},
                                        std::function<void (Rig&, int64_t)> perBlock = {},
                                        long* allocsOut = nullptr)
{
    const int total = (int) (seconds * rig.sr);
    juce::AudioBuffer<float> out (rig.channels, total);
    juce::AudioBuffer<float> block (rig.channels, 4096);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);
    long allocs = 0;

    int64_t pos = 0;
    while (pos < total)
    {
        const int n = (int) juce::jmin<int64_t> (blockFn(), total - pos);
        if (perBlock) perBlock (rig, pos);

        midi.clear();
        for (auto& [s, note, on] : notes)
            if (s >= pos && s < pos + n)
                midi.addEvent (on ? juce::MidiMessage::noteOn (1, note, (juce::uint8) 100)
                                  : juce::MidiMessage::noteOff (1, note), (int) (s - pos));

        block.setSize (rig.channels, n, false, false, true);
        rig.head.samplePos = pos;

        gAllocs = 0;
        gCounting = true;
        rig.proc->processBlock (block, midi);
        gCounting = false;
        allocs += gAllocs.load();

        for (int ch = 0; ch < rig.channels; ++ch)
            out.copyFrom (ch, (int) pos, block, ch, 0, n);
        pos += n;
    }
    if (allocsOut) *allocsOut = allocs;
    return out;
}

static float peakDb (const juce::AudioBuffer<float>& b)
{
    float m = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch) m = juce::jmax (m, b.getMagnitude (ch, 0, b.getNumSamples()));
    return juce::Decibels::gainToDecibels (m, -300.0f);
}

static float rmsDb (const juce::AudioBuffer<float>& b)
{
    double sum = 0.0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i) sum += (double) b.getSample (ch, i) * b.getSample (ch, i);
    const double rms = std::sqrt (sum / juce::jmax (1, b.getNumChannels() * b.getNumSamples()));
    return juce::Decibels::gainToDecibels ((float) rms, -300.0f);
}

static float diffDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    float m = 0.0f;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            m = juce::jmax (m, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    return juce::Decibels::gainToDecibels (m, -300.0f);
}

static bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i))) return false;
    return true;
}

// Massimo salto sample-to-sample: rivela click/zipper grossolani.
static float maxStep (const juce::AudioBuffer<float>& b)
{
    float m = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 1; i < b.getNumSamples(); ++i)
            m = juce::jmax (m, std::abs (b.getSample (ch, i) - b.getSample (ch, i - 1)));
    return m;
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (auto* w = wav.createWriterFor (os.get(), sr, (unsigned) b.getNumChannels(), 24, {}, 0))
    {
        os.release();
        std::unique_ptr<juce::AudioFormatWriter> writer (w);
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
}

//==============================================================================
static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (! ok) ++failures;
}

static std::function<int()> fixed (int n) { return [n] { return n; }; }

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "test_out");
    outDir.createDirectory();

    std::cout << "== 1. silenzio esatto (niente MIDI, transport fermo) ==\n";
    for (double sr : { 44100.0, 96000.0 })
        for (int ch : { 1, 2 })
        {
            Rig rig (sr, ch, 512);
            auto b = render (rig, 3.0, fixed (512));
            check (peakDb (b) <= -300.0f, juce::String (sr, 0) + " Hz, " + juce::String (ch) + " ch: peak "
                                          + juce::String (peakDb (b), 1) + " dBFS");
        }

    std::cout << "== 2/3. determinismo e invarianza al block size (transport in play, 123 BPM) ==\n";
    {
        auto make = [] (int block, bool variable)
        {
            Rig rig (48000.0, 2, 1024);
            rig.head.playing = true; rig.head.bpm = 123.0;
            juce::Random r (99);
            std::function<int()> fn = variable ? std::function<int()> ([r]() mutable { return 1 + r.nextInt (1024); })
                                               : fixed (block);
            return render (rig, 6.0, fn, { { 48000, 55, true }, { 200000, 55, false } });
        };
        const auto a = make (512, false);
        const auto b = make (512, false);
        const auto c = make (64, false);
        const auto d = make (0, true);
        check (diffDb (a, b) < -120.0f, "due istanze identiche: diff " + juce::String (diffDb (a, b), 1) + " dBFS");
        check (diffDb (a, c) < -120.0f, "block 512 vs 64: diff " + juce::String (diffDb (a, c), 1) + " dBFS");
        check (diffDb (a, d) < -120.0f, "block 512 vs variabile 1..1024: diff " + juce::String (diffDb (a, d), 1) + " dBFS");
        check (rmsDb (a) > -50.0f, "la sequenza suona: RMS " + juce::String (rmsDb (a), 1) + " dBFS");
    }

    std::cout << "== 4/5. sample rate × bus × block, allocazioni ==\n";
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int ch : { 1, 2 })
            for (int block : { 32, 512, 1024 })
            {
                Rig rig (sr, ch, block);
                rig.head.playing = true;
                long allocs = 0;
                auto b = render (rig, 4.0, fixed (block), {}, {}, &allocs);
                const juce::String tag = juce::String (sr, 0) + " Hz " + juce::String (ch) + "ch blk " + juce::String (block);
                check (allFinite (b) && rmsDb (b) > -50.0f && peakDb (b) < 0.0f && allocs == 0,
                       tag + ": RMS " + juce::String (rmsDb (b), 1) + ", peak " + juce::String (peakDb (b), 1)
                       + " dBFS, alloc " + juce::String (allocs));
            }

    std::cout << "== MIDI (RUN off): mono synth ==\n";
    {
        Rig rig (44100.0, 2, 256);
        rig.set (pid::seqOn, 0.0f);
        auto b = render (rig, 3.0, fixed (256), { { 1000, 60, true }, { 30000, 63, true }, { 60000, 63, false },
                                                  { 70000, 60, false } });
        check (allFinite (b) && rmsDb (b) > -50.0f, "note MIDI suonano: RMS " + juce::String (rmsDb (b), 1) + " dBFS");
    }
    {
        Rig rig (44100.0, 2, 256);   // RUN on, transport fermo: la nota tenuta avvia il clock libero
        auto b = render (rig, 3.0, fixed (256), { { 1000, 50, true }, { 100000, 50, false } });
        check (rmsDb (b) > -50.0f, "RUN + nota tenuta a transport fermo: il sequencer parte (RMS "
                                   + juce::String (rmsDb (b), 1) + " dBFS)");
    }

    std::cout << "== 6. automazione aggressiva ==\n";
    {
        Rig rig (44100.0, 2, 128);
        rig.head.playing = true;
        rig.set (pid::drone, 1.0f);
        juce::Random r (5);
        long allocs = 0;
        auto b = render (rig, 8.0, fixed (128), {},
                         [&r] (Rig& rg, int64_t)
                         {
                             rg.set (pid::cutoff, 20.0f + r.nextFloat() * 17980.0f);
                             rg.set (pid::delayTime, 10.0f + r.nextFloat() * 1990.0f);
                             rg.set (pid::fold, r.nextFloat());
                             rg.set (pid::volume, -30.0f + r.nextFloat() * 30.0f);
                             rg.set (pid::drone, r.nextBool() ? 1.0f : 0.0f);
                         }, &allocs);
        check (allFinite (b) && peakDb (b) < 6.0f && allocs == 0,
               "random automation ogni 128 sample: finito, peak " + juce::String (peakDb (b), 1) + " dBFS, alloc "
               + juce::String (allocs));
        check (maxStep (b) < 1.0f, "nessun salto enorme sample-to-sample (max " + juce::String (maxStep (b), 3) + ")");
    }

    std::cout << "== 7. bypass e stato ==\n";
    {
        Rig rig (44100.0, 2, 512);
        rig.head.playing = true;
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer m;
        for (int i = 0; i < 50; ++i) rig.proc->processBlock (b, m);
        rig.proc->processBlockBypassed (b, m);
        check (b.getMagnitude (0, 512) == 0.0f, "processBlockBypassed = silenzio");

        rig.set (pid::cutoff, 1234.0f);
        rig.set (pid::seqScale, 5.0f);
        juce::MemoryBlock state;
        rig.proc->getStateInformation (state);
        Rig other (44100.0, 2, 512);
        other.proc->setStateInformation (state.getData(), (int) state.getSize());
        const float c = other.proc->apvts.getRawParameterValue (pid::cutoff)->load();
        const float s = other.proc->apvts.getRawParameterValue (pid::seqScale)->load();
        check (std::abs (c - 1234.0f) < 0.5f && (int) s == 5, "state round-trip (cutoff " + juce::String (c, 1) + ")");
    }

    std::cout << "== demo WAV ==\n";
    {
        Rig rig (48000.0, 2, 512);
        rig.head.playing = true; rig.head.bpm = 96.0;
        auto b = render (rig, 24.0, fixed (512));
        writeWav (outDir.getChildFile ("demo_1_sequenza.wav"), b, 48000.0);
    }
    {
        Rig rig (48000.0, 2, 512);
        rig.set (pid::seqOn, 0.0f);   rig.set (pid::drone, 1.0f);
        rig.set (pid::oscWave, 0.55f); rig.set (pid::osc2Detune, 7.03f);
        rig.set (pid::cutoff, 380.0f); rig.set (pid::reso, 0.6f); rig.set (pid::lfoRate, 0.07f);
        rig.set (pid::lfoCutoff, 0.55f); rig.set (pid::lfoFold, 0.5f); rig.set (pid::lfoShape, 5.0f);
        rig.set (pid::drift, 0.8f);   rig.set (pid::spaceSize, 0.92f); rig.set (pid::spaceMix, 0.7f);
        rig.set (pid::delayTime, 820.0f); rig.set (pid::delayFb, 0.7f); rig.set (pid::delayMix, 0.35f);
        auto b = render (rig, 30.0, fixed (512), { { 2400, 38, true }, { 1400000, 38, false } });
        writeWav (outDir.getChildFile ("demo_2_drone.wav"), b, 48000.0);
    }
    {
        Rig rig (48000.0, 2, 512);
        rig.head.playing = true; rig.head.bpm = 132.0;
        rig.set (pid::seqMutate, 0.7f);  rig.set (pid::seqRatchet, 0.45f); rig.set (pid::chaos, 0.8f);
        rig.set (pid::seqProb, 0.7f);    rig.set (pid::fold, 0.5f);        rig.set (pid::lfoShape, 4.0f);
        rig.set (pid::lfoRate, 6.0f);    rig.set (pid::lfoFold, 0.7f);     rig.set (pid::seqScale, 4.0f);
        rig.set (pid::decay, 160.0f);    rig.set (pid::reso, 0.72f);       rig.set (pid::seqSwing, 0.25f);
        rig.set (pid::filterDrive, 0.8f); rig.set (pid::delayTime, 340.0f);
        auto b = render (rig, 24.0, fixed (512));
        writeWav (outDir.getChildFile ("demo_3_freaky.wav"), b, 48000.0);
    }
    std::cout << "  scritti in " << outDir.getFullPathName() << "\n";

    std::cout << (failures == 0 ? "\nTUTTI I TEST PASSATI\n" : "\nTEST FALLITI: " + std::to_string (failures) + "\n");
    return failures == 0 ? 0 : 1;
}
