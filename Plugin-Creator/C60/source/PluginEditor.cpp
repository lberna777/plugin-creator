#include "PluginEditor.h"
#include "Params.h"

namespace
{
using namespace juce;

Image asset (const char* data, int size) { return ImageCache::getFromMemory (data, size); }
#define C60_IMG(name) asset (BinaryData::name, BinaryData::name##Size)

// palette del display fluorescente e delle serigrafie
const Colour vfdCyan   (0xff5ff2e6);
const Colour vfdAmber  (0xffffb347);
const Colour vfdRed    (0xffff4a3d);
const Colour vfdGhost  (0x1a5ff2e6);
const Colour silk      (0xcfd9d2c4);
const Colour ledRed    (0xffff3b2f);
const Colour ledAmber  (0xffffa733);
const Colour ledGreen  (0xff63f58a);

void glowText (Graphics& g, const String& t, Rectangle<float> r, Colour c, float h, Justification j = Justification::centred)
{
    g.setFont (FontOptions (h, Font::bold));
    g.setColour (c.withAlpha (0.28f));
    for (auto d : { Point<float> (-0.7f, 0), Point<float> (0.7f, 0), Point<float> (0, -0.7f), Point<float> (0, 0.7f) })
        g.drawText (t, r.translated (d.x, d.y), j, false);
    g.setColour (c);
    g.drawText (t, r, j, false);
}

String timeString (int64_t samples, double sr)
{
    const bool neg = samples < 0;
    const auto s = (int64_t) (std::abs ((double) samples) / jmax (1.0, sr));
    return String (neg ? "-" : " ") + String (s / 60) + ":" + String (s % 60).paddedLeft ('0', 2);
}
}

//==============================================================================
ImageKnob::ImageKnob (juce::Image image, int numSteps)
    : juce::Slider (RotaryHorizontalVerticalDrag, NoTextBox), img (image), steps (numSteps)
{
    const float arc = steps > 1 ? juce::degreesToRadians (45.0f * (float) (steps - 1)) : juce::degreesToRadians (270.0f);
    setRotaryParameters (-arc * 0.5f, arc * 0.5f, true);
    setMouseDragSensitivity (steps > 1 ? 120 : 260);
    setVelocityBasedMode (false);
    setPopupDisplayEnabled (steps == 0, true, nullptr);
}

void ImageKnob::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const auto rp = getRotaryParameters();
    const float ang = rp.startAngleRadians + (float) valueToProportionOfLength (getValue()) * (rp.endAngleRadians - rp.startAngleRadians);

    // ombra di contatto sulla piastra
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (b.reduced (b.getWidth() * 0.04f).translated (0, b.getHeight() * 0.05f));

    const auto t = juce::AffineTransform::scale (b.getWidth() / (float) img.getWidth(), b.getHeight() / (float) img.getHeight())
                       .rotated (ang, b.getCentreX(), b.getCentreY());
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImageTransformed (img, t);
}

// clic senza trascinare su un selettore → posizione successiva (come sui deck veri)
void ImageKnob::mouseUp (const juce::MouseEvent& e)
{
    juce::Slider::mouseUp (e);
    if (steps > 1 && ! e.mouseWasDraggedSinceMouseDown())
        setValue (std::fmod (getValue() + 1.0, (double) steps), juce::sendNotificationSync);
}

//==============================================================================
DeckKey::DeckKey (juce::Image up, juce::Image down, juce::Colour ledColour, juce::Rectangle<float> ledArea)
    : juce::Button ({}), upImg (up), downImg (down), led (ledColour), ledRel (ledArea) {}

void DeckKey::paintButton (juce::Graphics& g, bool, bool isDown)
{
    auto b = getLocalBounds().toFloat();
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (isDown || getToggleState() ? downImg : upImg, b);

    if (ledRel.isEmpty()) return;
    const float lvl = ledLevel ? juce::jlimit (0.f, 1.f, ledLevel()) : 0.f;
    auto r = juce::Rectangle<float> (b.getWidth() * (ledRel.getX() - ledRel.getWidth() * 0.5f),
                                     b.getHeight() * (ledRel.getY() - ledRel.getHeight() * 0.5f),
                                     b.getWidth() * ledRel.getWidth(), b.getHeight() * ledRel.getHeight());
    const float rad = r.getHeight() * 0.5f;
    g.setColour (led.withMultipliedBrightness (0.25f).withAlpha (0.55f));
    g.fillRoundedRectangle (r, rad);
    if (lvl > 0.f)
    {
        g.setColour (led.withAlpha (0.25f * lvl));
        g.fillRoundedRectangle (r.expanded (rad * 1.6f), rad * 2.2f);
        g.setColour (led.withAlpha (lvl));
        g.fillRoundedRectangle (r, rad);
        g.setColour (juce::Colours::white.withAlpha (0.45f * lvl));
        g.fillRoundedRectangle (r.reduced (r.getWidth() * 0.15f, r.getHeight() * 0.3f), rad * 0.5f);
    }
}

