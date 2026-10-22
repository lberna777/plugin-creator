#include "Views.h"

namespace
{
    // dimensione font come frazione dell'altezza della finestra (layout.json → fonts/…)
    float fontSize (const juce::Component& c, const Layout& layout, const char* which)
    {
        const auto* top = c.getTopLevelComponent();
        const float h = (float) (top != nullptr ? top->getHeight() : c.getHeight());
        return juce::jmax (9.0f, layout.number (juce::String ("fonts/") + which) * h);
    }

    juce::String num (double v, int decimals, const char* unit = "")
    {
        if (v <= mc::Snapshot::silence || ! std::isfinite (v)) return juce::String::fromUTF8 ("—");
        auto s = juce::String (v, decimals);
        if (v < 0) s = juce::String::fromUTF8 ("\xE2\x88\x92") + s.substring (1);   // segno meno vero
        return unit[0] != 0 ? s + " " + unit : s;
    }

    bool hasLoudness (const mc::Snapshot& s) { return s.integrated > mc::Snapshot::silence; }
}

//==============================================================================
void SpectrumView::setData (const mc::Snapshot& m, const mc::Snapshot* r)
{
    mix = m;
    hasRef = r != nullptr;
    if (hasRef) ref = *r;
    repaint();
}

float SpectrumView::xFor (double hz, juce::Rectangle<float> area) const
{
    const double fMin = layout.number ("spectrumView/fMin"), fMax = layout.number ("spectrumView/fMax");
    const double t = std::log (hz / fMin) / std::log (fMax / fMin);
    return area.getX() + (float) t * area.getWidth();
}

