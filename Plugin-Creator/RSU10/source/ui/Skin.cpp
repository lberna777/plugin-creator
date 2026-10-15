#include "Skin.h"

namespace skin
{
juce::Image asset (const char* fileName)
{
    static std::map<juce::String, juce::Image> cache;
    const juce::String key (fileName);
    if (auto it = cache.find (key); it != cache.end())
        return it->second;

    juce::Image img;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        if (key == BinaryData::originalFilenames[i])
        {
            int size = 0;
            if (auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
                img = juce::ImageCache::getFromMemory (data, size);
            break;
        }
    cache[key] = img;
    return img;
}

namespace
{
// Plastica puntinata: una tessera di rumore generata una volta e ripetuta
const juce::Image& speckleTile()
{
    static juce::Image tile = []
    {
        constexpr int n = 256;
        juce::Image img (juce::Image::ARGB, n, n, true);
        juce::Random rng (1995);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
            {
                const float r = rng.nextFloat();
                juce::Colour c = juce::Colours::transparentBlack;
                if (r > 0.985f)      c = juce::Colours::white.withAlpha (0.22f + 0.2f * rng.nextFloat());
                else if (r > 0.965f) c = juce::Colours::black.withAlpha (0.25f);
                else if (r > 0.80f)  c = juce::Colours::white.withAlpha (0.035f);
                bd.setPixelColour (x, y, c);
            }
        return img;
    }();
    return tile;
}

void drawImageIn (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> r)
{
    g.drawImage (img, r, juce::RectanglePlacement::stretchToFit);
}
} // namespace

juce::Font silkFont (float h)
{
    return juce::Font (juce::FontOptions (h, juce::Font::bold)).withExtraKerningFactor (0.04f);
}

juce::Font screenFont (float h, bool bold)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain));
}