// Il componente va da (base − anchorUp·h) a (base + (1 − anchorDown)·h): le due immagini
// hanno la base della leva in punti diversi, così la base resta ferma sul foro.
void DeckToggle::paintButton (juce::Graphics& g, bool, bool)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    const float w = (float) getWidth(), h = (float) imageH;
    if (getToggleState()) g.drawImage (upImg, { 0, 0, w, h });
    else                  g.drawImage (downImg, { 0, (anchorUp - anchorDown) * h, w, h });
}

//==============================================================================
CassetteView::CassetteView (C60AudioProcessor& p) : proc (p)
{
    shells[0] = C60_IMG (cassette_type1_png);
    shells[1] = C60_IMG (cassette_type2_png);
    shells[2] = C60_IMG (cassette_type4_png);
    hubImg = C60_IMG (reel_hub_png);
    setInterceptsMouseClicks (false, false);
}

void CassetteView::tick (double dt)
{
    const auto d = proc.getDeckState();
    if (! d.playing || d.length <= 0) return;
    const double frac = juce::jlimit (0.0, 1.0, (double) d.position / (double) d.length);
    const double rate = 1.0 + proc.apvts.getRawParameterValue (ParamID::pitch)->load() * 0.01;
    // velocità lineare costante: la bobina piccola gira più veloce
    const double rL = 0.36 + 0.64 * std::sqrt (1.0 - frac), rR = 0.36 + 0.64 * std::sqrt (frac);
    angleL += dt * rate * 3.2 / rL;
    angleR += dt * rate * 3.2 / rR;
}

void CassetteView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0d0e10));
    g.fillRect (b);

    const auto d = proc.getDeckState();
    const int type = juce::jlimit (0, 2, (int) proc.apvts.getRawParameterValue (ParamID::tape)->load());
    auto body = bodyInWindow;
    const float bw = body.getWidth();
    const double frac = d.length > 0 ? juce::jlimit (0.0, 1.0, (double) d.position / (double) d.length) : 0.0;

    auto hub = [&] (float cx, float cy) { return juce::Point<float> (body.getX() + cx * bw, body.getY() + cy * body.getHeight()); };
    const auto hl = hub (0.295f, 0.47f), hr = hub (0.704f, 0.47f);
    const float pMin = 0.075f * bw, pMax = 0.21f * bw;
    const float rl = pMin + (pMax - pMin) * (float) std::sqrt (1.0 - frac), rr = pMin + (pMax - pMin) * (float) std::sqrt (frac);

    // nastro avvolto (dietro al guscio: si vede dalla finestra e dai gusci trasparenti)
    for (auto [c, r] : { std::pair { hl, rl }, std::pair { hr, rr } })
    {
        juce::ColourGradient grad (juce::Colour (0xff3a2a20), c, juce::Colour (0xff1c140f), c.translated (r, 0), true);
        g.setGradientFill (grad);
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    }

    // perni di trascinamento che ruotano dentro i fori dei mozzi
    const float hubR = 0.049f * bw;
    for (auto [c, a] : { std::pair { hl, angleL }, std::pair { hr, angleR } })
    {
        g.setColour (juce::Colour (0xff1a1b1d));
        g.fillEllipse (c.x - hubR, c.y - hubR, 2 * hubR, 2 * hubR);
        g.setColour (juce::Colour (0xffcfcac0));
        for (int k = 0; k < 6; ++k)
        {
            juce::Path tooth;
            tooth.addRoundedRectangle (-hubR * 0.12f, -hubR * 0.95f, hubR * 0.24f, hubR * 0.45f, hubR * 0.06f);
            g.fillPath (tooth, juce::AffineTransform::rotation ((float) a + k * juce::MathConstants<float>::twoPi / 6.0f).translated (c));
        }
        g.setColour (juce::Colour (0xff8a857c));
        g.fillEllipse (c.x - hubR * 0.28f, c.y - hubR * 0.28f, hubR * 0.56f, hubR * 0.56f);
    }

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (shells[type], body);

    // mozzi della cassetta che girano col nastro
    const float hd = 0.15f * bw;
    for (auto [c, a] : { std::pair { hl, angleL }, std::pair { hr, angleR } })
        g.drawImageTransformed (hubImg, juce::AffineTransform::scale (hd / (float) hubImg.getWidth())
                                            .translated (c.x - hd * 0.5f, c.y - hd * 0.5f)
                                            .rotated ((float) a, c.x, c.y));

    // vetro fumé dello sportello + riflesso
    g.setColour (juce::Colour (0x40000000));
    g.fillRect (b);
    juce::Path refl;
    refl.startNewSubPath (b.getX() + b.getWidth() * 0.55f, b.getY());
    refl.lineTo (b.getX() + b.getWidth() * 0.78f, b.getY());
    refl.lineTo (b.getX() + b.getWidth() * 0.38f, b.getBottom());
    refl.lineTo (b.getX() + b.getWidth() * 0.15f, b.getBottom());
    refl.closeSubPath();
    g.setColour (juce::Colours::white.withAlpha (0.045f));
    g.fillPath (refl);
}

