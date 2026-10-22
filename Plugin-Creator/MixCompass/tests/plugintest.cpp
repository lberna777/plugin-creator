// plugintest — validazione offline di MIX COMPASS (il processore vero, senza host).
//
//   1. null-test: modalità A → uscita == ingresso, bit per bit (diff < -120 dBFS), mono e stereo,
//      44.1 / 48 / 96 kHz, blocchi di lunghezza casuale 1..1024
//   2. ZERO allocazioni dentro processBlock (contatore su operator new), in A e in B
//   3. riferimento WAV a 44.1 kHz caricato con host a 48 kHz (ricampionamento + analisi su thread)
//   4. A/B con level match: in B l'uscita ha il loudness del TUO mix (±1 LU); senza match quello del riferimento
//   5. ritorno ad A: di nuovo bit-identico
//   6. stato: il percorso del riferimento sopravvive a save/restore e si ricarica
//   7. bypass intatto; layout non supportati rifiutati
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "analysis/Loudness.h"
#include <cstdlib>
#include <new>
#include <random>

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
namespace
{
int failures = 0, checks = 0;

void check (bool ok, const char* what, const juce::String& detail = {})   // what: letterale UTF-8
{
    ++checks;
    if (! ok) ++failures;
    std::printf ("  [%s] %s %s\n", ok ? " OK " : "FAIL", what, detail.toRawUTF8());
}
void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    check (ok, what.toRawUTF8(), detail);
}

struct Pink
{
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    float next (double w)
    {
        b0 = 0.99886 * b0 + w * 0.0555179;  b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;  b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;  b5 = -0.7616 * b5 - w * 0.0168980;
        const double p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
        b6 = w * 0.115926;
        return (float) (p * 0.11);
    }
};

// Sorgente "mix": rosa stereo a un livello dato
struct Source
{
    explicit Source (float gain, unsigned seed) : g (gain), rng (seed) {}
    void fill (juce::AudioBuffer<float>& b, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float c = pc.next (nd (rng));
            const float l = c + 0.2f * pl.next (nd (rng)), r = c + 0.2f * pr.next (nd (rng));
            b.setSample (0, i, g * l);
            if (b.getNumChannels() > 1) b.setSample (1, i, g * r);
        }
    }
    float g;
    std::mt19937 rng;
    std::normal_distribution<double> nd { 0.0, 1.0 };
    Pink pc, pl, pr;
};

std::unique_ptr<MixCompassProcessor> makeProcessor (double sr, int channels, int maxBlock)
{
    auto p = std::make_unique<MixCompassProcessor>();
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (set);
    layout.outputBuses.add (set);
    p->setBusesLayout (layout);
    p->setRateAndBufferSizeDetails (sr, maxBlock);
    p->prepareToPlay (sr, maxBlock);
    return p;
}

void setParam (MixCompassProcessor& p, const char* id, bool on)
{
    p.apvts.getParameter (id)->setValueNotifyingHost (on ? 1.0f : 0.0f);
}

template <typename Cond>
bool waitFor (Cond c, int ms)
{
    for (int t = 0; t < ms; t += 10)
    {
        if (c()) return true;
        juce::Thread::sleep (10);
    }
    return c();
}

