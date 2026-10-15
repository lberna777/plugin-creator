// Parametri di ogni pad (64 = 4 banchi x 16). Non sono parametri del DAW: sono troppi
// (64 x 29) per l'automazione, vivono nello stato del kit. Una sola tabella li descrive
// per motore, UI, salvataggio: id, nome, intervallo, default.
#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>

namespace rsu
{
inline constexpr int kPadsPerBank = 16;
inline constexpr int kBanks = 4;
inline constexpr int kNumPads = kPadsPerBank * kBanks;
inline constexpr int kMaxLayers = 4;          // campioni per pad, scelti dalla velocity
inline constexpr int kNumOutputs = 16;        // uscita principale + 15 aux, tutte stereo
inline constexpr int kFirstNote = 36;         // C1 = pad A1 (mappa di FPC / Drum Machine Designer)
inline constexpr int kNumChokeGroups = 8;

enum class PP : int
{
    // SUONO
    Volume, Pan, Tune, Fine, Attack, Decay, Sustain, Release,
    // TAGLIO
    Start, End, LoopStart, FadeIn, FadeOut, PlayMode, Reverse, Stretch,
    // COLORE
    FilterType, Cutoff, Resonance, VelToFilter, VelToVolume, Grade, Drive, Comp,
    // USCITA
    Output, Choke, Mono, Split2, Split3, Split4,
    Count
};
inline constexpr int kNumPP = int (PP::Count);

enum class PlayMode { OneShot, Gate, Loop };
enum class FilterType { Off, LowPass, HighPass, BandPass };
enum class Grade { High, Standard, Long, ExtraLong };       // nomi dell'SU10
enum class Stretch { Off, Auto, Beats1, Beats2, Beats4, Beats8, Beats16 };

struct PPDef
{
    const char* id;         // chiave nel file del kit: congelata
    const char* name;       // etichetta sullo schermo
    float min, max, def;
    float skewCentre;       // 0 = lineare; altrimenti valore al centro della corsa
    const char* unit;
    const char* const* choices = nullptr;   // parametro a scelta
    int numChoices = 0;
};

namespace choices
{
inline constexpr const char* playMode[] { "ONE-SHOT", "GATE", "LOOP" };
inline constexpr const char* onOff[]    { "OFF", "ON" };
inline constexpr const char* stretch[]  { "OFF", "AUTO", "1 BAT", "2 BAT", "4 BAT", "8 BAT", "16 BAT" };
inline constexpr const char* filter[]   { "OFF", "PASSA-BASSO", "PASSA-ALTO", "PASSA-BANDA" };
inline constexpr const char* grade[]    { "HIGH", "STANDARD", "LONG", "EXTRA LONG" };
inline constexpr const char* output[]   { "MAIN", "OUT 2", "OUT 3", "OUT 4", "OUT 5", "OUT 6", "OUT 7", "OUT 8",
                                          "OUT 9", "OUT 10", "OUT 11", "OUT 12", "OUT 13", "OUT 14", "OUT 15", "OUT 16" };
inline constexpr const char* choke[]    { "NESSUNO", "1", "2", "3", "4", "5", "6", "7", "8" };
}

inline const std::array<PPDef, kNumPP>& ppDefs()
{
    using namespace choices;
    static const std::array<PPDef, kNumPP> defs {{
        { "vol",      "VOLUME",     -60.0f, 12.0f, 0.0f,    -12.0f, "dB" },
        { "pan",      "PAN",        -1.0f,  1.0f,  0.0f,    0.0f,   "" },
        { "tune",     "INTONAZ.",   -24.0f, 24.0f, 0.0f,    0.0f,   "st" },
        { "fine",     "FINE",       -100.0f,100.0f,0.0f,    0.0f,   "ct" },
        { "att",      "ATTACCO",    0.0f,   2000.0f, 0.0f,  80.0f,  "ms" },
        { "dec",      "DECAY",      5.0f,   8000.0f, 600.0f, 400.0f, "ms" },
        { "sus",      "SUSTAIN",    0.0f,   1.0f,  1.0f,    0.0f,   "" },
        { "rel",      "RILASCIO",   1.0f,   5000.0f, 40.0f, 300.0f, "ms" },

        { "start",    "INIZIO",     0.0f,   1.0f,  0.0f,    0.0f,   "%" },
        { "end",      "FINE CAMP.", 0.0f,   1.0f,  1.0f,    0.0f,   "%" },
        { "loop",     "PUNTO LOOP", 0.0f,   1.0f,  0.0f,    0.0f,   "%" },
        { "fadein",   "FADE IN",    0.0f,   500.0f, 0.0f,   50.0f,  "ms" },
        { "fadeout",  "FADE OUT",   0.0f,   500.0f, 2.0f,   50.0f,  "ms" },
        { "mode",     "MODO",       0.0f,   2.0f,  0.0f,    0.0f,   "", playMode, 3 },
        { "rev",      "REVERSE",    0.0f,   1.0f,  0.0f,    0.0f,   "", onOff, 2 },
        { "stretch",  "STRETCH",    0.0f,   6.0f,  0.0f,    0.0f,   "", stretch, 7 },

        { "ftype",    "FILTRO",     0.0f,   3.0f,  0.0f,    0.0f,   "", filter, 4 },
        { "cutoff",   "TAGLIO",     20.0f,  20000.0f, 20000.0f, 1000.0f, "Hz" },
        { "reso",     "RISONANZA",  0.0f,   1.0f,  0.1f,    0.0f,   "" },
        { "velflt",   "VEL>FILTRO", 0.0f,   1.0f,  0.0f,    0.0f,   "%" },
        { "velvol",   "VEL>VOLUME", 0.0f,   1.0f,  0.8f,    0.0f,   "%" },
        { "grade",    "GRADE",      0.0f,   3.0f,  0.0f,    0.0f,   "", grade, 4 },
        { "drive",    "SATURAZ.",   0.0f,   1.0f,  0.0f,    0.0f,   "%" },
        { "comp",     "COMPRESS.",  0.0f,   1.0f,  0.0f,    0.0f,   "%" },

        { "out",      "USCITA",     0.0f,   15.0f, 0.0f,    0.0f,   "", output, 16 },
        { "choke",    "CHOKE",      0.0f,   8.0f,  0.0f,    0.0f,   "", choke, 9 },
        { "mono",     "MONO",       0.0f,   1.0f,  0.0f,    0.0f,   "", onOff, 2 },
        { "split2",   "STRATO 2 DA", 1.0f,  127.0f, 64.0f,  0.0f,   "vel" },
        { "split3",   "STRATO 3 DA", 1.0f,  127.0f, 96.0f,  0.0f,   "vel" },
        { "split4",   "STRATO 4 DA", 1.0f,  127.0f, 120.0f, 0.0f,   "vel" },
    }};
    return defs;
}

// Pagine delle 7 manopole (una pagina per tasto in gomma della piastra); -1 = manopola libera
inline constexpr int kKnobs = 7;
inline constexpr int kPages = 5;
inline constexpr const char* kPageNames[kPages] { "SUONO", "INVIL.", "TAGLIO", "COLORE", "USCITA" };
inline constexpr int kPageParams[kPages][kKnobs] {
    { int (PP::Volume), int (PP::Pan), int (PP::Tune), int (PP::Fine), int (PP::VelToVolume), -1, -1 },
    { int (PP::Attack), int (PP::Decay), int (PP::Sustain), int (PP::Release), int (PP::FadeIn), int (PP::FadeOut), -1 },
    { int (PP::Start), int (PP::End), int (PP::LoopStart), int (PP::PlayMode), int (PP::Reverse), int (PP::Stretch), -1 },
    { int (PP::FilterType), int (PP::Cutoff), int (PP::Resonance), int (PP::VelToFilter), int (PP::Grade), int (PP::Drive), int (PP::Comp) },
    { int (PP::Output), int (PP::Choke), int (PP::Mono), int (PP::Split2), int (PP::Split3), int (PP::Split4), -1 },
};
inline int pageParam (int page, int knob) noexcept
{
    return (page >= 0 && page < kPages && knob >= 0 && knob < kKnobs) ? kPageParams[page][knob] : -1;
}

inline const PPDef& def (PP p) { return ppDefs()[size_t (p)]; }
inline bool isChoice (PP p)   { return def (p).choices != nullptr; }
inline bool isInteger (PP p)  { return isChoice (p) || p == PP::Tune || p == PP::Fine || p == PP::Split2 || p == PP::Split3 || p == PP::Split4; }

inline juce::NormalisableRange<float> rangeOf (PP p)
{
    const auto& d = def (p);
    juce::NormalisableRange<float> r (d.min, d.max, isInteger (p) ? 1.0f : 0.0f);
    if (d.skewCentre > d.min && d.skewCentre < d.max)
        r.setSkewForCentre (d.skewCentre);
    return r;
}

juce::String formatValue (PP p, float v);

// Valori dei 64 pad, letti dal thread audio senza lock
class PadParamStore
{
public:
    PadParamStore() { resetAll(); }

    float get (int pad, PP p) const noexcept        { return values[size_t (pad)][size_t (p)].load (std::memory_order_relaxed); }
    int   getInt (int pad, PP p) const noexcept     { return juce::roundToInt (get (pad, p)); }
    void  set (int pad, PP p, float v) noexcept
    {
        values[size_t (pad)][size_t (p)].store (rangeOf (p).snapToLegalValue (juce::jlimit (def (p).min, def (p).max, v)),
                                                std::memory_order_relaxed);
        ++generation;
    }
    void resetPad (int pad) noexcept
    {
        for (int i = 0; i < kNumPP; ++i)
            values[size_t (pad)][size_t (i)].store (ppDefs()[size_t (i)].def);
        ++generation;
    }
    void resetAll() noexcept
    {
        for (int pad = 0; pad < kNumPads; ++pad)
            resetPad (pad);
    }

    std::atomic<uint32_t> generation { 0 };   // la UI ridisegna quando cambia

private:
    std::array<std::array<std::atomic<float>, kNumPP>, kNumPads> values;
};

inline juce::String padLabel (int pad)
{
    return juce::String::charToString (juce::juce_wchar ('A' + pad / kPadsPerBank)) + juce::String (pad % kPadsPerBank + 1);
}
} // namespace rsu