//==============================================================================
void VfdDisplay::tick (double dt)
{
    const float lv[2] = { proc.outLevelL.load(), proc.outLevelR.load() };
    for (int c = 0; c < 2; ++c)
    {
        const float db = juce::Decibels::gainToDecibels (lv[c], -90.f);
        levelDb[c] = db > levelDb[c] ? db : juce::jmax (db, levelDb[c] - (float) dt * 26.f);
        if (db >= peakDb[c]) { peakDb[c] = db; peakHold[c] = 1.5f; }
        else if ((peakHold[c] -= (float) dt) <= 0.f) peakDb[c] = juce::jmax (db, peakDb[c] - (float) dt * 12.f);
    }
}

void VfdDisplay::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float H = b.getHeight(), W = b.getWidth();
    auto inner = b.reduced (W * 0.035f, H * 0.07f);

    auto meters = inner.removeFromTop (H * 0.36f);
    drawMeter (g, meters.removeFromTop (meters.getHeight() * 0.33f), levelDb[0], peakDb[0], "L");
    auto scale = meters.removeFromTop (meters.getHeight() * 0.5f);
    drawMeter (g, meters, levelDb[1], peakDb[1], "R");

    // scala tra i due meter
    const float x0 = scale.getX() + W * 0.04f, x1 = scale.getRight();
    for (int db : { -40, -30, -20, -10, -6, -3, 0, 3, 6 })
    {
        const float x = x0 + (x1 - x0) * (float) (db + 40) / 46.f;
        glowText (g, (db > 0 ? "+" : "") + juce::String (db), { x - 20, scale.getY(), 40, scale.getHeight() },
                  db > 0 ? vfdRed : db >= -6 ? vfdAmber : vfdCyan, H * 0.045f);
    }

    inner.removeFromTop (H * 0.05f);
    auto mid = inner.removeFromTop (H * 0.28f);
    const auto d = proc.getDeckState();
    drawCounter (g, mid.removeFromLeft (W * 0.27f), d.position - d.memo, d.sampleRate);
    mid.removeFromLeft (W * 0.03f);
    drawTimeline (g, mid);

    inner.removeFromTop (H * 0.04f);
    drawIndicators (g, inner);
}

void VfdDisplay::drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float db, float pk, const juce::String& ch)
{
    glowText (g, ch, r.removeFromLeft (r.getHeight() * 1.3f), vfdCyan, r.getHeight() * 0.9f);
    constexpr int segs = 46;
    const float sw = r.getWidth() / segs;
    for (int i = 0; i < segs; ++i)
    {
        const float segDb = -40.f + (float) i;
        const juce::Colour c = segDb >= 0.f ? vfdRed : segDb >= -6.f ? vfdAmber : vfdCyan;
        const bool lit = db >= segDb || (pk >= segDb && pk < segDb + 1.f && pk > -40.f);
        auto s = juce::Rectangle<float> (r.getX() + i * sw, r.getY() + r.getHeight() * 0.15f, sw * 0.72f, r.getHeight() * 0.7f);
        g.setColour (lit ? c : c.withAlpha (0.09f));
        g.fillRect (s);
        if (lit) { g.setColour (c.withAlpha (0.22f)); g.fillRect (s.expanded (1.2f)); }
    }
}

