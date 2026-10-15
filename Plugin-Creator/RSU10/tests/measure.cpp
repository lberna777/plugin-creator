// Banco di prova dell'RSU-10, senza DAW: ogni funzione del campionatore suonata e misurata.
#include "PluginProcessor.h"

using namespace rsu;

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
    if (! ok)
        ++failures;
}

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

// Motore con 16 uscite stereo in buffer propri
struct Rig
{
    PadParamStore params;
    SampleSlots slots;
    Engine engine { params, slots };
    juce::AudioBuffer<float> out { kNumOutputs * 2, kBlock };
    juce::AudioBuffer<float> in { 2, kBlock };
    EngineGlobals g;

    Rig() { engine.prepare (kRate, kBlock); in.clear(); }

    // Ritorna il picco sull'uscita o (canale sinistro) nei blocchi suonati
    float run (double seconds, juce::MidiBuffer midiFirst = {}, int outIndex = 0, std::vector<float>* capture = nullptr)
    {
        float peak = 0.0f;
        const int blocks = juce::jmax (1, int (seconds * kRate / kBlock));
        for (int b = 0; b < blocks; ++b)
        {
            OutputPointers op;
            for (int o = 0; o < kNumOutputs; ++o)
            {
                op.left[o] = out.getWritePointer (o * 2);
                op.right[o] = out.getWritePointer (o * 2 + 1);
            }
            const float* ins[2] { in.getReadPointer (0), in.getReadPointer (1) };
            engine.process (op, ins, 2, kBlock, b == 0 ? midiFirst : juce::MidiBuffer(), g);
            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                for (int i = 0; i < kBlock; ++i)
                    if (! std::isfinite (out.getSample (ch, i)))
                        nanFound = true;
            peak = juce::jmax (peak, out.getMagnitude (outIndex * 2, 0, kBlock));
            if (capture != nullptr)
                capture->insert (capture->end(), out.getReadPointer (outIndex * 2), out.getReadPointer (outIndex * 2) + kBlock);
        }
        return peak;
    }

    static juce::MidiBuffer note (int n, int vel = 100)
    {
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) vel), 0);
        return m;
    }
    static juce::MidiBuffer off (int n)
    {
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOff (1, n), 0);
        return m;
    }

    bool nanFound = false;
};

