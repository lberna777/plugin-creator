#include "LookAndFeel.h"

// Proporzioni interne dei controlli (frazioni del loro raggio/altezza): geometria del disegno,
// non posizioni sul pannello — quelle stanno in layout.json.
namespace knobGeom
{
    constexpr float arcInset   = 0.08f;   // anello del valore
    constexpr float arcWidth   = 0.07f;
    constexpr float bodyRadius = 0.72f;
    constexpr float capRadius  = 0.50f;
    constexpr float pointerIn  = 0.18f;
    constexpr float pointerOut = 0.66f;
    constexpr float pointerW   = 0.075f;
    constexpr int   knurls     = 28;
}

MachineLookAndFeel::MachineLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, palette::module);
    setColour (juce::PopupMenu::textColourId, palette::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, palette::amberDim);
    setColour (juce::PopupMenu::highlightedTextColourId, palette::amber);
    setColour (juce::ComboBox::textColourId, palette::amber);
    setColour (juce::ComboBox::arrowColourId, palette::amber);
    setColour (juce::ComboBox::backgroundColourId, palette::screen);
    setColour (juce::ComboBox::outlineColourId, palette::moduleEdge);
}

juce::Font MachineLookAndFeel::machineFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions ("Helvetica Neue", height, bold ? juce::Font::bold : juce::Font::plain))
               .withExtraKerningFactor (0.06f);
}

void MachineLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float startAngle, float endAngle, juce::Slider& slider)
{
    using namespace knobGeom;
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto  centre = bounds.getCentre();
    const float angle  = startAngle + pos * (endAngle - startAngle);
    const bool  bipolar = slider.getProperties()["bipolar"];
    const bool  hot     = slider.isMouseOverOrDragging();

    // anello: traccia + valore (dal centro per i bipolari)
    const float arcR = radius * (1.0f - arcInset);
    const float strokeW = radius * arcWidth;
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (palette::moduleEdge);
    g.strokePath (track, juce::PathStrokeType (strokeW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (hot ? palette::amber : palette::amber.withMultipliedBrightness (0.85f));
    g.strokePath (value, juce::PathStrokeType (strokeW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // corpo zigrinato
    const float bodyR = radius * bodyRadius;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre.translated (0.0f, radius * 0.06f)));

    juce::ColourGradient bodyGrad (palette::knobBody.brighter (0.35f), centre.x, centre.y - bodyR,
                                   palette::knobBody, centre.x, centre.y + bodyR, false);
    g.setGradientFill (bodyGrad);
    g.fillEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre));

    g.setColour (juce::Colours::white.withAlpha (0.07f));
    for (int i = 0; i < knurls; ++i)
    {
        const float a = angle + juce::MathConstants<float>::twoPi * (float) i / (float) knurls;
        const auto p1 = centre.getPointOnCircumference (bodyR * 0.82f, a);
        const auto p2 = centre.getPointOnCircumference (bodyR * 0.98f, a);
        g.drawLine ({ p1, p2 }, radius * 0.03f);
    }

    // cappuccio metallico
    const float capR = radius * capRadius;
    juce::ColourGradient capGrad (palette::metal.brighter (0.4f), centre.x - capR, centre.y - capR,
                                  palette::metal.darker (0.9f), centre.x + capR, centre.y + capR, false);
    g.setGradientFill (capGrad);
    g.fillEllipse (juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (centre));
    g.setColour (palette::knobBody);
    g.drawEllipse (juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (centre), radius * 0.02f);

    // indicatore
    juce::Path pointer;
    const float pw = radius * pointerW;
    pointer.addRoundedRectangle (-pw * 0.5f, -radius * pointerOut, pw, radius * (pointerOut - pointerIn), pw * 0.5f);
    g.setColour (palette::ink);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void MachineLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto r   = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on  = b.getToggleState();
    const float cr = r.getHeight() * 0.2f;

    g.setColour (palette::screen);
    g.fillRoundedRectangle (r, cr);
    g.setColour (highlighted ? palette::amber.withAlpha (0.6f) : palette::moduleEdge);
    g.drawRoundedRectangle (r, cr, 1.0f);

    // LED
    const float ledD = r.getHeight() * 0.36f;
    const auto  led  = juce::Rectangle<float> (ledD, ledD).withCentre ({ r.getX() + r.getHeight() * 0.5f, r.getCentreY() });
    if (on)
    {
        g.setColour (palette::amber.withAlpha (0.25f));
        g.fillEllipse (led.expanded (ledD * 0.6f));
    }
    g.setColour (on ? palette::amber : palette::amberDim);
    g.fillEllipse (led);

    g.setColour (on ? palette::ink : palette::inkDim);
    g.setFont (machineFont (r.getHeight() * 0.42f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (r.getHeight() * 0.9f), juce::Justification::centredLeft, false);
}

void MachineLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (w, h).toFloat().reduced (0.5f);
    const float cr = (float) h * 0.2f;
    g.setColour (palette::screen);
    g.fillRoundedRectangle (r, cr);
    g.setColour (box.isMouseOver (true) ? palette::amber.withAlpha (0.6f) : palette::moduleEdge);
    g.drawRoundedRectangle (r, cr, 1.0f);

    const float arrowW = (float) h * 0.32f;
    const juce::Point<float> c { (float) w - (float) h * 0.45f, (float) h * 0.5f };
    juce::Path arrow;
    arrow.addTriangle (c.x - arrowW * 0.5f, c.y - arrowW * 0.25f, c.x + arrowW * 0.5f, c.y - arrowW * 0.25f,
                       c.x, c.y + arrowW * 0.3f);
    g.setColour (palette::amber);
    g.fillPath (arrow);
}

void MachineLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const int h = box.getHeight();
    label.setBounds (h / 4, 0, box.getWidth() - h, h);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

juce::Font MachineLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return machineFont ((float) box.getHeight() * 0.48f);
}

juce::Font MachineLookAndFeel::getPopupMenuFont()
{
    return machineFont (14.0f, false);
}
