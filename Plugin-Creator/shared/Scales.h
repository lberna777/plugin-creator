// Scale musicali per suonare "sempre intonati": condivise da Mikro Bridge (pad in modo Keys)
// e RSU-10 (PAD MODE KEYBOARD). Senza JUCE, solo interi: testabili e usabili nel thread audio.
#pragma once

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
} // namespace scales
