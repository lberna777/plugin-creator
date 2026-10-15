// Test del cuore senza hardware: report sintetici come quelli della Mikro MK2.
#include "Display.h"
#include "MikroProtocol.h"
#include "Scales.h"
#include "PadEngine.h"

#include <cstdio>
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
}

int main()
{
    testScales();
    testParsers();
    testPadEngine();
    testDisplay();
    if (failures == 0)
        std::printf ("MikroCoreTests: tutti i test passati\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
