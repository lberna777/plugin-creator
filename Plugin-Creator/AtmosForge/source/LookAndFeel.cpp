#include "LookAndFeel.h"

using namespace juce;

namespace Palette
{
    Colour forScene (int sceneIndex)
    {
        switch (sceneIndex)
        {
            case 0:  return Colour (0xff7fa7d4);   // strada: azzurro urbano
            case 1:  return Colour (0xffe8913a);   // camino: ambra
            case 2:  return Colour (0xff86b8c9);   // pioggia/neve: grigio-azzurro
            case 3:  return Colour (0xffc2a173);   // passi: terra
            default: return accent;
        }
    }
}

AtmosLookAndFeel::AtmosLookAndFeel()
{
    setColour (Slider::textBoxTextColourId, Palette::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (ComboBox::backgroundColourId, Palette::panel);
    setColour (ComboBox::textColourId, Palette::text);
    setColour (ComboBox::outlineColourId, Palette::stroke);
    setColour (ComboBox::arrowColourId, Palette::textDim);
    setColour (PopupMenu::backgroundColourId, Palette::panel);
    setColour (PopupMenu::textColourId, Palette::text);
    setColour (PopupMenu::highlightedBackgroundColourId, Palette::stroke);
    setColour (PopupMenu::highlightedTextColourId, Colours::white);
    setColour (Label::textColourId, Palette::text);
    setColour (TextButton::buttonColourId, Palette::panel);
    setColour (TextButton::textColourOffId, Palette::text);
    setColour (TextButton::textColourOnId, Colours::white);
}

void AtmosLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h,
                                         float sliderPos, float startAngle, float endAngle,
                                         Slider& s)
{
    auto bounds = Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float ring = radius * 0.16f;

    // corpo del knob
    ColourGradient body (Palette::plateLight, centre.x, centre.y - radius,
                         Palette::plate, centre.x, centre.y + radius, false);
    g.setGradientFill (body);
    g.fillEllipse (Rectangle<float> (radius * 1.5f, radius * 1.5f).withCentre (centre));
    g.setColour (Palette::stroke);
    g.drawEllipse (Rectangle<float> (radius * 1.5f, radius * 1.5f).withCentre (centre), 1.0f);

    // traccia + arco del valore
    Path track;
    track.addCentredArc (centre.x, centre.y, radius - ring * 0.5f, radius - ring * 0.5f,
                         0.0f, startAngle, endAngle, true);
    g.setColour (Palette::stroke);
    g.strokePath (track, PathStrokeType (ring * 0.7f, PathStrokeType::curved, PathStrokeType::rounded));

    Path value;
    value.addCentredArc (centre.x, centre.y, radius - ring * 0.5f, radius - ring * 0.5f,
                         0.0f, startAngle, angle, true);
    g.setColour (s.isEnabled() ? accent : Palette::textDim);
    g.strokePath (value, PathStrokeType (ring * 0.7f, PathStrokeType::curved, PathStrokeType::rounded));

    // indicatore
    Path pointer;
    pointer.addRoundedRectangle (-radius * 0.035f, -radius * 0.72f, radius * 0.07f, radius * 0.38f, radius * 0.035f);
    g.setColour (Palette::text);
    g.fillPath (pointer, AffineTransform::rotation (angle).translated (centre));
}

void AtmosLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&,
                                             bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const float corner = jmin (6.0f, r.getHeight() * 0.28f);

    Colour fill = on ? accent.withAlpha (0.22f) : Palette::panel;
    if (down)             fill = fill.brighter (0.16f);
    else if (highlighted) fill = fill.brighter (0.07f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, corner);
    g.setColour (on ? accent : Palette::stroke);
    g.drawRoundedRectangle (r, corner, on ? 1.4f : 1.0f);
}

void AtmosLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    g.setFont (Font (FontOptions (jmin (14.0f, b.getHeight() * 0.42f), Font::bold)));
    g.setColour (b.getToggleState() ? accent.brighter (0.4f)
                                    : (b.isEnabled() ? Palette::text : Palette::textDim));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 0),
                      Justification::centred, 1);
}

void AtmosLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox&)
{
    auto r = Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (Palette::stroke);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    Path arrow;
    const float cx = (float) w - 14.0f, cy = (float) h * 0.5f;
    arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.5f);
    g.setColour (Palette::textDim);
    g.fillPath (arrow);
}

void AtmosLookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (8, 0, box.getWidth() - 26, box.getHeight());
    label.setFont (Font (FontOptions (jmin (13.0f, box.getHeight() * 0.5f))));
    label.setJustificationType (Justification::centredLeft);
}

void AtmosLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float h = jmin (18.0f, r.getHeight());
    auto sw = Rectangle<float> (0.0f, 0.0f, h * 1.9f, h).withY (r.getCentreY() - h * 0.5f);
    const bool on = b.getToggleState();

    g.setColour (on ? accent.withAlpha (0.35f) : Palette::panel);
    g.fillRoundedRectangle (sw, h * 0.5f);
    g.setColour (on ? accent : Palette::stroke);
    g.drawRoundedRectangle (sw, h * 0.5f, 1.0f);

    const float knobD = h - 5.0f;
    const float kx = on ? sw.getRight() - knobD - 2.5f : sw.getX() + 2.5f;
    g.setColour (on ? accent : Palette::textDim);
    g.fillEllipse (kx, sw.getY() + 2.5f, knobD, knobD);

    g.setColour (highlighted ? Palette::text : Palette::textDim);
    g.setFont (Font (FontOptions (12.0f, Font::bold)));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (sw.getWidth() + 8.0f),
                Justification::centredLeft);
}