// contagiri a 7 segmenti con i segmenti spenti visibili ("fantasmi"), come sui VFD veri
void VfdDisplay::drawCounter (juce::Graphics& g, juce::Rectangle<float> r, int64_t samples, double sr)
{
    static const int segMap[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
    const juce::String t = timeString (samples, sr);           // " M:SS" / "-M:SS"
    const float dh = juce::jmin (r.getHeight() * 0.9f, r.getWidth() / 3.3f), dw = dh * 0.52f, th = dh * 0.11f;
    float x = r.getX();
    auto drawDigit = [&] (int mask, float px)
    {
        const float y = r.getY() + (r.getHeight() - dh) * 0.5f;
        juce::Rectangle<float> seg[7] = {
            { px + th, y, dw - 2 * th, th },                       // a
            { px + dw - th, y + th, th, dh * 0.5f - 1.5f * th },   // b
            { px + dw - th, y + dh * 0.5f + 0.5f * th, th, dh * 0.5f - 1.5f * th }, // c
            { px + th, y + dh - th, dw - 2 * th, th },             // d
            { px, y + dh * 0.5f + 0.5f * th, th, dh * 0.5f - 1.5f * th }, // e
            { px, y + th, th, dh * 0.5f - 1.5f * th },             // f
            { px + th, y + dh * 0.5f - 0.5f * th, dw - 2 * th, th } // g
        };
        for (int s = 0; s < 7; ++s)
        {
            const bool on = (mask >> s) & 1;
            g.setColour (on ? vfdCyan : vfdGhost);
            g.fillRect (seg[s].transformedBy (juce::AffineTransform::shear (-0.08f, 0).translated (dh * 0.04f, 0)));
            if (on) { g.setColour (vfdCyan.withAlpha (0.2f)); g.fillRect (seg[s].expanded (1.0f)); }
        }
    };
    for (auto ch : t)
    {
        if (ch == ':')
        {
            g.setColour (vfdCyan);
            const float cx = x + dw * 0.2f, cy = r.getCentreY();
            g.fillRect (cx, cy - dh * 0.22f, th, th);
            g.fillRect (cx, cy + dh * 0.12f, th, th);
            x += dw * 0.55f;
        }
        else if (ch == '-' || ch == ' ')
        {
            drawDigit (ch == '-' ? 0x40 : 0, x);
            x += dw * 1.25f;
        }
        else
        {
            drawDigit (segMap[ch - '0'], x);
            x += dw * 1.25f;
        }
    }
}

void VfdDisplay::drawTimeline (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto d = proc.getDeckState();
    const auto& tape = proc.getTape();
    auto top = r.removeFromTop (r.getHeight() * 0.3f);
    auto bar = r.removeFromTop (r.getHeight() * 0.55f).reduced (0, r.getHeight() * 0.05f);

    g.setColour (vfdGhost);
    g.fillRect (bar);
    const int cells = tape.cells();
    if (cells > 0 && d.length > 0)
    {
        static const juce::Colour pal[4] = { vfdCyan, vfdAmber, juce::Colour (0xff7fd0ff), juce::Colour (0xffb0ff8a) };
        const float cw = bar.getWidth() / (float) cells;
        for (int i = 0; i < cells;)
        {
            const uint16_t p = tape.passAt (i);
            int j = i + 1;
            while (j < cells && tape.passAt (j) == p) ++j;
            if (p != 0)
            {
                g.setColour (pal[p % 4].withAlpha (p == d.currentPass ? 1.f : 0.6f));
                g.fillRect (bar.getX() + i * cw, bar.getY(), juce::jmax (1.f, (j - i) * cw - 0.6f), bar.getHeight());
            }
            i = j;
        }
        auto xOf = [&] (int64_t s) { return bar.getX() + bar.getWidth() * (float) juce::jlimit (0.0, 1.0, (double) s / (double) d.length); };
        for (auto [s, lbl] : { std::pair { d.in, "IN" }, std::pair { d.out, "OUT" } })
        {
            const float x = xOf (s);
            glowText (g, lbl, { x - 20, top.getY(), 40, top.getHeight() }, vfdAmber, top.getHeight() * 0.85f);
            g.setColour (vfdAmber);
            g.fillRect (x - 0.75f, bar.getY() - 2, 1.5f, bar.getHeight() + 4);
        }
        g.setColour (d.recording ? vfdRed : juce::Colours::white);
        g.fillRect (xOf (d.position) - 1.f, bar.getY() - 4, 2.f, bar.getHeight() + 8);
    }
    glowText (g, "0", r.removeFromLeft (20), vfdCyan.withAlpha (0.6f), r.getHeight() * 0.7f, juce::Justification::centredLeft);
    glowText (g, "10 MIN", r.removeFromRight (60), vfdCyan.withAlpha (0.6f), r.getHeight() * 0.7f, juce::Justification::centredRight);
}

void VfdDisplay::drawIndicators (juce::Graphics& g, juce::Rectangle<float> r)
{
    auto& s = proc.apvts;
    auto v = [&] (const char* id) { return s.getRawParameterValue (id)->load(); };
    const auto d = proc.getDeckState();
    const int nr = (int) v (ParamID::nr), type = (int) v (ParamID::tape);
    const bool blinkOn = (juce::Time::getMillisecondCounter() / 400) % 2 == 0;

    struct Ind { juce::String text; bool on; juce::Colour col; };
    const Ind items[] = {
        { "REC",        d.recording || (v (ParamID::rec) > 0.5f && blinkOn), vfdRed },
        { "SOURCE",     v (ParamID::monitor) < 0.5f, vfdCyan },
        { "TAPE",       v (ParamID::monitor) > 0.5f, vfdCyan },
        { type == 0 ? "TYPE I" : type == 1 ? "TYPE II" : "TYPE IV", true, vfdCyan },
        { v (ParamID::speed) > 0.5f ? "9.5" : "4.76", true, vfdCyan },
        { nr == 0 ? "NR OFF" : nr == 4 ? "DBX" : "NR " + juce::String::charToString ("BCS"[nr - 1]), nr != 0, vfdCyan },
        { "HX PRO",     v (ParamID::hxPro) > 0.5f, vfdCyan },
        { "AUTO PUNCH", v (ParamID::autoPunch) > 0.5f, vfdAmber },
        { "CAL",        calBusy ? blinkOn : d.calibrated, vfdAmber },
        { "AUTO AZ",    v (ParamID::autoAz) > 0.5f, vfdCyan },
        { "PITCH",      std::abs (v (ParamID::pitch)) > 0.05f, vfdAmber },
    };
    const float w = r.getWidth() / (float) std::size (items);
    for (auto& it : items)
        glowText (g, it.text, r.removeFromLeft (w), it.on ? it.col : it.col.withAlpha (0.1f), juce::jmin (r.getHeight() * 0.5f, w / (float) juce::jmax (4, it.text.length()) * 1.45f));
}

//==============================================================================
C60AudioProcessorEditor::C60AudioProcessorEditor (C60AudioProcessor& p)
    : AudioProcessorEditor (p), proc (p), layout (BinaryData::layout_json, BinaryData::layout_jsonSize),
      cassette (p), display (p)
{
    plate = C60_IMG (bg_plate_jpg);

    // knob e selettori
    for (auto id : { ParamID::input, ParamID::output })                                   addKnob (id, C60_IMG (knob_large_png));
    for (auto id : { ParamID::pitch, ParamID::wow, ParamID::flutter, ParamID::hiss, ParamID::age, ParamID::mix }) addKnob (id, C60_IMG (knob_small_png));
    addKnob (ParamID::nr, C60_IMG (selector_png), 5);
    addKnob (ParamID::tape, C60_IMG (selector_png), 3);
    addKnob (ParamID::speed, C60_IMG (selector_png), 2);
    addKnob (ParamID::bias, C60_IMG (trimmer_png));
    addKnob (ParamID::azimuth, C60_IMG (trimmer_png));

    // levette
    for (auto id : { ParamID::hxPro, ParamID::autoGain, ParamID::monitor })
    {
        auto* t = toggles.add (new DeckToggle (C60_IMG (toggle_up_png), C60_IMG (toggle_down_png)));
        addAndMakeVisible (t);
        if (juce::String (id) == ParamID::monitor)
        {
            // MONITOR è una scelta (Source/Tape): levetta su = TAPE
            auto* prm = proc.apvts.getParameter (id);
            t->onClick = [t, prm] { prm->setValueNotifyingHost (t->getToggleState() ? 1.f : 0.f); };
        }
        else
            buttonAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, id, *t));
        byName[id] = t;
    }

    // tasti
    const auto keyLed = juce::Rectangle<float> (layout.number ("style/keyLed/cx"), layout.number ("style/keyLed/cy"),
                                                layout.number ("style/keyLed/w"), layout.number ("style/keyLed/h"));
    const auto wideLed = juce::Rectangle<float> (layout.number ("style/wideKeyLed/cx"), layout.number ("style/wideKeyLed/cy"),
                                                 layout.number ("style/wideKeyLed/w"), layout.number ("style/wideKeyLed/h"));
    auto keyUp = C60_IMG (key_up_png), keyDown = C60_IMG (key_down_png);
    recKey       = &addKey (keyUp, keyDown, ledRed, keyLed);
    punchInKey   = &addKey (keyUp, keyDown, ledAmber, keyLed);
    punchOutKey  = &addKey (keyUp, keyDown, ledAmber, keyLed);
    autoPunchKey = &addKey (keyUp, keyDown, ledAmber, keyLed);
    recMuteKey   = &addKey (keyUp, keyDown, ledAmber, keyLed);
    undoKey      = &addKey (keyUp, keyDown, ledGreen, keyLed);
    eraseKey     = &addKey (C60_IMG (key_small_up_png), C60_IMG (key_small_down_png), ledRed, wideLed);
    exportKey    = &addKey (C60_IMG (key_small_up_png), C60_IMG (key_small_down_png), ledAmber, wideLed);
    auto sqUp = C60_IMG (button_sq_up_png), sqDown = C60_IMG (button_sq_down_png);
    const auto sqLed = juce::Rectangle<float>();   // spie nei fori cal/led* della piastra
    calKey    = &addKey (sqUp, sqDown, ledAmber, sqLed);
    autoAzKey = &addKey (sqUp, sqDown, ledGreen, sqLed);
    memoKey   = &addKey (sqUp, sqDown, ledAmber, sqLed);

    for (auto [k, id] : { std::pair { recKey, ParamID::rec }, std::pair { autoPunchKey, ParamID::autoPunch },
                          std::pair { recMuteKey, ParamID::recMute }, std::pair { autoAzKey, ParamID::autoAz } })
    {
        k->setClickingTogglesState (true);
        buttonAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, id, *k));
        k->ledLevel = [k] { return k->getToggleState() ? 1.f : 0.f; };
    }
    recKey->ledLevel = [this] { return proc.getDeckState().recording ? 1.f : (recKey->getToggleState() ? (blink < 0.5 ? 1.f : 0.25f) : 0.f); };
    undoKey->ledLevel = [this] { return proc.getDeckState().canUndo ? 0.8f : 0.f; };
    calKey->ledLevel = [this] { return calTimer > 0 ? (std::fmod (calTimer * 6, 1.0) < 0.5 ? 1.f : 0.f)
                                                    : (proc.getDeckState().calibrated ? 1.f : (blink < 0.5 ? 0.6f : 0.f)); };
    for (auto* k : { punchInKey, punchOutKey, eraseKey, exportKey, memoKey })
        k->ledLevel = [this, k] { auto it = flash.find (k); return it != flash.end() && it->second > 0 ? 1.f : 0.f; };

    punchInKey->onClick  = [this] { proc.setPunchInHere();  flash[punchInKey] = 0.4; };
    punchOutKey->onClick = [this] { proc.setPunchOutHere(); flash[punchOutKey] = 0.4; };
    memoKey->onClick     = [this] { proc.setMemoHere();     flash[memoKey] = 0.4; };
    undoKey->onClick     = [this] { proc.undoLastPass(); };
    calKey->onClick      = [this] { if (calTimer <= 0) calTimer = 2.0; };   // ~2 s di misura (400 Hz / 10 kHz), poi calibra
    eraseKey->onClick    = [this]
    {
        flash[eraseKey] = 0.4;
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Cancellare il nastro?",
                                            "Tutte le registrazioni andranno perse.", "Cancella", "Annulla", this,
                                            juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) proc.eraseTape(); }));
    };
    exportKey->onClick = [this]
    {
        flash[exportKey] = 0.4;
        chooser = std::make_unique<juce::FileChooser> ("Esporta il nastro (WAV 24 bit)",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("C-60 tape.wav"), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (f == juce::File()) return;
                                  juce::String err;
                                  if (! proc.exportTape (f.withFileExtension ("wav"), err))
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Esportazione", err);
                              });
    };

    recKey->setTooltip ("REC: arma la registrazione. Registra quando il DAW è in play.");
    punchInKey->setTooltip ("PUNCH IN: fissa il punto IN alla posizione attuale.");
    punchOutKey->setTooltip ("PUNCH OUT: fissa il punto OUT alla posizione attuale.");
    autoPunchKey->setTooltip ("AUTO PUNCH: registra solo tra IN e OUT.");
    recMuteKey->setTooltip ("REC MUTE: registra silenzio.");
    undoKey->setTooltip ("UNDO: annulla l'ultima passata.");
    eraseKey->setTooltip ("ERASE: cancella tutto il nastro.");
    exportKey->setTooltip ("EXPORT: salva il nastro come WAV, come si sente in TAPE.");
    calKey->setTooltip ("CAL: calibra livello, bias e azimut per il nastro inserito.");
    autoAzKey->setTooltip ("AUTO AZ: correzione automatica dell'azimut.");
    memoKey->setTooltip ("MEMO: azzera il contagiri qui.");

    addAndMakeVisible (cassette);
    addAndMakeVisible (display);
    display.setInterceptsMouseClicks (false, false);

    const int w = (int) layout.number ("window/w", 1291), h = (int) layout.number ("window/h", 506);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) w / h);
    setResizeLimits (w * 6 / 10, h * 6 / 10, w * 3 / 2, h * 3 / 2);
    setSize (w, h);
    lastTick = juce::Time::getMillisecondCounter();
    startTimerHz (30);
}