// Processa `seconds` di audio dalla sorgente; opzionalmente raccoglie loudness d'uscita e diff vs ingresso.
struct RunResult { double maxDiff = 0; long allocs = 0; };
RunResult runAudio (MixCompassProcessor& p, Source& src, double sr, int channels, double seconds,
                    mc::LoudnessMeter* outMeter = nullptr, bool realtimeish = true)
{
    RunResult res;
    juce::AudioBuffer<float> buf (channels, 1024), in (channels, 1024);
    juce::MidiBuffer midi;
    std::mt19937 rng (11);
    const long long total = (long long) (sr * seconds);
    long long done = 0, sinceSleep = 0;
    while (done < total)
    {
        const int n = (int) std::min<long long> (total - done, 1 + (long long) (rng() % 1024));
        buf.setSize (channels, n, false, false, true);
        in.setSize (channels, n, false, false, true);
        src.fill (in, n);
        for (int ch = 0; ch < channels; ++ch) buf.copyFrom (ch, 0, in, ch, 0, n);

        gAllocs = 0;
        gCounting = true;
        p.processBlock (buf, midi);
        gCounting = false;
        res.allocs += gAllocs.load();

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < n; ++i)
                res.maxDiff = std::max (res.maxDiff, (double) std::abs (buf.getSample (ch, i) - in.getSample (ch, i)));

        if (outMeter != nullptr)
        {
            const float* d[2] = { buf.getReadPointer (0), buf.getReadPointer (channels > 1 ? 1 : 0) };
            outMeter->process (d, n);
        }
        done += n;
        sinceSleep += n;
        // lascia al thread di analisi il tempo di svuotare il FIFO (2.7 s di capienza)
        if (realtimeish && sinceSleep > (long long) (sr * 0.5)) { juce::Thread::sleep (25); sinceSleep = 0; }
    }
    return res;
}

juce::String db (double linear)
{
    return linear <= 0 ? "-inf dBFS" : juce::String (20.0 * std::log10 (linear), 1) + " dBFS";
}

