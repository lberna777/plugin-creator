// Display 128x64 della Mikro disegnato con juce::Graphics (font veri, icone vettoriali) e convertito a 1 bit.
// Due livelli: "ink" (testi e linee, soglia netta) e "tone" (aree sfumate, dithering ordinato Bayer 4x4,
// stabile da un frame all'altro: niente sfarfallio come col Floyd-Steinberg). Dove l'ink copre vince l'ink.
// Immagini preallocate: render() alloca solo il contesto grafico, e il ponte lo chiama solo a stato cambiato.
#pragma once

#include "Display.h"
#include "Scales.h"

#include <juce_graphics/juce_graphics.h>

namespace mikro
{
// Tutto quello che si vede sul display: solo interi, si confronta a ogni giro senza allocare
struct DisplayState
{
    int mode = 0;            // 0 batteria, 1 tastiera, 2 accordi
    int lastNote = -1, lastVelocity = 0, lastPad = -1;
    int root = 0, scale = 0, channel = 1, curve = 1;
    int lowNote = 0, highNote = 0;   // note di pad 1 e pad 16
    uint16_t held = 0;               // pad che suonano adesso
    bool aftertouch = false;
    bool harmony = false;            // tastiera: solo note che stanno bene insieme

    bool inKey (int pc) const noexcept { return harmony ? scales::inConsonant (scale, root, pc) : scales::inScale (scale, root, pc); }

    bool operator== (const DisplayState& o) const noexcept
    {
        return mode == o.mode && lastNote == o.lastNote && lastVelocity == o.lastVelocity && lastPad == o.lastPad
            && root == o.root && scale == o.scale && channel == o.channel && curve == o.curve
            && lowNote == o.lowNote && highNote == o.highNote && held == o.held && aftertouch == o.aftertouch && harmony == o.harmony;
    }
    bool operator!= (const DisplayState& o) const noexcept { return ! (*this == o); }
};

class DisplayRenderer
{
public:
    DisplayRenderer()
        : ink (juce::Image::ARGB, kDisplayW, kDisplayH, true, juce::SoftwareImageType()),
          tone (juce::Image::ARGB, kDisplayW, kDisplayH, true, juce::SoftwareImageType())
    {
    }

    static juce::String noteName (int note)
    {
        if (note < 0 || note > 127)
            return "--";
        return juce::String (scales::kRootNames[note % 12]) + juce::String (note / 12 - 2);   // 60 = C3
    }

    void render (const DisplayState& s, Display& out)
    {
        ink.clear (ink.getBounds());
        tone.clear (tone.getBounds(), juce::Colours::black);
        {
            juce::Graphics gi (ink), gt (tone);
            drawHeader (gi, s);
            if (s.mode == 2)      drawChords (gi, gt, s);
            else if (s.mode == 1) drawKeys (gi, gt, s);
            else                  drawDrums (gi, gt, s);
        }
        convert (out);
    }

    // Specchio a schermo: pixel accesi chiari su fondo scuro, con la griglia dell'LCD
    static void paintPreview (juce::Graphics& g, juce::Rectangle<float> area, const Display& d)
    {
        const juce::Colour glass (0xff0d1418), off (0xff16222a), on (0xffdcecff);
        g.setColour (glass);
        g.fillRoundedRectangle (area, 6.0f);
        const float cell = juce::jmin (area.getWidth() / kDisplayW, area.getHeight() / kDisplayH);
        const auto lcd = juce::Rectangle<float> (cell * kDisplayW, cell * kDisplayH).withCentre (area.getCentre());
        const float dot = cell > 2.5f ? cell - 0.6f : cell;
        const auto& fb = d.data();
        for (int y = 0; y < kDisplayH; ++y)
            for (int x = 0; x < kDisplayW; ++x)
            {
                const bool lit = (fb[size_t ((y >> 3) * kDisplayW + x)] >> (y & 7)) & 1;
                g.setColour (lit ? on : off);
                g.fillRect (lcd.getX() + float (x) * cell, lcd.getY() + float (y) * cell, dot, dot);
            }
    }

