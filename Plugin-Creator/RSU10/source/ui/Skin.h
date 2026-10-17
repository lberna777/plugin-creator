// Materiali dell'RSU-10. Ogni pezzo usa l'asset PNG di Gemini se c'è (source/assets/*.png,
// stesso nome dei prompt); altrimenti lo disegna da codice, così il plugin è completo anche
// prima della grafica definitiva.
#pragma once

#include <JuceHeader.h>

namespace skin
{
namespace col
{
// Carrozzeria 1995
inline const juce::Colour body      { 0xff4b5670 };   // blu ardesia
inline const juce::Colour bodyDark  { 0xff3a4359 };
inline const juce::Colour bezel     { 0xff0d0e10 };
inline const juce::Colour silk      { 0xffe9e9e6 };   // serigrafia bianca
inline const juce::Colour silkRed   { 0xffd8473a };
inline const juce::Colour keyGrey   { 0xffaeaca7 };
inline const juce::Colour keyViolet { 0xffaa9ee6 };
inline const juce::Colour rubber    { 0xff2b2c31 };
inline const juce::Colour recRed    { 0xffe23a2e };
// Anima moderna
inline const juce::Colour alu       { 0xff5d6066 };
inline const juce::Colour aluLight  { 0xff8a8e95 };
inline const juce::Colour screen    { 0xff060708 };
inline const juce::Colour amber     { 0xffffb04a };
inline const juce::Colour cyan      { 0xff39d3e3 };
inline const juce::Colour warmWhite { 0xfff4ead8 };
inline const juce::Colour dim       { 0xff6f7680 };
}

enum class KeyStyle { Grey, Violet, Rubber, Wide, Rec, Enter, Tiny };

// Asset di Gemini, se presenti nel binario (nome file come nei prompt, es. "knob.png")
juce::Image asset (const char* fileName);

void drawBody (juce::Graphics&, juce::Rectangle<float>);
void drawBezel (juce::Graphics&, juce::Rectangle<float> outer, juce::Rectangle<float> glass);
void drawAluPlate (juce::Graphics&, juce::Rectangle<float>);
void drawKey (juce::Graphics&, juce::Rectangle<float>, KeyStyle, bool down);
void drawPad (juce::Graphics&, juce::Rectangle<float>, bool down);
void drawPadRing (juce::Graphics&, juce::Rectangle<float> pad, juce::Colour, float intensity);
void drawKnob (juce::Graphics&, juce::Rectangle<float>, float angleRadians);
void drawKnobArc (juce::Graphics&, juce::Rectangle<float> knob, float from01, float to01, bool bipolar);
// Serigrafia su una riga: il testo non viene mai schiacciato su due righe, al massimo si stringe un poco
void drawSilk (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification, float height,
               juce::Colour = col::silk, bool bold = false);
void drawLed (juce::Graphics&, juce::Point<float> centre, float radius, bool on, juce::Colour = col::amber);
// LED nel tasto: una finestrella in alto sul tappo, così l'etichetta sopra resta centrata
void drawKeyLed (juce::Graphics&, juce::Rectangle<float> key, bool on, juce::Colour = col::amber, float yFrac = 0.2f);

// Simboli serigrafati del trasporto, come sull'SU10 (|<  <<  []  >  >>)
enum class Glyph { None, Top, Bwd, Stop, Play, Fwd, Left, Right };
void drawGlyph (juce::Graphics&, Glyph, juce::Rectangle<float>, juce::Colour);

juce::Font silkFont (float height, bool bold = false);
juce::Font screenFont (float height, bool bold = false);

inline constexpr float kKnobStart = juce::MathConstants<float>::pi * 1.25f;   // -135°
inline constexpr float kKnobEnd   = juce::MathConstants<float>::pi * 2.75f;   // +135°
} // namespace skin
