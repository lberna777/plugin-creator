#pragma once
#include <JuceHeader.h>

// Palette e disegno vettoriale della UI: niente PNG, niente testo cotto nello sfondo.
// Lo sfondo è una piastra inerte (gradiente + grana); tutto ciò che si muove è qui.
namespace Palette
{
    const juce::Colour plate      { 0xff16181d };
    const juce::Colour plateLight { 0xff22252c };
    const juce::Colour panel      { 0xff1d2027 };
    const juce::Colour stroke     { 0xff33383f };
    const juce::Colour text       { 0xffd8dce3 };
    const juce::Colour textDim    { 0xff7d858f };
    const juce::Colour accent     { 0xffe8913a };   // ambra: il fuoco è la scena di default
    const juce::Colour accentCool { 0xff4fa3c7 };
    const juce::Colour ok         { 0xff6fc27a };
    const juce::Colour hot        { 0xffd2564b };

    juce::Colour forScene (int sceneIndex);
}

class AtmosLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AtmosLookAndFeel();

    void setAccent (juce::Colour c) { accent = c; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool, int, int, int, int,
                       juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;

private:
    juce::Colour accent = Palette::accent;
};