    static constexpr const char* kTitle = "BARCA MASCHINE";
    static constexpr double kSplashMs = 2600.0;

    // Animazione d'avvio: la barca attraversa lo schermo sulle onde, il titolo resta al centro
    void renderSplash (double ms, Display& out)
    {
        ink.clear (ink.getBounds());
        tone.clear (tone.getBounds(), juce::Colours::black);
        {
            juce::Graphics gi (ink), gt (tone);
            const float t = float (juce::jlimit (0.0, 1.0, ms / kSplashMs));

            // Cielo: sfumatura leggera dall'alto (dithering)
            gt.setGradientFill (juce::ColourGradient (juce::Colour::greyLevel (0.22f), 0.0f, 0.0f,
                                                      juce::Colours::black, 0.0f, 30.0f, false));
            gt.fillRect (0, 0, kDisplayW, 30);

            const auto ft = font (17.0f, true);
            const float tw = width (ft, kTitle);
            text (gi, kTitle, (kDisplayW - tw) * 0.5f, 22.0f, ft);

            // Onde: due sinusoidi che scorrono, mare sfumato sotto
            const float phase = float (ms) * 0.006f;
            auto waveY = [phase] (float x, float amp, float k, float off) { return off + amp * std::sin (x * k + phase); };
            juce::Path sea;
            sea.startNewSubPath (0.0f, waveY (0.0f, 1.6f, 0.18f, 50.0f));
            for (int x = 1; x <= kDisplayW; ++x)
                sea.lineTo (float (x), waveY (float (x), 1.6f, 0.18f, 50.0f));
            juce::Path fill (sea);
            fill.lineTo (float (kDisplayW), float (kDisplayH));
            fill.lineTo (0.0f, float (kDisplayH));
            fill.closeSubPath();
            gt.setGradientFill (juce::ColourGradient (juce::Colour::greyLevel (0.55f), 0.0f, 50.0f,
                                                      juce::Colour::greyLevel (0.12f), 0.0f, 64.0f, false));
            gt.fillPath (fill);
            gi.setColour (juce::Colours::white);
            gi.strokePath (sea, juce::PathStrokeType (1.0f));

            // Barca: da fuori a sinistra a fuori a destra, dondola sull'onda
            const float bx = -30.0f + t * (kDisplayW + 60.0f);
            const float by = waveY (bx, 1.6f, 0.18f, 50.0f);
            const float tilt = 0.12f * std::cos (bx * 0.18f + phase);
            juce::Path boat;
            boat.startNewSubPath (-14.0f, -4.0f);   // scafo
            boat.lineTo (14.0f, -4.0f);
            boat.lineTo (9.0f, 2.0f);
            boat.lineTo (-10.0f, 2.0f);
            boat.closeSubPath();
            boat.addRectangle (-0.5f, -24.0f, 1.5f, 20.0f);   // albero
            juce::Path sail;
            sail.addTriangle (1.5f, -23.0f, 1.5f, -6.0f, 12.0f, -6.0f);   // randa
            sail.addTriangle (-1.0f, -20.0f, -1.0f, -6.0f, -10.0f, -6.0f);   // fiocco
            const auto xf = juce::AffineTransform::rotation (tilt).translated (bx, by);
            gi.setColour (juce::Colours::black);   // la barca copre onde e titolo
            gi.strokePath (boat, juce::PathStrokeType (2.0f), xf);
            gi.strokePath (sail, juce::PathStrokeType (2.0f), xf);
            gi.fillPath (sail, xf);
            gi.setColour (juce::Colours::white);
            gi.fillPath (boat, xf);
            gi.fillPath (sail, xf);
            gi.setColour (juce::Colours::black);   // cucitura della vela
            gi.drawLine ({ juce::Point<float> (4.0f, -16.0f).transformedBy (xf), juce::Point<float> (7.0f, -9.0f).transformedBy (xf) }, 1.0f);
        }
        convert (out);
    }

private:
    juce::Image ink, tone;