C60AudioProcessorEditor::~C60AudioProcessorEditor()
{
    stopTimer();
    buttonAtt.clear();
    sliderAtt.clear();
}

ImageKnob& C60AudioProcessorEditor::addKnob (const char* id, juce::Image img, int steps)
{
    auto* k = knobs.add (new ImageKnob (img, steps));
    addAndMakeVisible (k);
    sliderAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, id, *k));
    if (auto* prm = proc.apvts.getParameter (id))
    {
        k->setDoubleClickReturnValue (steps == 0, prm->convertFrom0to1 (prm->getDefaultValue()));
        k->setTooltip (prm->getName (64));
    }
    byName[id] = k;
    return *k;
}

DeckKey& C60AudioProcessorEditor::addKey (juce::Image up, juce::Image down, juce::Colour led, juce::Rectangle<float> ledArea)
{
    auto* k = keys.add (new DeckKey (up, down, led, ledArea));
    addAndMakeVisible (k);
    return *k;
}

void C60AudioProcessorEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    const double dt = juce::jlimit (0.0, 0.2, (now - lastTick) * 0.001);
    lastTick = now;

    blink = std::fmod (blink + dt, 1.0);
    for (auto& [k, t] : flash) t -= dt;
    if (calTimer > 0 && (calTimer -= dt) <= 0) proc.calibrate();
    display.calBusy = calTimer > 0;

    // la levetta MONITOR segue il parametro (automazione dal DAW)
    if (auto* t = dynamic_cast<DeckToggle*> (byName[ParamID::monitor]))
        t->setToggleState (proc.apvts.getRawParameterValue (ParamID::monitor)->load() > 0.5f, juce::dontSendNotification);

    cassette.tick (dt);
    display.tick (dt);
    cassette.repaint();
    display.repaint();
    for (auto* k : keys) k->repaint();
    for (auto path : { "cal/ledcal", "cal/ledautoaz", "cal/ledmemo" })
        repaint (layout.rect (path, getLocalBounds()).expanded (8));
}

