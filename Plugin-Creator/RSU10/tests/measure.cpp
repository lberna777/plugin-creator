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
        r.engine.muted[0] = true;
        check (r.run (0.3, Rig::note (kFirstNote)) < 1.0e-6f, "MUTE nel motore: nemmeno l'attacco passa");
        r.engine.muted[0] = false;
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

    // ---- 13. funzioni Maschine e scale, attraverso il processor intero (come in FL)
    {
        RSU10AudioProcessor p;
        p.prepareToPlay (kRate, kBlock);
        juce::AudioBuffer<float> buf (2, kBlock);
        auto block = [&] (juce::MidiBuffer m) { buf.clear(); p.processBlock (buf, m); return buf.getMagnitude (0, 0, kBlock); };
        auto cc = [] (int n, int v) { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (16, n, v), 0); return m; };
        auto playPeak = [&] (int note)
        {
            float peak = block (Rig::note (note));
            for (int i = 0; i < 20; ++i) peak = juce::jmax (peak, block ({}));
            block (Rig::off (note));
            for (int i = 0; i < 100; ++i) block ({});
            return peak;
        };

        check (playPeak (kFirstNote) > 0.1f, "processor: A1 suona");
        block (cc (45, 127));                         // MUTE tenuto sulla Mikro
        const float duringMute = block (Rig::note (kFirstNote));
        block (Rig::off (kFirstNote));
        block (cc (45, 0));
        p.drainPosted();
        check (duringMute < 1.0e-4f && p.engine.muted[0].load(), "MUTE + pad: il colpo non suona e A1 va in mute");
        const float mutedPeak = playPeak (kFirstNote);
        check (mutedPeak < 1.0e-3f, "A1 in mute resta muto (picco " + juce::String (mutedPeak, 5) + ")");
        p.padAction (0, PadAction::Mute);
        check (playPeak (kFirstNote) > 0.1f, "di nuovo MUTE: A1 suona");

        p.padAction (1, PadAction::Solo);
        const float pa1 = playPeak (kFirstNote), pa2 = playPeak (kFirstNote + 1);
        check (pa1 < 1.0e-3f && pa2 > 0.05f, "SOLO su A2: A1 tace (" + juce::String (pa1, 5) + "), A2 suona (" + juce::String (pa2, 3) + ")");
        p.padAction (1, PadAction::Solo);

        p.fixedVelocity = true;
        block (Rig::note (kFirstNote, 20));
        int vel = 0;
        for (const auto m : p.lastMidi()) if (m.getMessage().isNoteOn()) vel = m.getMessage().getVelocity();
        block (Rig::off (kFirstNote));
        check (vel == 127, "FIXED VEL: velocity 20 diventa 127");
        p.fixedVelocity = false;

        p.selectedPad = 2;
        p.setPadMode (PadMode::Velocity16);
        block (Rig::note (kFirstNote + 3, 100));
        int n16 = -1; vel = 0;
        for (const auto m : p.lastMidi()) if (m.getMessage().isNoteOn()) { n16 = m.getMessage().getNoteNumber(); vel = m.getMessage().getVelocity(); }
        block (Rig::off (kFirstNote + 3));
        check (n16 == kFirstNote + 2 && vel == 31, "16 VELOCITY: pad 4 suona A3 a velocity 31");

        // KEYBOARD in Re minore naturale: ogni nota in arrivo si aggancia alla scala
        p.setPadMode (PadMode::Keyboard);
        auto setChoice = [&] (const char* id, int i) { auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (float (i))); };
        setChoice (ParamID::keyRoot, 2);
        setChoice (ParamID::keyScale, 2);
        bool allIn = true;
        for (int n = 40; n < 90; ++n)
        {
            block (Rig::note (n));
            for (const auto m : p.lastMidi())
                if (m.getMessage().isNoteOn())
                    allIn &= scales::inScale (2, 60 + 2, m.getMessage().getNoteNumber());
            block (Rig::off (n));
        }
        check (allIn, "KEYBOARD in D minore: 50 note cromatiche escono tutte in scala");
        check (p.keyboardSemitones (0) == 2 && p.keyboardSemitones (7) == 14 && p.keyboardNoteName (2) == "F3", "pad della tastiera: D3 E3 F3 ... ottava al pad 8");
        p.setPadMode (PadMode::Normal);

        // NOTE REPEAT tenuto: a 120 BPM in 1/16, un secondo = 8 colpi
        block (cc (27, 127));
        int hits = 0;
        block (Rig::note (kFirstNote));
        for (const auto m : p.lastMidi()) hits += m.getMessage().isNoteOn() ? 1 : 0;
        for (int i = 1; i < int (kRate / kBlock); ++i)
        {
            block ({});
            for (const auto m : p.lastMidi()) hits += m.getMessage().isNoteOn() ? 1 : 0;
        }
        block (Rig::off (kFirstNote));
        block (cc (27, 0));
        check (hits >= 8 && hits <= 9, "NOTE REPEAT 1/16 a 120 BPM: " + juce::String (hits) + " colpi in 1 s");

        // ERASE + pad, poi UNDO
        block (cc (23, 127));
        block (Rig::note (kFirstNote + 4));
        block (Rig::off (kFirstNote + 4));
        block (cc (23, 0));
        p.drainPosted();
        const bool erased = p.samples.get (4, 0) == nullptr;
        p.runCommand (Cmd::Undo);
        check (erased && p.samples.get (4, 0) != nullptr, "ERASE + pad cancella A5, UNDO lo riporta");
        // DUPLICATE: A1 su A9
        p.padAction (0, PadAction::Duplicate);
        p.padAction (8, PadAction::Duplicate);
        check (p.samples.get (8, 0) == p.samples.get (0, 0), "DUPLICATE: A1 copiato su A9");
    }

    // ---- 14. CHOP: un loop con 8 colpi diventa 8 pad, poi parti uguali, UNDO in un colpo solo
    {
        RSU10AudioProcessor p;
        p.prepareToPlay (kRate, kBlock);
        const double rate = 44100.0;
        juce::AudioBuffer<float> loop (1, int (rate * 2.0));
        loop.clear();
        const double hits[] { 0.0, 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75 };
        for (double h : hits)   // colpo: rumore che decade in 80 ms
        {
            juce::Random rng (int (h * 1000) + 7);
            const int at = int (h * rate);
            for (int i = 0; i < int (0.08 * rate); ++i)
                loop.setSample (0, at + i, (rng.nextFloat() * 2.0f - 1.0f) * 0.8f * std::exp (-float (i) / float (0.02 * rate)));
        }
        Sample::Ptr src = new Sample (std::move (loop), rate, "loop");
        p.samples.set (0, 0, src);
        p.pads.resetPad (0);
        const auto before = p.samples.get (5, 0);

        const int n = p.chopPad (0, RSU10AudioProcessor::ChopMode::Transients);
        bool aligned = n == 8;
        for (int i = 0; i < n && aligned; ++i)
            aligned = std::abs (p.pads.get (i, PP::Start) * 2.0 - hits[i]) < 0.012   // entro ~12 ms dal colpo
                      && p.samples.get (i, 0) == src.get();
        check (aligned, "CHOP sui colpi: " + juce::String (n) + " fette allineate ai colpi");
        check (std::abs (p.pads.get (7, PP::End) - 1.0f) < 1.0e-4f && p.pads.get (2, PP::End) == p.pads.get (3, PP::Start),
               "le fette sono contigue e l'ultima arriva alla fine");

        // SHIFT + pad 10 dalla Mikro subito dopo: parti uguali, 16 fette
        juce::AudioBuffer<float> buf (2, kBlock);
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::controllerEvent (16, 111, 127), 0);
        buf.clear(); p.processBlock (buf, m);
        p.drainPosted();
        check (p.samples.get (15, 0) == src.get() && std::abs (p.pads.get (1, PP::Start) - 1.0f / 16.0f) < 1.0e-3f,
               "di nuovo CHOP (SHIFT + pad 10): 16 parti uguali");

        // ogni fetta suona
        float peak = 0.0f;
        juce::MidiBuffer hit;
        hit.addEvent (juce::MidiMessage::noteOn (1, kFirstNote + 4, (juce::uint8) 120), 0);
        buf.clear(); p.processBlock (buf, hit);
        for (int i = 0; i < 10; ++i) { buf.clear(); juce::MidiBuffer none; p.processBlock (buf, none); peak = juce::jmax (peak, buf.getMagnitude (0, 0, kBlock)); }
        check (peak > 0.05f, "la fetta del pad 5 suona (picco " + juce::String (peak, 2) + ")");

        p.runCommand (Cmd::Undo);   // annulla il chop in parti uguali
        p.runCommand (Cmd::Undo);   // annulla il chop sui colpi
        check (p.samples.get (5, 0) == before && p.samples.get (0, 0) == src.get() && p.pads.get (0, PP::Start) == 0.0f,
               "due UNDO: torna tutto com'era prima dei chop");
    }

    std::cout << (failures == 0 ? "TUTTO OK\n" : juce::String (failures) + " test falliti\n");
    return failures == 0 ? 0 : 1;
}