    // DIN Condensed su macOS (tratto uniforme: regge bene la soglia a 1 bit); altrove la sans di sistema, stretta
    static juce::Font font (float size, bool heavy = false)
    {
        static const bool haveDin = juce::Font::findAllTypefaceNames().contains ("DIN Condensed");
        if (haveDin)
            return juce::Font (juce::FontOptions ("DIN Condensed", "Bold", size * 1.12f));
        return juce::Font (juce::FontOptions (size, heavy ? juce::Font::bold : juce::Font::plain)).withHorizontalScale (0.82f);
    }

    static float width (const juce::Font& f, const juce::String& t)
    {
        return juce::GlyphArrangement::getStringWidth (f, t);
    }

    // Testo con la riga di base in y (per allineare dimensioni diverse)
    static float text (juce::Graphics& g, const juce::String& t, float x, float baseline, const juce::Font& f,
                       juce::Colour c = juce::Colours::white)
    {
        g.setColour (c);
        g.setFont (f);
        g.drawSingleLineText (t, juce::roundToInt (x), juce::roundToInt (baseline));
        return x + width (f, t);
    }

    // Etichette piccole: font a pixel da 5 px (sotto i ~12 px un font vettoriale a 1 bit si sfalda)
    static int pixelText (juce::Graphics& g, const juce::String& t, int x, int top, juce::Colour c = juce::Colours::white)
    {
        g.setColour (c);
        for (auto p = t.getCharPointer(); ! p.isEmpty(); )
        {
            const auto ch = p.getAndAdvance();
            const Glyph& gl = kFont5[(ch >= 32 && ch < 128) ? ch - 32 : '?' - 32];
            for (int col = 0; col < gl.width; ++col)
                for (int row = 0; row < 5; ++row)
                    if ((gl.cols[col] >> row) & 1)
                        g.fillRect (x + col, top + row, 1, 1);
            x += gl.width + 1;
        }
        return x;
    }

    static int pixelWidth (const juce::String& t) { return Display::textWidth (t.toRawUTF8()); }

    // Il testo più grande possibile (fino a maxSize) che sta in maxW
    static juce::Font fitted (const juce::String& t, float maxSize, float maxW)
    {
        auto f = font (maxSize, true);
        for (float sz = maxSize; sz > 12.0f && width (f, t) > maxW; sz -= 1.0f)
            f = font (sz - 1.0f, true);
        return f;
    }

    static juce::String curveName (int c)
    {
        static const char* n[] { "SOFT", "LIN", "HARD", "FIX" };
        return n[juce::jlimit (0, 3, c)];
    }

    void drawHeader (juce::Graphics& g, const DisplayState& s)
    {
        static const char* modes[] { "DRUMS", "KEYS", "CHORDS" };
        text (g, kTitle, 0.0f, 10.0f, font (13.0f, true));

        // Modo in negativo, a destra
        const juce::String m (modes[juce::jlimit (0, 2, s.mode)]);
        const int w = pixelWidth (m) + 6;
        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (float (kDisplayW - w), 1.0f, float (w), 9.0f, 1.5f);
        pixelText (g, m, kDisplayW - w + 3, 3, juce::Colours::black);

        // Riga del fondo, tratteggiata
        for (int x = 0; x < kDisplayW; x += 2)
            g.fillRect (x, 52, 1, 1);
    }

    void footer (juce::Graphics& g, const juce::String& t)
    {
        pixelText (g, t, 0, 57);
    }

