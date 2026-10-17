// Test del cuore senza hardware: report sintetici come quelli della Mikro MK2.
#include "Display.h"
#include "MikroProtocol.h"
#include "Scales.h"
#include "PadEngine.h"
#include "Arp.h"

#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstdlib>

using namespace mikro;

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { std::printf ("FALLITO %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static std::vector<uint8_t> padReport (const std::array<int, kNumPads>& p)
{
    std::vector<uint8_t> r (65, 0);
    r[0] = kReportPads;
    for (int i = 0; i < kNumPads; ++i)
    {
        // ordine nel report diverso dall'indice, come nell'hardware: conta il nibble alto
        const int slot = (i * 5) % kNumPads;
        r[size_t (1 + 2 * slot)] = uint8_t (p[size_t (i)] & 0xFF);
        r[size_t (2 + 2 * slot)] = uint8_t (i << 4 | (p[size_t (i)] >> 8));
    }
    return r;
}

static void testParsers()
{
    std::array<int, kNumPads> in {}, out {};
    for (int i = 0; i < kNumPads; ++i)
        in[size_t (i)] = i * 250 + 7;
    auto r = padReport (in);
    CHECK (parsePads (r.data(), r.size(), out));
    CHECK (out == in);
    r[0] = 0x01;
    CHECK (! parsePads (r.data(), r.size(), out));

    const uint8_t b[] { kReportButtons, 0x09, 0x00, 0x00, 0x80, 0x0E };   // SHIFT, PLAY, SCENE, encoder 14
    ButtonState s;
    CHECK (parseButtons (b, sizeof (b), s));
    CHECK (s.pressed (Shift) && s.pressed (Play) && s.pressed (Scene) && ! s.pressed (Rec));
    CHECK (s.encoder == 14);

    CHECK (encoderDelta (14, 15) == 1);
    CHECK (encoderDelta (15, 0) == 1);
    CHECK (encoderDelta (0, 15) == -1);
    CHECK (encoderDelta (3, 1) == -2);

    // report 0 = pad 13 (in alto a sinistra), report 15 = pad 4 (in basso a destra)
    CHECK (physicalPad (0) == 12);
    CHECK (physicalPad (3) == 15);
    CHECK (physicalPad (12) == 0);
    CHECK (physicalPad (15) == 3);
    CHECK (physicalPad (5, true) == 5);

    // Ogni pad fisico ha un LED RGB distinto, 3 byte dentro il report
    for (int p = 0; p < kNumPads; ++p)
        CHECK (kPadLed[size_t (p)] + 2 < kNumLeds);

    LedState leds {};
    CHECK (ledReport (leds).size() == 1 + kNumLeds);

    std::array<uint8_t, kDisplayBytes> fb {};
    fb[size_t (6 * kDisplayW + 3)] = 0xAB;
    const auto d = displayReport (fb, 3);
    CHECK (d.size() == 9 + 256);
    CHECK (d[0] == kReportDisplay && d[3] == 6 && d[5] == 0x80 && d[7] == 2);
    CHECK (d[9 + 3] == 0xAB);
}

static void testPadEngine()
{
    PadSettings s;
    s.onThreshold = 250;
    s.fullScale = 3200;
    s.attackReports = 3;

    // Colpo forte: picco al terzo report -> velocity dal picco
    {
        PadEngine e;
        CHECK (e.process (100, s).count == 0);
        CHECK (e.process (900, s).count == 0);
        CHECK (e.process (2500, s).count == 0);
        auto ev = e.process (3300, s);
        CHECK (ev.count == 1 && ev.e[0].type == PadEvent::NoteOn && ev.e[0].value == 127);
        CHECK (e.process (2000, s).count == 0);          // tenuto, niente aftertouch
        CHECK (e.process (90, s).count == 0);            // un solo report basso non basta (rimbalzo)
        CHECK (e.process (90, s).count == 0);
        ev = e.process (90, s);
        CHECK (ev.count == 1 && ev.e[0].type == PadEvent::NoteOff);
        CHECK (! e.isHeld());
    }

    // Il picco arriva prima della finestra: si suona appena la pressione scende
    {
        PadEngine e;
        e.process (1000, s);
        auto ev = e.process (800, s);
        CHECK (ev.count == 1 && ev.e[0].type == PadEvent::NoteOn);
        CHECK (ev.e[0].value == PadEngine::velocityFor (1000, s));
    }

    // Colpo brevissimo: NoteOn e NoteOff nello stesso report
    {
        PadEngine e;
        e.process (600, s);
        auto ev = e.process (0, s);
        CHECK (ev.count == 2 && ev.e[0].type == PadEvent::NoteOn && ev.e[1].type == PadEvent::NoteOff);
    }

    // Isteresi: sotto soglia ma sopra metà soglia il pad resta tenuto
    {
        PadEngine e;
        e.process (2000, s); e.process (1500, s);
        CHECK (e.isHeld());
        CHECK (e.process (200, s).count == 0);
        CHECK (e.isHeld());
    }

    // Doppio colpo: la gomma rimbalza staccando il dito -> una sola nota
    {
        PadEngine e;
        int ons = 0;
        const int bounce[] { 1500, 2500, 2400, 80, 400, 60, 350, 40, 0, 0, 0, 0 };
        double t = 0.0;
        for (int p : bounce)
        {
            const auto ev = e.process (p, s, t);
            for (int k = 0; k < ev.count; ++k) ons += ev.e[k].type == PadEvent::NoteOn ? 1 : 0;
            t += 1.0;   // un report al millisecondo
        }
        CHECK (ons == 1);
        // dopo il rilascio, entro 30 ms non riparte; dopo sì
        CHECK (e.process (900, s, t + 5.0).count == 0);
        e.process (0, s, t + 6.0);
        CHECK (e.process (900, s, t + 60.0).count == 0 && e.process (700, s, t + 61.0).count == 1);
    }

    // Colpo fantasma: pad vicino con poca pressione mentre un altro è premuto forte -> ignorato
    {
        PadEngine ghost, chord;
        int ghostOns = 0, chordOns = 0;
        for (int r = 0; r < 6; ++r)
        {
            const auto g = ghost.process (400, s, r, 3000);   // 13% del pad forte
            const auto c = chord.process (1600, s, r, 3000);  // 53%: accordo vero
            for (int k = 0; k < g.count; ++k) ghostOns += g.e[k].type == PadEvent::NoteOn ? 1 : 0;
            for (int k = 0; k < c.count; ++k) chordOns += c.e[k].type == PadEvent::NoteOn ? 1 : 0;
        }
        CHECK (ghostOns == 0 && chordOns == 1);
    }

    // Velocity: monotona, 1..127, curve ordinate
    {
        int prev = 0;
        for (int p = 250; p <= 4095; p += 50)
        {
            const int v = PadEngine::velocityFor (p, s);
            CHECK (v >= 1 && v <= 127 && v >= prev);
            prev = v;
        }
        PadSettings soft = s, hard = s, fixed = s;
        soft.curve = VelocityCurve::Soft;
        hard.curve = VelocityCurve::Hard;
        fixed.curve = VelocityCurve::Fixed;
        CHECK (PadEngine::velocityFor (1200, soft) > PadEngine::velocityFor (1200, s));
        CHECK (PadEngine::velocityFor (1200, hard) < PadEngine::velocityFor (1200, s));
        CHECK (PadEngine::velocityFor (300, fixed) == 127);
    }

    // Aftertouch: solo se attivo e solo quando cambia
    {
        PadSettings a = s;
        a.aftertouch = true;
        PadEngine e;
        e.process (1500, a); e.process (1000, a);
        auto ev = e.process (2000, a);
        CHECK (ev.count == 1 && ev.e[0].type == PadEvent::Pressure);
        CHECK (e.process (2005, a).count == 0);
    }
}

static void testDisplay()
{
    Display d;
    d.pixel (5, 10, true);
    CHECK (d.data()[size_t (1 * kDisplayW + 5)] == (1 << 2));
    d.pixel (500, 10, true);   // fuori: ignorato
    d.clear();
    const int end = d.text (0, 0, "C#1", 2);
    CHECK (end == Display::textWidth ("C#1", 2) + 2);
    bool any = false;
    for (auto b : d.data())
        any |= b != 0;
    CHECK (any);
}

// Scale: ogni grado è nella scala, i gradi salgono, lo snap trova la nota intonata più vicina
static void testScales()
{
    using namespace scales;
    for (int sc = 0; sc < kNumScales; ++sc)
        for (int r = 0; r < 12; ++r)
        {
            for (int d = -16; d < 32; ++d)
            {
                CHECK (inScale (sc, r, r + degreeToSemitones (sc, d)));
                CHECK (degreeToSemitones (sc, d + 1) > degreeToSemitones (sc, d));
            }
            for (int n = 0; n < 128; ++n)
            {
                const int m = snap (sc, r, n);
                CHECK (inScale (sc, r, m) && m - n <= 2 && n - m <= 2);
            }
        }
    CHECK (degreeToSemitones (1, 7) == 12);             // maggiore: pad 8 = ottava
    CHECK (degreeToSemitones (11, 5) == 12);            // pentatonica minore: pad 6 = ottava
    CHECK (snap (1, 0, 61) == 60 && snap (1, 0, 66) == 65);

    // Armonia: nessuna coppia di note che stride, maggiore -> pentatonica maggiore, minore -> minore
    for (int sc = 0; sc < kNumScales; ++sc)
    {
        const auto sub = consonantSubset (sc);
        CHECK (sub.count >= 3 && sub.steps[0] == 0);
        for (int i = 0; i < sub.count; ++i)
            for (int j = 0; j < sub.count; ++j)
                CHECK (i == j || ! clash (sub.steps[i], sub.steps[j]));
    }
    {
        const auto maj = consonantSubset (1), min = consonantSubset (2);
        const int majWant[] { 0, 2, 4, 7, 9 }, minWant[] { 0, 3, 5, 7, 10 };
        CHECK (maj.count == 5 && min.count == 5);
        for (int i = 0; i < 5; ++i)
            CHECK (maj.steps[i] == majWant[i] && min.steps[i] == minWant[i]);
    }

    // Accordi per grado: in Do maggiore 6-2-1 = Am Dm C, le settime sui pad 9-16
    CHECK (chordName (1, 0, 5) == "Am" && chordName (1, 0, 1) == "Dm" && chordName (1, 0, 0) == "C");
    CHECK (chordName (1, 0, 6) == "Bdim" && chordName (1, 0, 12) == "G7" && chordName (1, 0, 14) == "Bm7b5");
    CHECK (chordName (2, 9, 4) == "Em" && chordName (3, 9, 4) == "E");   // La minore: v naturale, V armonica
    for (int sc = 1; sc < kNumScales; ++sc)
        for (int p = 0; p < 16; ++p)
        {
            const auto c = chordForPad (sc, p);
            for (int k = 0; k < c.count; ++k)
                CHECK (inScale (sc, 0, c.notes[k]) && (k == 0 || c.notes[k] > c.notes[k - 1]));
        }
}

// Arpeggiatore: ordine delle note, ottave, tempo, rilascio, latch
static std::vector<int> runArp (Arpeggiator& a, const ArpSettings& s, double fromMs, double toMs)
{
    std::vector<int> ons;
    std::array<ArpEvent, 2> ev;
    for (double t = fromMs; t < toMs; t += 1.0)
    {
        const int n = a.tick (t, s, ev);
        for (int i = 0; i < n; ++i)
            if (ev[size_t (i)].type == ArpEvent::NoteOn)
                ons.push_back (ev[size_t (i)].note);
    }
    return ons;
}

static void testArp()
{
    ArpSettings s;
    s.bpm = 120.0;   // 1/16 = 125 ms
    s.rate = 3;
    {
        Arpeggiator a;
        a.press (64, 100); a.press (60, 100); a.press (67, 100);
        const auto ons = runArp (a, s, 0.0, 1000.0);
        const std::vector<int> want { 60, 64, 67, 60, 64, 67, 60, 64 };   // Su, 8 passi in 1 s
        CHECK (ons == want);
    }
    {
        Arpeggiator a;
        s.pattern = ArpPattern::Down; s.octaves = 2;
        a.press (60, 100); a.press (64, 100);
        const auto ons = runArp (a, s, 0.0, 500.0);
        const std::vector<int> want { 76, 72, 64, 60 };                    // Giu' su 2 ottave
        CHECK (ons == want);
    }
    {
        Arpeggiator a;
        s.pattern = ArpPattern::UpDown; s.octaves = 1;
        a.press (60, 100); a.press (64, 100); a.press (67, 100);
        const auto ons = runArp (a, s, 0.0, 750.0);
        const std::vector<int> want { 60, 64, 67, 64, 60, 64 };            // Su e giu', senza ripetere gli estremi
        CHECK (ons == want);
    }
    {
        Arpeggiator a;
        s.pattern = ArpPattern::AsPlayed;
        a.press (67, 100); a.press (60, 100);
        CHECK ((runArp (a, s, 0.0, 150.0) == std::vector<int> { 67, 60 }));   // a 150 ms suona ancora il 60
        a.release (67, false); a.release (60, false);
        std::array<ArpEvent, 2> ev;
        int offs = 0;
        for (double t = 150.0; t < 400.0; t += 1.0)
            for (int i = 0, n = a.tick (t, s, ev); i < n; ++i)
                offs += ev[size_t (i)].type == ArpEvent::NoteOff ? 1 : 0;
        CHECK (offs == 1 && ! a.active() && a.soundingNote() < 0);    // staccate le dita: l'ultima nota si chiude
    }
    {
        Arpeggiator a;   // latch: continua senza dita, un nuovo gesto sostituisce le note
        s.pattern = ArpPattern::Up;
        a.press (60, 100); a.release (60, true);
        CHECK (runArp (a, s, 0.0, 500.0).size() == 4);
        a.press (72, 100);
        const auto ons = runArp (a, s, 500.0, 750.0);
        CHECK (! ons.empty() && ons.back() == 72 && std::find (ons.begin(), ons.end(), 60) == ons.end());
    }
    {
        Arpeggiator a;   // due accordi con una nota in comune: lasciarne uno non toglie la nota all'altro
        s.pattern = ArpPattern::Up; s.latch = false;
        a.press (60, 100); a.press (64, 100);   // pad 1
        a.press (64, 100); a.press (67, 100);   // pad 2
        a.release (60, false); a.release (64, false);   // lascio il pad 1
        const auto ons = runArp (a, s, 0.0, 500.0);
        CHECK ((ons == std::vector<int> { 64, 67, 64, 67 }));
    }
    CHECK (kKnobFx[0].cc == kKnobFxDaw && kKnobFx[1].cc == 74);
}

int main()
{
    testArp();
    testScales();
    testParsers();
    testPadEngine();
    testDisplay();
    if (failures == 0)
        std::printf ("MikroCoreTests: tutti i test passati\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
