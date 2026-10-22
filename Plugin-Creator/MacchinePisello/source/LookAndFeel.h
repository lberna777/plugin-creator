#pragma once
#include <JuceHeader.h>

namespace palette
{
    const juce::Colour panel      { 0xff1b1c1e };
    const juce::Colour panelHi    { 0xff26282b };
    const juce::Colour module     { 0xff222326 };
    const juce::Colour moduleEdge { 0xff34363a };
    const juce::Colour titleBar   { 0xff2c2d31 };
    const juce::Colour ink        { 0xffd9d4c7 };   // serigrafia crema
    const juce::Colour inkDim     { 0xff8b877d };
    const juce::Colour amber      { 0xffff9a2e };
    const juce::Colour amberDim   { 0xff5a3714 };
    const juce::Colour screen     { 0xff0c120e };
    const juce::Colour trace      { 0xffffb45a };
    const juce::Colour knobBody   { 0xff141414 };
    const juce::Colour metal      { 0xff9fa3a7 };
}

class MachineLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MachineLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh,
                       juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;

    static juce::Font machineFont (float height, bool bold = true);
};