    // Barra della velocity: contorno netto, riempimento sfumato (dithering) fino al valore
    void velocityBar (juce::Graphics& gi, juce::Graphics& gt, juce::Rectangle<int> r, int vel)
    {
        gi.setColour (juce::Colours::white);
        gi.drawRect (r, 1);
        const auto inner = r.reduced (2);
        const int w = inner.getWidth() * juce::jlimit (0, 127, vel) / 127;
        if (w > 0)
        {
            gt.setGradientFill (juce::ColourGradient (juce::Colour::greyLevel (0.25f), float (inner.getX()), 0.0f,
                                                      juce::Colours::white, float (inner.getRight()), 0.0f, false));
            gt.fillRect (inner.withWidth (w));
            gi.fillRect (inner.getX() + w - 1, inner.getY(), 1, inner.getHeight());   // bordo netto sul valore
        }
    }

    // Mappa 4x4 dei pad (pad 1 in basso a sinistra): suonati pieni, ultimo a mezzo tono
    void padMap (juce::Graphics& gi, juce::Graphics& gt, int x0, int y0, int cell, const DisplayState& s, bool chordRows)
    {
        const int gap = 2;
        for (int p = 0; p < 16; ++p)
        {
            const int col = p % 4, row = 3 - p / 4;
            const juce::Rectangle<int> r (x0 + col * (cell + gap), y0 + row * (cell + gap), cell, cell);
            gi.setColour (juce::Colours::white);
            if ((s.held >> p) & 1)
                gi.fillRect (r);
            else
            {
                gi.drawRect (r, 1);
                if (p == s.lastPad)
                {
                    gt.setColour (juce::Colour::greyLevel (0.5f));
                    gt.fillRect (r.reduced (1));
                }
                else if (chordRows && p >= 8)   // fila delle settime: puntino al centro
                    gi.fillRect (r.getCentreX() - 1, r.getCentreY() - 1, 2, 2);
            }
        }
    }

    void drawDrums (juce::Graphics& gi, juce::Graphics& gt, const DisplayState& s)
    {
        const auto big = fitted (noteName (s.lastNote), 28.0f, 62.0f);
        const float end = text (gi, noteName (s.lastNote), 0.0f, 38.0f, big);
        if (s.lastPad >= 0)
        {
            pixelText (gi, "PAD", juce::roundToInt (end) + 4, 25);
            text (gi, juce::String (s.lastPad + 1), end + 3.0f, 38.0f, font (14.0f, true));
        }
        velocityBar (gi, gt, { 0, 42, 70, 7 }, s.lastVelocity);
        const juce::String v (s.lastNote >= 0 ? juce::String (s.lastVelocity) : juce::String ("--"));
        pixelText (gi, v, 88 - pixelWidth (v), 43);

        padMap (gi, gt, 93, 14, 7, s, false);
        footer (gi, noteName (s.lowNote) + juce::String ("-") + noteName (s.highNote)
                    + "  " + curveName (s.curve) + "  AT " + (s.aftertouch ? "ON" : "OFF") + "  CH " + juce::String (s.channel));
    }

