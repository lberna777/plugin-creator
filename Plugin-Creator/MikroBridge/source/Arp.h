// Effetti della manopola e arpeggiatore di Mikro Bridge. Niente JUCE e niente allocazioni:
// girano sul thread USB e si provano da soli in MikroCoreTests.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace mikro
{
// ---------------------------------------------------------------- manopola

// Cosa fa l'encoder. FL = come prima (CC 14 relativo sul canale controlli: jog dello script FL,
// ribbon dell'RSU-10); gli altri mandano un valore assoluto allo strumento, sul canale dei pad.
struct KnobFx
{
    const char* name;      // sulla finestra
    const char* shortName; // sul display della Mikro
    int cc;                // -1 = FL (relativo), -2 = pitch bend, -3 = velocità dell'arpeggiatore
    int initial;           // valore di partenza e di reset (encoder premuto)
};

inline constexpr int kKnobFxPitchBend = -2, kKnobFxArpRate = -3, kKnobFxDaw = -1;
inline constexpr int kNumKnobFx = 13;
inline constexpr KnobFx kKnobFx[kNumKnobFx] {
    { "FL (scorri / ribbon RSU-10)", "FL",       kKnobFxDaw,       64 },
    { "Filtro (CC 74)",              "FILTRO",   74,               127 },
    { "Risonanza (CC 71)",           "RISON.",   71,               0 },
    { "Modulazione (CC 1)",          "MOD",      1,                0 },
    { "Volume (CC 7)",               "VOLUME",   7,                100 },
    { "Pan (CC 10)",                 "PAN",      10,               64 },
    { "Espressione (CC 11)",         "ESPRESS.", 11,               127 },
    { "Riverbero (CC 91)",           "RIVERB.",  91,               0 },
    { "Delay / chorus (CC 93)",      "DELAY",    93,               0 },
    { "Attacco (CC 73)",             "ATTACCO",  73,               64 },
    { "Rilascio (CC 72)",            "RILASCIO", 72,               64 },
    { "Pitch bend",                  "PITCH",    kKnobFxPitchBend, 64 },
    { "Velocita' arpeggiatore",      "ARP VEL",  kKnobFxArpRate,   3 },
};

// ---------------------------------------------------------------- arpeggiatore

enum class ArpPattern { Up = 0, Down, UpDown, Random, AsPlayed };
inline constexpr int kNumArpPatterns = 5;
inline constexpr const char* kArpPatternNames[kNumArpPatterns] { "Su", "Giu'", "Su e giu'", "Casuale", "Come suonate" };

inline constexpr int kNumArpRates = 6;
inline constexpr const char* kArpRateNames[kNumArpRates] { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
inline constexpr double kArpRateBeats[kNumArpRates] { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };

struct ArpSettings
{
    ArpPattern pattern = ArpPattern::Up;
    int rate = 3;          // indice in kArpRateNames
    int octaves = 1;       // 1..3: il giro si ripete un'ottava sopra
    float gate = 0.6f;     // durata della nota, frazione del passo
    bool latch = false;    // le note restano finché non se ne suona una nuova da zero
    double bpm = 120.0;
};

struct ArpEvent
{
    enum Type : uint8_t { NoteOn, NoteOff } type;
    uint8_t note, velocity;
};

class Arpeggiator
{
public:
    static constexpr int kMaxNotes = 32;

    // Note tenute (dai pad; in Accordi tutte le note dell'accordo)
    void press (int note, int velocity) noexcept
    {
        if (latched && heldCount == 0)   // latch: un nuovo gesto sostituisce il vecchio
            count = 0;
        latched = false;
        ++heldCount;
        for (int i = 0; i < count; ++i)
            if (notes[size_t (i)].note == note)
            {
                ++notes[size_t (i)].holders;   // la stessa nota da un altro pad (accordi vicini)
                return;
            }
        if (count < kMaxNotes)
            notes[size_t (count++)] = { note, velocity, order++, 1 };
    }

    void release (int note, bool latch) noexcept
    {
        heldCount = std::max (0, heldCount - 1);
        if (latch)
        {
            latched = heldCount == 0;
            return;
        }
        for (int i = 0; i < count; ++i)
            if (notes[size_t (i)].note == note)
            {
                if (--notes[size_t (i)].holders > 0)   // un altro pad la tiene ancora
                    break;
                for (int j = i; j < count - 1; ++j)
                    notes[size_t (j)] = notes[size_t (j + 1)];
                --count;
                break;
            }
    }

    void clear() noexcept { count = 0; heldCount = 0; latched = false; }
    bool active() const noexcept { return count > 0; }

    // Da chiamare spesso (thread USB, ~1-2 ms). Scrive al più 2 eventi in out, ritorna quanti.
    int tick (double nowMs, const ArpSettings& s, std::array<ArpEvent, 2>& out) noexcept
    {
        int n = 0;
        const double stepMs = 60000.0 / std::clamp (s.bpm, 20.0, 400.0) * kArpRateBeats[std::clamp (s.rate, 0, kNumArpRates - 1)];

        if (sounding >= 0 && nowMs >= offAt)
        {
            out[size_t (n++)] = { ArpEvent::NoteOff, uint8_t (sounding), 0 };
            sounding = -1;
        }
        if (count == 0)
        {
            running = false;
            return n;
        }
        if (! running)   // primo passo subito, appena si preme
        {
            running = true;
            nextStep = nowMs;
            index = -1;
            direction = 1;
        }
        if (nowMs < nextStep)
            return n;

        if (sounding >= 0)   // legato estremo (gate ~1): chiude la precedente
        {
            out[size_t (n++)] = { ArpEvent::NoteOff, uint8_t (sounding), 0 };
            sounding = -1;
        }
        const auto step = nextNote (s);
        if (n < 2 && step.note >= 0 && step.note <= 127)
        {
            out[size_t (n++)] = { ArpEvent::NoteOn, uint8_t (step.note), uint8_t (std::clamp (step.velocity, 1, 127)) };
            sounding = step.note;
            offAt = nowMs + stepMs * std::clamp (double (s.gate), 0.05, 0.98);
        }
        nextStep += stepMs;
        if (nextStep < nowMs)   // ritardo (pausa del thread): riparte dal presente
            nextStep = nowMs + stepMs;
        return n;
    }

    void restart() noexcept { running = false; }   // MIDI Start dal DAW: il giro riparte dal primo passo
    int soundingNote() const noexcept { return sounding; }

private:
    struct Held { int note, velocity; uint32_t order; int holders; };
    struct Step { int note, velocity; };

    Step nextNote (const ArpSettings& s) noexcept
    {
        // ordine delle note del giro: per altezza o come suonate
        std::array<Held, kMaxNotes> sorted {};
        std::copy (notes.begin(), notes.begin() + count, sorted.begin());
        if (s.pattern == ArpPattern::AsPlayed)
            std::sort (sorted.begin(), sorted.begin() + count, [] (const Held& a, const Held& b) { return a.order < b.order; });
        else
            std::sort (sorted.begin(), sorted.begin() + count, [] (const Held& a, const Held& b) { return a.note < b.note; });

        const int oct = std::clamp (s.octaves, 1, 3);
        const int total = count * oct;
        int i = 0;
        switch (s.pattern)
        {
            case ArpPattern::Up:
            case ArpPattern::AsPlayed:
                index = (index + 1) % total;
                i = index;
                break;
            case ArpPattern::Down:
                index = (index + 1) % total;
                i = total - 1 - index;
                break;
            case ArpPattern::UpDown:
                if (total == 1) { index = 0; i = 0; break; }
                index += direction;
                if (index >= total) { index = total - 2; direction = -1; }
                if (index < 0)      { index = 1; direction = 1; }
                i = index;
                break;
            case ArpPattern::Random:
                rng = rng * 1664525u + 1013904223u;
                i = int ((rng >> 8) % uint32_t (total));
                break;
        }
        const auto& h = sorted[size_t (i % count)];
        return { h.note + 12 * (i / count), h.velocity };
    }

    std::array<Held, kMaxNotes> notes {};
    int count = 0, heldCount = 0;
    bool latched = false;
    uint32_t order = 0, rng = 12345u;

    bool running = false;
    double nextStep = 0.0, offAt = 0.0;
    int index = -1, direction = 1, sounding = -1;
};
} // namespace mikro