Sample::Ptr sine (double freq, double seconds, double rate = 44100.0)
{
    juce::AudioBuffer<float> b (1, int (seconds * rate));
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / rate));
    return new Sample (std::move (b), rate, "sine" + juce::String (freq));
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::cout << "RSU-10 banco di prova\n";

    // ---- 1. kit di prova: suona e si ferma
    {
        Rig r;
        auto kit = makeDemoKit();
        check (kit.size() == 16, "kit di prova: 16 suoni");
        for (size_t i = 0; i < kit.size(); ++i)
            r.slots.set (int (i), 0, kit[i]);
        const float peak = r.run (0.3, Rig::note (kFirstNote));
        check (peak > 0.3f, "C1 suona il pad A1 (picco " + juce::String (peak, 2) + ")");
        r.run (1.0);
        check (r.engine.padVoices[0].load() == 0, "la cassa finisce da sola (one-shot)");
        check (r.run (0.1) < 1.0e-6f, "silenzio dopo la fine");
        check (! r.nanFound, "niente NaN");
    }

    // ---- 2. choke: il charleston chiuso zittisce l'aperto
    {
        Rig r;
        auto kit = makeDemoKit();
        r.slots.set (3, 0, kit[3]);
        r.slots.set (4, 0, kit[4]);
        r.params.set (3, PP::Choke, 1);
        r.params.set (4, PP::Choke, 1);
        r.run (0.05, Rig::note (kFirstNote + 4));
        check (r.engine.padVoices[4].load() == 1, "charleston aperto suona");
        r.run (0.05, Rig::note (kFirstNote + 3));
        check (r.engine.padVoices[4].load() == 0, "il chiuso lo zittisce (choke 1)");
    }

    // ---- 3. strati di velocity
    {
        Rig r;
        r.slots.set (20, 0, sine (200, 1.0));
        r.slots.set (20, 1, sine (400, 1.0));
        r.params.set (20, PP::Split2, 64);
        r.run (0.02, Rig::note (kFirstNote + 20, 30));
        check (r.engine.padLastLayer[20].load() == 0, "velocity 30 -> strato 1");
        r.run (0.02, Rig::note (kFirstNote + 20, 100));
        check (r.engine.padLastLayer[20].load() == 1, "velocity 100 -> strato 2");
    }

    // ---- 4. reverse: la testina parte dalla fine e scende
    {
        Rig r;
        r.slots.set (0, 0, sine (300, 1.0));
        r.params.set (0, PP::Reverse, 1);
        r.run (0.1, Rig::note (kFirstNote));
        const float p1 = r.engine.padPosition[0].load();
        r.run (0.1);
        const float p2 = r.engine.padPosition[0].load();
        check (p1 > 0.8f && p2 < p1, "reverse: posizione " + juce::String (p1, 2) + " -> " + juce::String (p2, 2));
    }

    // ---- 5. stretch: un loop di 2 s (4 battute a 120) suonato a 60 BPM dura 4 s
    {
        Rig r;
        r.slots.set (0, 0, sine (220, 2.0));
        r.params.set (0, PP::Stretch, float (Stretch::Beats4));
        r.params.set (0, PP::FadeOut, 0);
        r.g.hostBpm = 60.0;
        r.run (3.0, Rig::note (kFirstNote));
        check (r.engine.padVoices[0].load() == 1, "stretch: a 3 s suona ancora");
        r.run (1.3);
        check (r.engine.padVoices[0].load() == 0, "stretch: finito entro 4,3 s");
        check (std::abs (stretchHeadRate (Stretch::Auto, 2.0, 88200, 120.0, 44100.0) - 1.0) < 1e-9, "stretch AUTO al tempo giusto = velocità 1");
    }

    // ---- 6. grade EXTRA LONG: campioni tenuti (11,64 kHz)
    {
        Rig r;
        r.slots.set (0, 0, sine (1000, 0.5, kRate));
        r.params.set (0, PP::Grade, float (Grade::ExtraLong));
        std::vector<float> cap;
        r.run (0.2, Rig::note (kFirstNote), 0, &cap);
        int repeats = 0;
        for (size_t i = 1000; i < 6000; ++i)
            repeats += juce::exactlyEqual (cap[i], cap[i - 1]) ? 1 : 0;
        check (repeats > 3000, "grade: " + juce::String (repeats) + "/5000 campioni tenuti");
    }

    // ---- 7. ribbon scratch: dito fermo = disco fermo (silenzio)
    {
        Rig r;
        r.slots.set (0, 0, sine (300, 2.0));
        r.params.set (0, PP::PlayMode, float (PlayMode::Gate));
        r.run (0.1, Rig::note (kFirstNote));
        r.g.ribbonTouch = true;
        r.g.ribbonMode = RibbonMode::Scratch;
        r.run (0.2);
        const float still = r.run (0.1);
        check (still < 0.02f, "scratch fermo: picco " + juce::String (still, 4));
        r.g.ribbonTouch = false;
        check (r.run (0.2) > 0.2f, "staccato il dito riparte");
    }

    // ---- 8. modo scala: un'ottava sopra = metà durata
    {
        Rig r;
        r.slots.set (5, 0, sine (300, 1.0));
        r.g.scale = true;
        r.g.scalePad = 5;
        r.g.scaleRoot = 60;
        r.run (0.6, Rig::note (72));
        check (r.engine.padVoices[5].load() == 0, "scala: C4 suona il pad un'ottava sopra (finisce in 0,5 s)");
        r.run (0.6, Rig::note (60));
        check (r.engine.padVoices[5].load() == 1, "scala: C3 alla velocità originale (ancora attivo a 0,6 s)");
    }

    // ---- 9. uscite separate
    {
        Rig r;
        r.slots.set (0, 0, sine (300, 1.0));
        r.params.set (0, PP::Output, 3);
        r.run (0.1, Rig::note (kFirstNote));
        const float onMain = r.out.getMagnitude (0, 0, kBlock), onOut4 = r.out.getMagnitude (6, 0, kBlock);
        check (onMain < 1e-6f && onOut4 > 0.1f, "pad su OUT 4: la principale resta muta");
    }

    // ---- 10. registrazione dall'uscita (ricampionamento)
    {
        Rig r;
        r.slots.set (0, 0, sine (300, 1.0));
        r.engine.armRecording (Engine::RecSource::Output, 0.01f);
        r.run (0.3, Rig::note (kFirstNote));
        r.engine.stopRecording();
        r.run (0.01);
        juce::AudioBuffer<float> rec;
        double rate = 0;
        const bool took = r.engine.takeRecording (rec, rate);
        check (took && rec.getNumSamples() > int (0.2 * kRate), "REC dall'uscita: " + juce::String (rec.getNumSamples() / kRate, 2) + " s");
    }

    // ---- 11. carico: 48 voci con filtro, saturazione, compressore, stretch
    {
        Rig r;
        for (int p = 0; p < kNumPads; ++p)
        {
            r.slots.set (p, 0, sine (100 + p * 20, 3.0));
            r.params.set (p, PP::FilterType, 1);
            r.params.set (p, PP::Drive, 0.5f);
            r.params.set (p, PP::Comp, 0.5f);
            if (p % 4 == 0)
                r.params.set (p, PP::Stretch, float (Stretch::Auto));
        }
        juce::MidiBuffer many;
        for (int v = 0; v < Engine::kMaxVoices; ++v)
            many.addEvent (juce::MidiMessage::noteOn (1, kFirstNote + v % kNumPads, (juce::uint8) 100), v);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        r.run (5.0, many);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const double load = ms / 5000.0 * 100.0;
        std::cout << "        CPU con 48 voci: " << juce::String (load, 1) << "% di un core\n";
        check (load < 60.0, "48 voci in tempo reale");
        check (! r.nanFound, "niente NaN sotto carico");
    }

    // ---- 12. stato del progetto e kit su disco
    {
        RSU10AudioProcessor a;
        a.pads.set (2, PP::Tune, 7);
        a.pads.set (40, PP::Cutoff, 1234);
        juce::MemoryBlock state;
        a.getStateInformation (state);
        RSU10AudioProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        check (b.pads.getInt (2, PP::Tune) == 7 && std::abs (b.pads.get (40, PP::Cutoff) - 1234.0f) < 1.0f, "stato: parametri dei pad ripristinati");
        check (b.samples.get (0, 0) != nullptr && b.samples.get (0, 0)->demoIndex == 0, "stato: kit di prova rigenerato");

        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rsu10_test_kit");
        dir.deleteRecursively();
        dir.createDirectory();
        juce::String err;
        const bool saved = a.saveKit (dir.getChildFile ("Prova.rsu10kit"), err);
        check (saved, "kit salvato " + err);
        RSU10AudioProcessor c;
        c.samples.clearAll();
        const bool loaded = c.loadKit (dir.getChildFile ("Prova.rsu10kit"), err);
        check (loaded && c.samples.get (4, 0) != nullptr && c.pads.getInt (2, PP::Tune) == 7, "kit ricaricato dai WAV copiati");
        check (dir.getChildFile ("Prova - campioni").findChildFiles (juce::File::findFiles, false).size() == 16, "16 campioni copiati accanto al kit");
        dir.deleteRecursively();
    }

    std::cout << (failures == 0 ? "TUTTO OK\n" : juce::String (failures) + " test falliti\n");
    return failures == 0 ? 0 : 1;
}