void SpectrumView::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    auto bounds = getLocalBounds().toFloat();
    bounds.reduce (layout.number ("spectrumView/pad") * bounds.getWidth(), layout.number ("spectrumView/pad") * bounds.getWidth());
    const float labelH = layout.number ("spectrumView/labelH") * bounds.getHeight();
    auto labels = bounds.removeFromBottom (labelH);
    auto diffArea = bounds.removeFromBottom (layout.number ("spectrumView/diffShare") * bounds.getHeight());
    auto curveArea = bounds;

    const float topDb = layout.number ("spectrumView/topDb"), bottomDb = layout.number ("spectrumView/bottomDb");
    const float stepDb = layout.number ("spectrumView/gridStepDb");
    const float diffRange = layout.number ("spectrumView/diffRangeDb");
    const float small = fontSize (*this, layout, "small");
    auto yFor = [&] (double db) {
        return juce::jmap ((float) juce::jlimit ((double) bottomDb, (double) topDb, db), bottomDb, topDb,
                           curveArea.getBottom(), curveArea.getY());
    };
    auto yDiff = [&] (double d) {
        return juce::jmap ((float) juce::jlimit ((double) -diffRange, (double) diffRange, d), -diffRange, diffRange,
                           diffArea.getBottom(), diffArea.getY());
    };

    // griglia
    g.setFont (small);
    for (float db = topDb; db >= bottomDb; db -= stepDb)
    {
        g.setColour (Theme::grid);
        g.drawHorizontalLine ((int) yFor (db), curveArea.getX(), curveArea.getRight());
    }
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = xFor (hz, curveArea);
        g.setColour (Theme::grid);
        g.drawVerticalLine ((int) x, curveArea.getY(), diffArea.getBottom());
        g.setColour (Theme::dim);
        g.drawText (hz >= 1000 ? juce::String (hz / 1000, 0) + "k" : juce::String (hz, 0),
                    juce::Rectangle<float> (x + 2, curveArea.getY(), 40, small * 1.3f), juce::Justification::topLeft);
    }
    g.setColour (Theme::grid.brighter (0.3f));
    g.drawHorizontalLine ((int) yDiff (0), diffArea.getX(), diffArea.getRight());
    g.setColour (Theme::dim);
    g.drawText ("differenza  +" + juce::String (diffRange, 0) + " / " + num (-diffRange, 0, "dB"),
                diffArea.withHeight (small * 1.3f).translated (4, 0), juce::Justification::topLeft);

    // zone in parole
    for (const auto& z : mc::zones)
    {
        const float x0 = xFor (z.lo, curveArea), x1 = xFor (z.hi, curveArea);
        g.setColour (Theme::grid);
        g.drawVerticalLine ((int) x0, labels.getY(), labels.getBottom());
        g.setColour (Theme::dim);
        g.drawFittedText (z.name, juce::Rectangle<float> (x0, labels.getY(), x1 - x0, labels.getHeight()).toNearestInt(),
                          juce::Justification::centred, 1);
    }

    auto curve = [&] (const mc::Snapshot& s, juce::Colour c)
    {
        if (! hasLoudness (s)) return;
        juce::Path p;
        bool started = false;
        for (int b = 0; b < mc::SpectrumAnalyzer::numBands; ++b)
        {
            const double v = s.bandDb[(size_t) b];
            if (v <= mc::Snapshot::silence) continue;
            const float x = xFor (mc::SpectrumAnalyzer::bandCentre (b), curveArea);
            const float y = yFor (v - s.integrated);
            if (! started) { p.startNewSubPath (x, y); started = true; }
            else p.lineTo (x, y);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (layout.number ("spectrumView/curveW"), juce::PathStrokeType::curved));
    };

    if (hasRef) curve (ref, Theme::ref);
    curve (mix, Theme::mix);

    // differenza (mix − riferimento) a parità di loudness
    if (hasRef && hasLoudness (mix) && hasLoudness (ref))
    {
        juce::Path fill, line;
        const float y0 = yDiff (0);
        fill.startNewSubPath (xFor (mc::SpectrumAnalyzer::bandCentre (0), curveArea), y0);
        bool started = false;
        for (int b = 0; b < mc::SpectrumAnalyzer::numBands; ++b)
        {
            const double m = mix.bandDb[(size_t) b], r = ref.bandDb[(size_t) b];
            if (m <= mc::Snapshot::silence || r <= mc::Snapshot::silence) continue;
            const double d = (m - mix.integrated) - (r - ref.integrated);
            const float x = xFor (mc::SpectrumAnalyzer::bandCentre (b), curveArea), y = yDiff (d);
            fill.lineTo (x, y);
            if (! started) { line.startNewSubPath (x, y); started = true; }
            else line.lineTo (x, y);
        }
        fill.lineTo (xFor (mc::SpectrumAnalyzer::bandCentre (mc::SpectrumAnalyzer::numBands - 1), curveArea), y0);
        fill.closeSubPath();
        g.setColour (Theme::text.withAlpha (0.15f));
        g.fillPath (fill);
        g.setColour (Theme::text);
        g.strokePath (line, juce::PathStrokeType (1.5f));
    }

    // legenda / stato
    g.setFont (fontSize (*this, layout, "body"));
    auto legend = curveArea.reduced (6).removeFromTop (fontSize (*this, layout, "body") * 1.4f).removeFromRight (curveArea.getWidth() * 0.5f);
    g.setColour (Theme::mix);
    g.drawText (juce::String::fromUTF8 ("— tuo mix"), legend.removeFromLeft (legend.getWidth() * 0.5f), juce::Justification::centredRight);
    g.setColour (Theme::ref);
    g.drawText (juce::String::fromUTF8 ("— riferimento"), legend, juce::Justification::centredRight);

    if (! hasLoudness (mix))
    {
        g.setColour (Theme::dim);
        g.drawText ("In attesa del tuo mix: fai suonare il pezzo", curveArea, juce::Justification::centred);
    }
}

//==============================================================================
void MetersView::setData (const mc::Snapshot& m, const mc::Snapshot* r)
{
    mix = m;
    hasRef = r != nullptr;
    if (hasRef) ref = *r;
    repaint();
}

