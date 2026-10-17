// Scale musicali per suonare "sempre intonati": condivise da Mikro Bridge (pad in modo Keys)
// e RSU-10 (PAD MODE KEYBOARD). Senza JUCE, solo interi: testabili e usabili nel thread audio.
#pragma once

#include <string>

namespace scales
{
struct Scale
{
    const char* name;
    const char* shortName;   // per il display 128x64 della Mikro
    int count;
    int steps[12];           // semitoni dalla tonica, crescenti
};

inline constexpr int kNumScales = 15;
inline constexpr Scale kScales[kNumScales] {
    { "Cromatica",          "CROM",   12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
    { "Maggiore",           "MAGG",    7, { 0, 2, 4, 5, 7, 9, 11 } },
    { "Minore naturale",    "MIN",     7, { 0, 2, 3, 5, 7, 8, 10 } },
    { "Minore armonica",    "MIN ARM", 7, { 0, 2, 3, 5, 7, 8, 11 } },
    { "Minore melodica",    "MIN MEL", 7, { 0, 2, 3, 5, 7, 9, 11 } },
    { "Dorica",             "DOR",     7, { 0, 2, 3, 5, 7, 9, 10 } },
    { "Frigia",             "FRIG",    7, { 0, 1, 3, 5, 7, 8, 10 } },
    { "Lidia",              "LID",     7, { 0, 2, 4, 6, 7, 9, 11 } },
    { "Misolidia",          "MISOL",   7, { 0, 2, 4, 5, 7, 9, 10 } },
    { "Locria",             "LOCR",    7, { 0, 1, 3, 5, 6, 8, 10 } },
    { "Pentatonica magg.",  "PENTA",   5, { 0, 2, 4, 7, 9 } },
    { "Pentatonica min.",   "PENTA M", 5, { 0, 3, 5, 7, 10 } },
    { "Blues",              "BLUES",   6, { 0, 3, 5, 6, 7, 10 } },
    { "Frigia dominante",   "FRIG D",  7, { 0, 1, 4, 5, 7, 8, 10 } },
    { "Giapponese (In)",    "IN",      5, { 0, 1, 5, 7, 8 } },
};

inline constexpr const char* kRootNames[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

inline const Scale& get (int scale) noexcept
{
    return kScales[(scale >= 0 && scale < kNumScales) ? scale : 0];
}

inline int floorDiv (int a, int b) noexcept { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
inline int mod (int a, int b) noexcept      { return a - floorDiv (a, b) * b; }

// Grado della scala (0 = tonica, anche negativo) -> semitoni dalla tonica
inline int degreeToSemitones (int scale, int degree) noexcept
{
    const auto& s = get (scale);
    return 12 * floorDiv (degree, s.count) + s.steps[mod (degree, s.count)];
}

inline bool inScale (int scale, int root, int note) noexcept
{
    const auto& s = get (scale);
    const int pc = mod (note - root, 12);
    for (int i = 0; i < s.count; ++i)
        if (s.steps[i] == pc)
            return true;
    return false;
}

// Nota della scala più vicina (a pari distanza, quella sotto): ogni tasto suona intonato
inline int snap (int scale, int root, int note) noexcept
{
    for (int d = 0; d < 12; ++d)
    {
        if (inScale (scale, root, note - d)) return note - d;
        if (inScale (scale, root, note + d)) return note + d;
    }
    return note;
}
// ---------------------------------------------------------------- armonia: ogni combinazione consonante
// Dalla scala si tolgono le note che si scontrano: semitono (o settima maggiore, la stessa coppia
// di classi) e tritono. Si toglie per prima la nota con più scontri, mai la tonica; a pari scontri
// la meno importante per il colore della scala (terza e quinta restano). Dal maggiore resta la
// pentatonica maggiore, dal minore la minore: qualunque gruppo di pad premuto insieme suona bene.
struct Subset
{
    int count = 0;
    int steps[12] {};
};

inline bool clash (int a, int b) noexcept
{
    const int d = mod (a - b, 12);
    return d == 1 || d == 11 || d == 6;
}

inline Subset consonantSubset (int scale) noexcept
{
    const auto& s = get (scale);
    bool keep[12] {};
    for (int i = 0; i < s.count; ++i)
        keep[i] = true;
    for (;;)
    {
        int worst = -1, worstClashes = 0;
        for (int i = 1; i < s.count; ++i)   // la tonica (i = 0) resta sempre
        {
            if (! keep[i])
                continue;
            int c = 0;
            for (int j = 0; j < s.count; ++j)
                if (keep[j] && j != i && clash (s.steps[i], s.steps[j]))
                    ++c;
            // importanza per posizione nella scala: 5a e 3a contano di più, poi 6a, 4a, 2a, 7a
            static constexpr int importance[12] { 100, 2, 5, 3, 6, 4, 1, 1, 1, 1, 1, 1 };
            if (c > 0 && (c > worstClashes || (c == worstClashes && importance[i] < importance[worst])))
            {
                worst = i;
                worstClashes = c;
            }
        }
        if (worst < 0)
            break;
        keep[worst] = false;
    }
    Subset out;
    for (int i = 0; i < s.count; ++i)
        if (keep[i])
            out.steps[out.count++] = s.steps[i];
    return out;
}

inline bool inConsonant (int scale, int root, int note) noexcept
{
    const auto sub = consonantSubset (scale);
    const int pc = mod (note - root, 12);
    for (int i = 0; i < sub.count; ++i)
        if (sub.steps[i] == pc)
            return true;
    return false;
}

// Grado della scala "in armonia" -> semitoni dalla tonica
inline int consonantDegreeToSemitones (int scale, int degree) noexcept
{
    const auto sub = consonantSubset (scale);
    return 12 * floorDiv (degree, sub.count) + sub.steps[mod (degree, sub.count)];
}

// ---------------------------------------------------------------- accordi per grado
// Pad 1-7 = triadi sui gradi I..VII, pad 8 = I all'ottava; pad 9-16 = gli stessi con la settima.
// Così una progressione "6-2-1" si suona coi pad 6, 2, 1 (o 14, 10, 9 con le settime).
inline constexpr int kChordPadsPerRow = 8;

struct Chord
{
    int count = 0;
    int notes[4] {};   // semitoni dalla tonica della scala, crescenti (posizione stretta)
    int degree = 0;    // 0 = I
};

inline Chord chordForPad (int scale, int pad) noexcept
{
    Chord c;
    const int step = pad % kChordPadsPerRow;   // 0..7 (7 = I all'ottava)
    c.degree = step;
    c.count = pad >= kChordPadsPerRow ? 4 : 3;
    for (int k = 0; k < c.count; ++k)
        c.notes[k] = degreeToSemitones (scale, step + 2 * k);   // terze sovrapposte dentro la scala
    return c;
}

// Sigla dagli intervalli: "", "m", "dim", "aug", "sus2", "sus4", "maj7", "7", "m7", "m7b5", "dim7"...
inline std::string chordSuffix (const Chord& c)
{
    const int third = c.notes[1] - c.notes[0], fifth = c.notes[2] - c.notes[0];
    std::string triad;
    if      (third == 4 && fifth == 7) triad = "";
    else if (third == 3 && fifth == 7) triad = "m";
    else if (third == 3 && fifth == 6) triad = "dim";
    else if (third == 4 && fifth == 8) triad = "aug";
    else if (third == 2 && fifth == 7) triad = "sus2";
    else if (third == 5 && fifth == 7) triad = "sus4";
    else                               triad = "(" + std::to_string (third) + "," + std::to_string (fifth) + ")";
    if (c.count < 4)
        return triad;
    const int seventh = c.notes[3] - c.notes[0];
    if (triad.empty() && seventh == 11) return "maj7";
    if (triad.empty() && seventh == 10) return "7";
    if (triad == "m" && seventh == 10)  return "m7";
    if (triad == "m" && seventh == 11)  return "mMaj7";
    if (triad == "dim" && seventh == 10) return "m7b5";
    if (triad == "dim" && seventh == 9)  return "dim7";
    if (triad == "aug" && seventh == 11) return "maj7#5";
    return triad + "add" + std::to_string (seventh);
}

inline std::string chordName (int scale, int root, int pad)
{
    const auto c = chordForPad (scale, pad);
    return std::string (kRootNames[mod (root + c.notes[0], 12)]) + chordSuffix (c);
}

// Numero romano del grado: maiuscolo se maggiore, minuscolo se minore, ° diminuito, ø semidiminuito
inline std::string chordRoman (int scale, int pad)
{
    static const char* up[] { "I", "II", "III", "IV", "V", "VI", "VII" };
    static const char* lo[] { "i", "ii", "iii", "iv", "v", "vi", "vii" };
    const auto c = chordForPad (scale, pad);
    const int deg = mod (c.degree, get (scale).count);
    const int idx = deg < 7 ? deg : 6;
    const int third = c.notes[1] - c.notes[0];
    const auto suffix = chordSuffix (c);
    std::string r = (third == 3 ? lo : up)[idx];   // UTF-8: ° e ø
    if (suffix == "dim")       return r + "\xc2\xb0";
    if (suffix == "dim7")      return r + "\xc2\xb0" "7";
    if (suffix == "m7b5")      return r + "\xc3\xb8" "7";
    if (suffix.rfind ("aug", 0) == 0 || suffix == "maj7#5") r += "+";
    if (c.count == 4)
        r += (suffix == "maj7" || suffix == "maj7#5" || suffix == "mMaj7") ? "M7" : "7";
    return r;
}
} // namespace scales