    void drawKeys (juce::Graphics& gi, juce::Graphics& gt, const DisplayState& s)
    {
        const auto big = fitted (noteName (s.lastNote), 28.0f, 62.0f);
        text (gi, noteName (s.lastNote), 0.0f, 38.0f, big);

        const juce::String rootName (scales::kRootNames[scales::mod (s.root, 12)]);
        const juce::String sc = s.harmony ? rootName + " " + scales::get (s.scale).shortName + " ARMONIA"
                                          : rootName + " " + scales::get (s.scale).name;
        pixelText (gi, (pixelWidth (sc) <= 82 ? sc : rootName + " " + scales::get (s.scale).shortName + (s.harmony ? " ARM" : "")).toUpperCase(), 0, 43);

        // Ottava di pianoforte: tasti della scala marcati, ultima nota piena
        const int x0 = 86, y0 = 15, kw = 6, kh = 32;
        static const int whitePc[] { 0, 2, 4, 5, 7, 9, 11 };
        static const int blackPc[] { 1, 3, -1, 6, 8, 10 };
        const int lastPc = s.lastNote >= 0 ? s.lastNote % 12 : -1;
        gi.setColour (juce::Colours::white);
        for (int k = 0; k < 7; ++k)
        {
            const juce::Rectangle<int> r (x0 + k * kw, y0, kw + 1, kh);
            const int pc = whitePc[k];
            if (pc == lastPc)
                gi.fillRect (r);
            else
            {
                gi.drawRect (r, 1);
                if (s.inKey (pc))
                    gi.fillRect (r.getX() + 2, r.getBottom() - 5, kw - 3, 3);
            }
        }
        for (int k = 0; k < 6; ++k)
        {
            const int pc = blackPc[k];
            if (pc < 0)
                continue;
            const juce::Rectangle<int> r (x0 + k * kw + 4, y0, 5, kh * 6 / 10);
            gi.setColour (juce::Colours::black);   // copre i bordi dei tasti bianchi
            gi.fillRect (r);
            gi.setColour (juce::Colours::white);
            if (pc == lastPc || s.inKey (pc))
            {
                gi.fillRect (r.reduced (pc == lastPc ? 0 : 1, 0));
                if (pc != lastPc)
                {
                    gi.setColour (juce::Colours::black);
                    gi.fillRect (r.reduced (2, 0).withTrimmedTop (2).withTrimmedBottom (2));
                }
            }
            else
            {
                gi.drawRect (r, 1);
                gt.setColour (juce::Colour::greyLevel (0.3f));
                gt.fillRect (r.reduced (1));
                gi.setColour (juce::Colours::transparentBlack);
            }
        }
        footer (gi, juce::String ("PAD ") + noteName (s.lowNote) + juce::String ("-")
                    + noteName (s.highNote) + "    CH " + juce::String (s.channel));
    }

    void drawChords (juce::Graphics& gi, juce::Graphics& gt, const DisplayState& s)
    {
        juce::String name ("--"), roman;
        if (s.lastPad >= 0)
        {
            name = scales::chordName (s.scale, s.root, s.lastPad);
            roman = juce::CharPointer_UTF8 (scales::chordRoman (s.scale, s.lastPad).c_str());
        }
        text (gi, name, 0.0f, 35.0f, fitted (name, 26.0f, 88.0f));
        if (roman.isNotEmpty())
        {
            // Grado in una cornice, come un'etichetta
            const auto f = font (16.0f, true);
            const float w = width (f, roman) + 7.0f;
            gi.setColour (juce::Colours::white);
            gi.drawRoundedRectangle (0.5f, 37.5f, w, 14.0f, 2.0f, 1.0f);
            text (gi, roman, 4.0f, 50.0f, f);
            pixelText (gi, "PAD " + juce::String (s.lastPad + 1), juce::roundToInt (w) + 4, 43);
        }
        padMap (gi, gt, 93, 14, 7, s, true);
        footer (gi, juce::String (scales::kRootNames[scales::mod (s.root, 12)]) + " " + scales::get (s.scale).shortName
                    + "  TRIADI 1-7  +7 9-15");
    }

    void convert (Display& out) const
    {
        static constexpr uint8_t bayer[4][4] { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
        const juce::Image::BitmapData bi (ink, juce::Image::BitmapData::readOnly);
        const juce::Image::BitmapData bt (tone, juce::Image::BitmapData::readOnly);
        out.clear();
        for (int y = 0; y < kDisplayH; ++y)
            for (int x = 0; x < kDisplayW; ++x)
            {
                const auto* pi = reinterpret_cast<const juce::PixelARGB*> (bi.getPixelPointer (x, y));
                const int a = pi->getAlpha();
                bool lit;
                if (a >= 110)   // coperto dall'ink: soglia sul colore (premoltiplicato)
                    lit = pi->getRed() * 2 > a;
                else            // area sfumata: dithering ordinato
                {
                    const auto* pt = reinterpret_cast<const juce::PixelARGB*> (bt.getPixelPointer (x, y));
                    lit = pt->getRed() > bayer[y & 3][x & 3] * 16 + 8;
                }
                if (lit)
                    out.pixel (x, y, true);
            }
    }
};
} // namespace mikro