void C60AudioProcessorEditor::drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> under, float gap, juce::Justification j)
{
    const float H = (float) getHeight();
    const float lh = layout.number ("style/labelH") * H;
    const float w = juce::jmax ((float) under.getWidth() * 1.05f, lh * 3.0f);
    juce::Rectangle<float> r ((float) under.getCentreX() - w * 0.5f, (float) under.getBottom() + gap * H, w, lh * 1.6f);
    g.setFont (juce::FontOptions (lh * 0.62f, juce::Font::bold).withKerningFactor (0.06f));
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawFittedText (text, r.translated (0, 1).toNearestInt(), j, 2, 0.8f);
    g.setColour (silk);
    g.drawFittedText (text, r.toNearestInt(), j, 2, 0.8f);
}

void C60AudioProcessorEditor::paint (juce::Graphics& g)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (plate, getLocalBounds().toFloat());

    const float gapK = layout.number ("style/labelGapKnob"), gapKey = layout.number ("style/labelGapKey");
    auto R = [this] (const juce::String& path) { return layout.rect (path, getLocalBounds()); };

    for (auto [path, text] : { std::pair { "knobs/input", "RECORD LEVEL" }, std::pair { "knobs/output", "OUTPUT" },
                               std::pair { "knobs/pitch", "PITCH" }, std::pair { "knobs/wow", "WOW" }, std::pair { "knobs/flutter", "FLUTTER" },
                               std::pair { "knobs/hiss", "HISS" }, std::pair { "knobs/age", "AGE" }, std::pair { "knobs/mix", "MIX" },
                               std::pair { "selectors/nr", "NR" }, std::pair { "selectors/tape", "TAPE" }, std::pair { "selectors/speed", "SPEED" } })
        drawLabel (g, text, R (path), gapK);

    for (auto [path, text] : { std::pair { "keys/rec", "REC" }, std::pair { "keys/punchin", "PUNCH IN" }, std::pair { "keys/punchout", "PUNCH OUT" },
                               std::pair { "keys/autopunch", "AUTO PUNCH" }, std::pair { "keys/recmute", "REC MUTE" }, std::pair { "keys/undo", "UNDO" },
                               std::pair { "widekeys/erase", "ERASE" }, std::pair { "widekeys/export", "EXPORT" } })
        drawLabel (g, text, R (path), gapKey);

    const float aUp = layout.number ("style/toggleAnchorUp", 0.72f), aDown = layout.number ("style/toggleAnchorDown", 0.28f);
    for (auto [path, text] : { std::pair { "toggles/hxpro", "HX PRO" }, std::pair { "toggles/autogain", "AUTO GAIN" }, std::pair { "toggles/monitor", "MONITOR" } })
    {
        auto r = R (path);
        const int cy = r.getCentreY(), h = r.getHeight();
        // nome sotto la leva, stati a destra delle due estremità
        drawLabel (g, text, r.withY (cy).withHeight (juce::roundToInt ((1.0f - aDown) * h)), gapK * 0.5f);
        const bool mon = juce::String (path) == "toggles/monitor";
        const float lh = layout.number ("style/labelH") * getHeight();
        const float x = (float) r.getRight() + layout.number ("style/toggleLabelDx") * getHeight();
        g.setFont (juce::FontOptions (lh * 0.7f, juce::Font::bold));
        g.setColour (silk);
        g.drawText (mon ? "TAPE" : "ON",    juce::Rectangle<float> (x, cy - aUp * h, 90, lh), juce::Justification::centredLeft, false);
        g.drawText (mon ? "SOURCE" : "OFF", juce::Rectangle<float> (x, cy + (1.0f - aDown) * h - lh, 90, lh), juce::Justification::centredLeft, false);
    }

    // posizioni dei selettori attorno al knob
    const float selR = layout.number ("style/selectorRadius") * getHeight();
    for (auto [path, names] : { std::pair { "selectors/nr", juce::StringArray { "OFF", "B", "C", "S", "DBX" } },
                                std::pair { "selectors/tape", juce::StringArray { "I", "II", "IV" } },
                                std::pair { "selectors/speed", juce::StringArray { "4.76", "9.5" } } })
    {
        const auto c = R (path).getCentre().toFloat();
        const int n = names.size();
        const float arc = juce::degreesToRadians (45.0f * (float) (n - 1));
        for (int i = 0; i < n; ++i)
        {
            const float a = -arc * 0.5f + arc * (float) i / (float) juce::jmax (1, n - 1);
            const auto p = c + juce::Point<float> (std::sin (a), -std::cos (a)) * selR;
            g.setFont (juce::FontOptions (getHeight() * 0.018f, juce::Font::bold));
            g.setColour (silk);
            g.drawText (names[i], juce::Rectangle<float> (60, 14).withCentre (p), juce::Justification::centred, false);
        }
    }

    // sportello di calibrazione
    {
        auto t = R ("cal/title");
        g.setFont (juce::FontOptions (t.getHeight() * 0.8f, juce::Font::bold).withKerningFactor (0.15f));
        g.setColour (silk);
        g.drawText ("CALIBRATION", t, juce::Justification::centred, false);
        for (auto [path, text] : { std::pair { "cal/bias", "BIAS" }, std::pair { "cal/azimuth", "AZIMUTH" },
                                   std::pair { "cal/calbtn", "CAL" }, std::pair { "cal/autoaz", "AUTO AZ" }, std::pair { "cal/memo", "MEMO" } })
            drawLabel (g, text, R (path), 0.008f);
    }

    // spie dello sportello di calibrazione
    for (auto [path, k, col] : { std::tuple { "cal/ledcal", calKey, ledAmber }, std::tuple { "cal/ledautoaz", autoAzKey, ledGreen },
                                 std::tuple { "cal/ledmemo", memoKey, ledAmber } })
    {
        auto r = R (path).toFloat();
        const float lvl = k->ledLevel ? juce::jlimit (0.f, 1.f, k->ledLevel()) : 0.f;
        g.setColour (col.withMultipliedBrightness (0.25f).withAlpha (0.7f));
        g.fillEllipse (r);
        if (lvl > 0.f)
        {
            g.setColour (col.withAlpha (0.3f * lvl)); g.fillEllipse (r.expanded (r.getWidth() * 0.8f));
            g.setColour (col.withAlpha (lvl));        g.fillEllipse (r);
        }
    }

    // marchio + versione (VERSION di CMake = unica fonte)
    {
        auto r = R ("brand");
        g.setFont (juce::FontOptions (r.getHeight() * 0.85f, juce::Font::bold).withKerningFactor (0.2f));
        g.setColour (silk);
        g.drawText ("C-60   CASSETTE DECK", r, juce::Justification::centredLeft, false);
        g.setFont (juce::FontOptions (r.getHeight() * 0.55f));
        g.drawText ("v" JucePlugin_VersionString, r, juce::Justification::centredRight, false);
    }
}