void MetersView::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    auto area = getLocalBounds().toFloat();
    area.reduce (layout.number ("metersView/pad") * area.getWidth(), layout.number ("metersView/pad") * area.getWidth());
    const float rowH = layout.number ("metersView/rowH") * (float) getHeight();
    const float gap  = layout.number ("metersView/sectionGap") * (float) getHeight();
    const float xMix = area.getX() + layout.number ("metersView/colMix") * area.getWidth();
    const float xRef = area.getX() + layout.number ("metersView/colRef") * area.getWidth();
    const float body = fontSize (*this, layout, "body"), small = fontSize (*this, layout, "small");
    float y = area.getY();

    auto row = [&] (const juce::String& name, const juce::String& a, const juce::String& b,
                    juce::Colour ca = Theme::text, bool header = false)
    {
        g.setFont (header ? juce::FontOptions (small).withStyle ("Bold") : juce::FontOptions (body));
        g.setColour (header ? Theme::dim : Theme::text);
        g.drawFittedText (name, juce::Rectangle<float> (area.getX(), y, xMix - area.getX() - 4, rowH).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.75f);
        g.setColour (header ? Theme::mix : ca);
        g.drawText (a, juce::Rectangle<float> (xMix, y, xRef - xMix, rowH), juce::Justification::centredLeft);
        g.setColour (header ? Theme::ref : Theme::text);
        g.drawText (b, juce::Rectangle<float> (xRef, y, area.getRight() - xRef, rowH), juce::Justification::centredLeft);
        y += rowH;
    };
    const auto R = [&] (double v, int d, const char* u) { return hasRef ? num (v, d, u) : juce::String::fromUTF8 ("—"); };

    // unità nel nome della riga, non nelle celle: i numeri restano leggibili anche a finestra piccola
    const auto none = juce::String::fromUTF8 ("—");
    row ("LOUDNESS", "TUO MIX", "RIFERIM.", Theme::text, true);
    row ("Integrated (LUFS)", num (mix.integrated, 1), R (ref.integrated, 1, ""));
    row ("Short-term (LUFS)", num (mix.shortTerm, 1), R (ref.shortTerm, 1, ""));
    row ("True peak (dBTP)", num (mix.truePeakDb, 1), R (ref.truePeakDb, 1, ""),
         mix.truePeakDb > 0.0 ? Theme::warn : Theme::text);
    row ("Dinamica PLR (LU)", hasLoudness (mix) ? num (mix.plr, 1) : none,
         hasRef && hasLoudness (ref) ? num (ref.plr, 1) : none);
    row ("Crest factor (dB)", hasLoudness (mix) ? num (mix.crestDb, 1) : none,
         hasRef && hasLoudness (ref) ? num (ref.crestDb, 1) : none);

    y += gap;
    row ("STEREO", "TUO MIX", "RIFERIM.", Theme::text, true);
    auto stereoCell = [&] (const mc::Snapshot& s, bool available, size_t gi, bool asCorrelation) -> juce::String {
        if (! available || ! hasLoudness (s)) return juce::String::fromUTF8 ("—");
        if (! s.stereo) return "mono";
        if (asCorrelation) return juce::String (s.correlation[gi], 2);
        // larghezza = quota di energia Side sul totale: 0 % mono, 50 % lati quanto il centro
        const double r = std::pow (10.0, s.sideToMidDb[gi] / 10.0);
        return juce::String (juce::roundToInt (100.0 * r / (1.0 + r))) + " %";
    };
    // bassi: conta che siano mono → correlazione; medi e alti: conta quanto sono larghi → %
    const bool bassWarn = mix.stereo && hasLoudness (mix) && mix.correlation[0] < 0.9;
    row ("Bassi < 120 Hz (corr.)", stereoCell (mix, true, 0, true), stereoCell (ref, hasRef, 0, true),
         bassWarn ? Theme::warn : Theme::text);
    row ("Larghezza medi", stereoCell (mix, true, 1, false), stereoCell (ref, hasRef, 1, false));
    row ("Larghezza alti", stereoCell (mix, true, 2, false), stereoCell (ref, hasRef, 2, false));

    y += gap;
    g.setFont (small);
    g.setColour (Theme::dim);
    const auto secs = hasLoudness (mix) ? juce::String (mix.seconds, 0) + " s di audio misurati" : juce::String ("nessun audio misurato");
    g.drawText (secs, juce::Rectangle<float> (area.getX(), y, area.getWidth(), rowH), juce::Justification::centredLeft);
}

//==============================================================================
void FindingsView::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    auto area = getLocalBounds().toFloat();
    area.reduce (layout.number ("findingsView/pad") * area.getWidth(), layout.number ("findingsView/pad") * area.getHeight());
    const float body = fontSize (*this, layout, "body"), small = fontSize (*this, layout, "small");

    g.setColour (Theme::dim);
    g.setFont (juce::FontOptions (small).withStyle ("Bold"));
    g.drawText ("COSA STA SUCCEDENDO", area.removeFromTop (layout.number ("findingsView/titleH") * (float) getHeight()),
                juce::Justification::centredLeft);

    const float gap = layout.number ("findingsView/gap") * (float) getHeight();
    for (size_t i = 0; i < findings.size() && area.getHeight() > 0; ++i)
    {
        juce::AttributedString s;
        s.append (juce::String::fromUTF8 ("•  "), juce::FontOptions (body), i == 0 ? Theme::ref : Theme::dim);
        s.append (juce::String::fromUTF8 (findings[i].text.c_str()), juce::FontOptions (body),
                  i == 0 ? Theme::text : Theme::text.withAlpha (0.85f));
        s.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout tl;
        tl.createLayout (s, area.getWidth());
        tl.draw (g, area.removeFromTop (tl.getHeight()));
        area.removeFromTop (gap);
    }
}
