// Protocollo USB-HID della Maschine Mikro MK2 (VID 0x17CC, PID 0x1200).
// Ricavato da maschine-rs (MIT) e open-maschine: nessuna dipendenza da JUCE, testabile da solo.
//
// Ingresso
//   report 0x01  pulsanti: 4 byte di bit (bit = ButtonId) + 1 byte encoder (contatore 0..15)
//   report 0x20  pad: 16 coppie (lo, hi), indice pad = hi >> 4, pressione 12 bit = (hi & 0x0F) << 8 | lo
// Uscita
//   report 0x80  LED: 78 byte (mono 0x00..0xFF, pad e GROUP RGB 0..0x7F)
//   report 0xE0  display 128x64 a pagine (byte = 8 pixel in verticale, bit0 in alto), 4 invii da 2 pagine
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mikro
{
inline constexpr uint16_t kVendorId  = 0x17CC;
inline constexpr uint16_t kProductId = 0x1200;

inline constexpr uint8_t kReportButtons = 0x01;
inline constexpr uint8_t kReportPads    = 0x20;
inline constexpr uint8_t kReportLeds    = 0x80;
inline constexpr uint8_t kReportDisplay = 0xE0;

inline constexpr int kNumPads      = 16;
inline constexpr int kNumLeds      = 78;
inline constexpr int kPadMax       = 4095;
inline constexpr int kDisplayW     = 128;
inline constexpr int kDisplayH     = 64;
inline constexpr int kDisplayBytes = kDisplayW * kDisplayH / 8;

// Indice del bit nel report 0x01
enum ButtonId : uint8_t
{
    Shift = 0x00, Erase = 0x01, Rec = 0x02, Play = 0x03, Grid = 0x04,
    StepRight = 0x05, StepLeft = 0x06, Restart = 0x07,
    EncoderPush = 0x0B, NoteRepeat = 0x0C, Sampling = 0x0D, Browse = 0x0E, Group = 0x0F,
    Main = 0x10, NavRight = 0x11, NavLeft = 0x12, Nav = 0x13, Control = 0x14,
    F3 = 0x15, F2 = 0x16, F1 = 0x17,
    Mute = 0x18, Solo = 0x19, Select = 0x1A, Duplicate = 0x1B, View = 0x1C,
    PadMode = 0x1D, Pattern = 0x1E, Scene = 0x1F,
    NumButtonBits = 0x20
};

struct ButtonInfo { ButtonId id; const char* name; int led; };   // led = -1: nessun LED

// Ordine stabile: la posizione in questa tabella decide il CC inviato (vedi Bridge), non cambiarla.
inline constexpr std::array<ButtonInfo, 29> kButtons {{
    { Play, "PLAY", 0x12 },       { Rec, "REC", 0x13 },             { Restart, "RESTART", 0x0E },
    { Erase, "ERASE", 0x14 },     { Grid, "GRID", 0x11 },           { StepLeft, "STEP <", 0x0F },
    { StepRight, "STEP >", 0x10 },{ NoteRepeat, "NOTE REPEAT", 0x0D },{ Sampling, "SAMPLING", 0x0C },
    { Browse, "BROWSE", 0x0B },   { Group, "GROUP", 0x08 },         { Main, "MAIN", 0x07 },
    { NavLeft, "NAV <", 0x05 },   { NavRight, "NAV >", 0x06 },      { Nav, "NAV", 0x04 },
    { Control, "CONTROL", 0x03 }, { F1, "F1", 0x00 },               { F2, "F2", 0x01 },
    { F3, "F3", 0x02 },           { Scene, "SCENE", 0x16 },         { Pattern, "PATTERN", 0x17 },
    { View, "VIEW", 0x19 },       { Duplicate, "DUPLICATE", 0x1A }, { Select, "SELECT", 0x1B },
    { Solo, "SOLO", 0x1C },       { Mute, "MUTE", 0x1D },           { EncoderPush, "ENCODER", -1 },
    { PadMode, "PAD MODE", 0x18 },{ Shift, "SHIFT", 0x15 },
}};

inline constexpr int kLedGroup = 0x08;   // RGB
inline constexpr int kLedShift = 0x15;

// LED RGB dei pad, indicizzati per pad FISICO (0 = pad 1 in basso a sinistra, 15 = pad 16 in alto a destra)
inline constexpr std::array<int, kNumPads> kPadLed {{
    0x42, 0x45, 0x48, 0x4B,   // pad 1..4
    0x36, 0x39, 0x3C, 0x3F,   // pad 5..8
    0x2A, 0x2D, 0x30, 0x33,   // pad 9..12
    0x1E, 0x21, 0x24, 0x27,   // pad 13..16
}};

// Il report numera i pad dall'alto (0 = pad 13). Converte nel numero fisico 0..15.
// flipped = true se una revisione dell'hardware li numerasse dal basso.
inline int physicalPad (int reportIndex, bool flipped = false) noexcept
{
    if (flipped)
        return reportIndex;
    return (3 - reportIndex / 4) * 4 + reportIndex % 4;
}

struct ButtonState
{
    uint32_t bits = 0;
    uint8_t encoder = 0;   // contatore assoluto a 4 bit
    bool pressed (ButtonId b) const noexcept { return (bits >> b) & 1u; }
};

inline bool parseButtons (const uint8_t* data, size_t len, ButtonState& out) noexcept
{
    if (len < 6 || data[0] != kReportButtons)
        return false;
    out.bits = uint32_t (data[1]) | uint32_t (data[2]) << 8 | uint32_t (data[3]) << 16 | uint32_t (data[4]) << 24;
    out.encoder = data[5] & 0x0F;
    return true;
}

// Passi dell'encoder tra due letture del contatore a 4 bit (-7..+8, con il giro 15 -> 0)
inline int encoderDelta (uint8_t previous, uint8_t current) noexcept
{
    int d = (int (current) - int (previous)) & 0x0F;
    return d > 8 ? d - 16 : d;
}

// Riempie pressure[reportIndex]; ritorna false se il report non è dei pad
inline bool parsePads (const uint8_t* data, size_t len, std::array<int, kNumPads>& pressure) noexcept
{
    if (len < 1 + 2 * kNumPads || data[0] != kReportPads)
        return false;
    for (int i = 0; i < kNumPads; ++i)
    {
        const uint8_t lo = data[1 + 2 * i], hi = data[2 + 2 * i];
        pressure[size_t (hi >> 4)] = int (hi & 0x0F) << 8 | lo;
    }
    return true;
}

using LedState = std::array<uint8_t, kNumLeds>;

inline std::vector<uint8_t> ledReport (const LedState& leds)
{
    std::vector<uint8_t> r { kReportLeds };
    r.insert (r.end(), leds.begin(), leds.end());
    return r;
}

// Display: 4 report, ognuno con 2 pagine da 8 righe (256 byte)
inline std::vector<uint8_t> displayReport (const std::array<uint8_t, kDisplayBytes>& fb, int chunk)
{
    const uint8_t page = uint8_t (chunk * 2);
    std::vector<uint8_t> r { kReportDisplay, 0x00, 0x00, page, 0x00, 0x80, 0x00, 0x02, 0x00 };
    const auto* start = fb.data() + size_t (page) * kDisplayW;
    r.insert (r.end(), start, start + 2 * kDisplayW);
    return r;
}
} // namespace mikro