void drawBody (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (auto img = asset ("bg_plate.png"); img.isValid())
    {
        drawImageIn (g, img, r);
        return;
    }
    const float corner = r.getHeight() * 0.03f;
    g.setGradientFill (juce::ColourGradient (col::body.brighter (0.08f), r.getX(), r.getY(), col::bodyDark, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);

    g.saveState();
    g.reduceClipRegion (r.toNearestInt());
    g.setTiledImageFill (speckleTile(), 0, 0, 1.0f);
    g.fillRoundedRectangle (r, corner);
    g.restoreState();

    // smusso: luce in alto a sinistra
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (r.reduced (1.5f), corner, 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
}

void drawBezel (juce::Graphics& g, juce::Rectangle<float> outer, juce::Rectangle<float> glass)
{
    const float c = outer.getHeight() * 0.02f + 2.0f;
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (outer.translated (0.0f, 2.0f), c);
    g.setColour (col::bezel);
    g.fillRoundedRectangle (outer, c);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (outer.reduced (0.5f), c, 1.0f);

    g.setColour (col::screen);
    g.fillRect (glass);
    g.setColour (juce::Colours::black);
    g.drawRect (glass, 1.0f);
}

void drawAluPlate (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (r.expanded (1.5f), 4.0f);
    g.setGradientFill (juce::ColourGradient (col::alu.brighter (0.15f), r.getX(), r.getY(), col::alu.darker (0.25f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 3.0f);
    // spazzolatura orizzontale
    juce::Random rng (7);
    for (float y = r.getY() + 1.0f; y < r.getBottom() - 1.0f; y += 1.5f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.015f + 0.03f * rng.nextFloat()));
        g.drawHorizontalLine (int (y), r.getX() + 2.0f, r.getRight() - 2.0f);
    }
    // smusso fresato
    g.setColour (col::aluLight.withAlpha (0.8f));
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
}

void drawKey (juce::Graphics& g, juce::Rectangle<float> r, KeyStyle style, bool down)
{
    static const char* names[] { "key_grey", "key_violet", "key_rubber", "key_wide", "key_rec", "key_enter", "key_tiny" };
    const juce::String file = juce::String (names[int (style)]) + (down ? "_down.png" : "_up.png");
    if (auto img = asset (file.toRawUTF8()); img.isValid())
    {
        drawImageIn (g, img, r);
        return;
    }

    juce::Colour base;
    switch (style)
    {
        case KeyStyle::Grey: case KeyStyle::Enter: case KeyStyle::Tiny: base = col::keyGrey; break;
        case KeyStyle::Violet:  base = col::keyViolet; break;
        case KeyStyle::Rubber: case KeyStyle::Wide: base = col::rubber; break;
        case KeyStyle::Rec:     base = col::recRed; break;
    }
    if (down)
    {
        base = base.darker (0.25f);
        r = r.translated (0.0f, 1.0f);
    }

    g.setColour (juce::Colours::black.withAlpha (down ? 0.25f : 0.45f));
    if (style == KeyStyle::Rec)
    {
        g.fillEllipse (r.translated (0.0f, down ? 1.0f : 2.5f));
        g.setGradientFill (juce::ColourGradient (base.brighter (0.4f), r.getCentreX(), r.getY(), base.darker (0.3f), r.getCentreX(), r.getBottom(), false));
        g.fillEllipse (r);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillEllipse (r.reduced (r.getWidth() * 0.28f).withY (r.getY() + r.getHeight() * 0.14f).withHeight (r.getHeight() * 0.22f));
        return;
    }
    const float c = juce::jmin (r.getWidth(), r.getHeight()) * 0.16f;
    g.fillRoundedRectangle (r.translated (0.0f, down ? 1.0f : 2.5f), c);
    g.setGradientFill (juce::ColourGradient (base.brighter (0.18f), r.getX(), r.getY(), base.darker (0.18f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, c);
    g.setColour (juce::Colours::white.withAlpha (style == KeyStyle::Rubber || style == KeyStyle::Wide ? 0.07f : 0.3f));
    g.drawRoundedRectangle (r.reduced (1.0f), c, 1.0f);
}

void drawPad (juce::Graphics& g, juce::Rectangle<float> r, bool down)
{
    if (auto img = asset (down ? "pad_down.png" : "pad_up.png"); img.isValid())
    {
        drawImageIn (g, img, r);
        return;
    }
    const float c = r.getWidth() * 0.16f;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r.translated (0.0f, down ? 1.0f : 3.0f), c);
    const auto base = down ? col::rubber.darker (0.3f) : col::rubber;
    g.setGradientFill (juce::ColourGradient (base.brighter (0.22f), r.getCentreX(), r.getY(), base.darker (0.2f), r.getCentreX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, c);
    // cupola: riflesso morbido
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (down ? 0.03f : 0.08f), r.getCentreX(), r.getY() + r.getHeight() * 0.1f,
                                             juce::Colours::transparentWhite, r.getCentreX(), r.getCentreY(), false));
    g.fillRoundedRectangle (r.reduced (r.getWidth() * 0.06f), c * 0.8f);
}

void drawPadRing (juce::Graphics& g, juce::Rectangle<float> pad, juce::Colour c, float intensity)
{
    if (intensity <= 0.01f)
        return;
    const float cr = pad.getWidth() * 0.18f;
    auto ring = pad.expanded (pad.getWidth() * 0.045f);
    for (int k = 3; k >= 1; --k)   // alone
    {
        g.setColour (c.withAlpha (0.08f * intensity * float (k)));
        g.drawRoundedRectangle (ring.expanded (float (k) * 1.6f), cr + float (k), 2.0f);
    }
    g.setColour (c.withAlpha (juce::jlimit (0.0f, 1.0f, 0.35f + 0.65f * intensity)));
    g.drawRoundedRectangle (ring, cr, juce::jmax (1.5f, pad.getWidth() * 0.025f));
}

void drawKnob (juce::Graphics& g, juce::Rectangle<float> r, float angle)
{
    const auto centre = r.getCentre();
    if (auto img = asset ("knob.png"); img.isValid())
    {
        g.drawImageTransformed (img, juce::AffineTransform::scale (r.getWidth() / img.getWidth(), r.getHeight() / img.getHeight())
                                         .translated (r.getX(), r.getY())
                                         .rotated (angle, centre.x, centre.y));
        return;
    }
    const float rad = r.getWidth() * 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (r.translated (0.0f, 2.5f));
    // zigrinatura
    g.setColour (col::alu.darker (0.5f));
    g.fillEllipse (r);
    for (int i = 0; i < 48; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * float (i) / 48.0f;
        g.setColour (juce::Colours::white.withAlpha (i % 2 == 0 ? 0.10f : 0.0f));
        g.drawLine (centre.x + std::cos (a) * rad * 0.86f, centre.y + std::sin (a) * rad * 0.86f,
                    centre.x + std::cos (a) * rad * 0.99f, centre.y + std::sin (a) * rad * 0.99f, 1.2f);
    }
    // cappello sabbiato
    auto cap = r.reduced (rad * 0.16f);
    g.setGradientFill (juce::ColourGradient (col::aluLight, cap.getX(), cap.getY(), col::alu.darker (0.35f), cap.getRight(), cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawEllipse (cap.reduced (0.5f), 1.0f);
    // linea incisa
    const float x1 = centre.x + std::sin (angle) * rad * 0.22f, y1 = centre.y - std::cos (angle) * rad * 0.22f;
    const float x2 = centre.x + std::sin (angle) * rad * 0.78f, y2 = centre.y - std::cos (angle) * rad * 0.78f;
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawLine (x1, y1, x2, y2, juce::jmax (2.0f, rad * 0.09f));
    g.setColour (col::warmWhite.withAlpha (0.8f));
    g.drawLine (x1, y1, x2, y2, juce::jmax (1.0f, rad * 0.04f));
}

void drawKnobArc (juce::Graphics& g, juce::Rectangle<float> knob, float from01, float to01, bool bipolar)
{
    const auto c = knob.getCentre();
    const float rad = knob.getWidth() * 0.5f * 1.22f;
    juce::Path track;
    track.addCentredArc (c.x, c.y, rad, rad, 0.0f, kKnobStart, kKnobEnd, true);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float a0 = kKnobStart + (kKnobEnd - kKnobStart) * (bipolar ? 0.5f : from01);
    const float a1 = kKnobStart + (kKnobEnd - kKnobStart) * to01;
    if (std::abs (a1 - a0) < 0.01f)
        return;
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, rad, rad, 0.0f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
    g.setColour (col::amber.withAlpha (0.25f));
    g.strokePath (arc, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::amber);
    g.strokePath (arc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void drawSilk (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j, float h, juce::Colour c)
{
    g.setFont (silkFont (h));
    g.setColour (c);
    g.drawFittedText (text, r.toNearestInt(), j, 2, 0.8f);
}

void drawLed (juce::Graphics& g, juce::Point<float> p, float radius, bool on, juce::Colour c)
{
    if (on)
    {
        g.setColour (c.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (radius * 4.0f, radius * 4.0f).withCentre (p));
    }
    g.setColour (on ? c : juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p));
}
} // namespace skin