void C60AudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto R = [&] (const juce::String& p) { return layout.rect (p, area); };

    for (auto id : { ParamID::input, ParamID::output, ParamID::pitch, ParamID::wow, ParamID::flutter, ParamID::hiss, ParamID::age, ParamID::mix })
        byName[id]->setBounds (R (juce::String ("knobs/") + id));
    byName[ParamID::nr]->setBounds (R ("selectors/nr"));
    byName[ParamID::tape]->setBounds (R ("selectors/tape"));
    byName[ParamID::speed]->setBounds (R ("selectors/speed"));
    byName[ParamID::bias]->setBounds (R ("cal/bias"));
    byName[ParamID::azimuth]->setBounds (R ("cal/azimuth"));

    // levette: la base della leva cade sul centro del foro in entrambe le posizioni
    const float aUp = layout.number ("style/toggleAnchorUp", 0.72f), aDown = layout.number ("style/toggleAnchorDown", 0.28f);
    for (auto [id, path] : { std::pair { ParamID::hxPro, "toggles/hxpro" }, std::pair { ParamID::autoGain, "toggles/autogain" },
                             std::pair { ParamID::monitor, "toggles/monitor" } })
    {
        auto r = R (path);
        auto* t = static_cast<DeckToggle*> (byName[id]);
        t->anchorUp = aUp; t->anchorDown = aDown; t->imageH = r.getHeight();
        t->setBounds (r.getX(), r.getCentreY() - juce::roundToInt (aUp * r.getHeight()),
                      r.getWidth(), juce::roundToInt ((aUp + 1.0f - aDown) * r.getHeight()));
    }

    recKey->setBounds (R ("keys/rec"));             punchInKey->setBounds (R ("keys/punchin"));
    punchOutKey->setBounds (R ("keys/punchout"));   autoPunchKey->setBounds (R ("keys/autopunch"));
    recMuteKey->setBounds (R ("keys/recmute"));     undoKey->setBounds (R ("keys/undo"));
    eraseKey->setBounds (R ("widekeys/erase"));     exportKey->setBounds (R ("widekeys/export"));
    calKey->setBounds (R ("cal/calbtn"));           autoAzKey->setBounds (R ("cal/autoaz"));
    memoKey->setBounds (R ("cal/memo"));

    const auto win = R ("cassette/window");
    cassette.setBounds (win);
    cassette.bodyInWindow = R ("cassette/body").toFloat().translated ((float) -win.getX(), (float) -win.getY());
    display.setBounds (R ("display"));
}