juce::File writeReferenceWav (const juce::File& dir)
{
    // 20 s di rosa stereo a 44.1 kHz, ~10 dB sopra il "mix" dei test: il level match deve abbassarlo
    auto file = dir.getChildFile ("mc_test_reference.wav");
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), 44100.0, 2, 24, {}, 0));
    if (w == nullptr) return {};
    os.release();
    Source s (0.9f, 99);
    juce::AudioBuffer<float> b (2, 4410);
    for (int k = 0; k < 200; ++k) { s.fill (b, 4410); w->writeFromAudioSampleBuffer (b, 0, 4410); }
    return file;
}
} // namespace

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    std::printf ("\n1-2) null-test e allocazioni in modalità A\n");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int ch : { 1, 2 })
        {
            auto p = makeProcessor (sr, ch, 1024);
            Source src (0.5f, 3);
            const auto r = runAudio (*p, src, sr, ch, 3.0, nullptr, false);
            const auto tag = juce::String (sr / 1000.0, 1) + " kHz " + (ch == 1 ? "mono  " : "stereo");
            check (r.maxDiff == 0.0 || 20.0 * std::log10 (r.maxDiff) < -120.0, "null-test " + tag, "diff max " + db (r.maxDiff));
            check (r.allocs == 0, "zero allocazioni " + tag, juce::String (r.allocs) + " new");
        }

    std::printf ("\n3) riferimento WAV 44.1 kHz con host a 48 kHz\n");
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory);
    const auto refFile = writeReferenceWav (tmp);
    check (refFile.existsAsFile(), "WAV di riferimento scritto", refFile.getFullPathName());

    const double sr = 48000.0;
    auto p = makeProcessor (sr, 2, 1024);
    p->loadReference (refFile);
    const bool loaded = waitFor ([&] { return p->getReferenceStatus().state == MixCompassProcessor::RefState::ready; }, 30000);
    const auto st = p->getReferenceStatus();
    check (loaded, "riferimento caricato e analizzato", st.message);
    if (! loaded) { std::printf ("\n%d/%d\n", checks - failures, checks); return 1; }
    const double refLufs = st.analysis->integrated;
    check (refLufs > -30 && refLufs < 0, "loudness del riferimento sensato", juce::String (refLufs, 2) + " LUFS");

    // il mix suona ~10 dB sotto il riferimento
    Source mix (0.28f, 7);
    {
        const auto r = runAudio (*p, mix, sr, 2, 8.0);
        check (r.maxDiff == 0.0, "con riferimento caricato, in A l'uscita resta identica", "diff " + db (r.maxDiff));
    }
    waitFor ([] { return false; }, 300);   // ultimo snapshot del thread di analisi

    // misura di controllo del mix, indipendente dal plugin
    double mixLufs;
    {
        mc::LoudnessMeter m;
        m.prepare (sr, 2);
        Source probe (0.28f, 7);
        juce::AudioBuffer<float> b (2, 48000);
        for (int k = 0; k < 8; ++k)
        {
            probe.fill (b, 48000);
            const float* d[2] = { b.getReadPointer (0), b.getReadPointer (1) };
            m.process (d, 48000);
        }
        mixLufs = m.getIntegrated();
    }
    std::printf ("    mix %.2f LUFS, riferimento %.2f LUFS\n", mixLufs, refLufs);

    std::printf ("\n4) A/B con level match\n");
    setParam (*p, ParamIds::listenRef, true);
    setParam (*p, ParamIds::levelMatch, true);
    {
        runAudio (*p, mix, sr, 2, 0.5);   // dentro il crossfade
        mc::LoudnessMeter out;
        out.prepare (sr, 2);
        const auto r = runAudio (*p, mix, sr, 2, 8.0, &out);
        const double got = out.getIntegrated();
        check (std::abs (got - mixLufs) <= 1.0, "B + match: uscita al loudness del TUO mix",
               juce::String (got, 2) + " LUFS vs mix " + juce::String (mixLufs, 2));
        check (r.maxDiff > 0.01, "B: si sente davvero il riferimento (uscita != ingresso)");
        check (r.allocs == 0, "B: zero allocazioni", juce::String (r.allocs) + " new");
    }
    setParam (*p, ParamIds::levelMatch, false);
    {
        runAudio (*p, mix, sr, 2, 0.5);
        mc::LoudnessMeter out;
        out.prepare (sr, 2);
        runAudio (*p, mix, sr, 2, 8.0, &out);
        const double got = out.getIntegrated();
        check (std::abs (got - refLufs) <= 1.0, "B senza match: uscita al loudness del riferimento",
               juce::String (got, 2) + " LUFS vs rif " + juce::String (refLufs, 2));
    }

    std::printf ("\n5) ritorno ad A\n");
    setParam (*p, ParamIds::listenRef, false);
    runAudio (*p, mix, sr, 2, 0.1);   // crossfade di 20 ms
    {
        const auto r = runAudio (*p, mix, sr, 2, 2.0);
        check (r.maxDiff == 0.0, "dopo il crossfade l'uscita è di nuovo identica", "diff " + db (r.maxDiff));
    }

    std::printf ("\n6) stato\n");
    {
        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = std::make_unique<MixCompassProcessor>();
        q->setStateInformation (state.getData(), (int) state.getSize());
        q->setRateAndBufferSizeDetails (96000.0, 512);
        q->prepareToPlay (96000.0, 512);
        const bool ok = waitFor ([&] { return q->getReferenceStatus().state == MixCompassProcessor::RefState::ready; }, 30000);
        check (ok, "il riferimento si ricarica dal progetto salvato (e a 96 kHz)", q->getReferenceStatus().name);
        check (! q->apvts.getParameter (ParamIds::levelMatch)->getValue(), "il parametro Match salvato (off) è ripristinato");
    }

    std::printf ("\n7) bypass e layout\n");
    {
        juce::AudioBuffer<float> b (2, 512), in (2, 512);
        Source s (0.5f, 5);
        s.fill (in, 512);
        b.makeCopyOf (in);
        juce::MidiBuffer midi;
        p->processBlockBypassed (b, midi);
        double d = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i) d = std::max (d, (double) std::abs (b.getSample (ch, i) - in.getSample (ch, i)));
        check (d == 0.0, "bypass: uscita identica");

        juce::AudioProcessor::BusesLayout bad;
        bad.inputBuses.add (juce::AudioChannelSet::mono());
        bad.outputBuses.add (juce::AudioChannelSet::stereo());
        check (! p->checkBusesLayoutSupported (bad), "mono in / stereo out rifiutato");
        juce::AudioProcessor::BusesLayout surround;
        surround.inputBuses.add (juce::AudioChannelSet::create5point1());
        surround.outputBuses.add (juce::AudioChannelSet::create5point1());
        check (! p->checkBusesLayoutSupported (surround), "5.1 rifiutato");
    }

    p->releaseResources();
    p.reset();
    refFile.deleteFile();
    std::printf ("\n%d/%d controlli passati\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
