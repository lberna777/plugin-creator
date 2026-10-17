#include "MainComponent.h"

namespace
{
namespace Col
{
const juce::Colour bg (0xff16171a), panel (0xff222428), text (0xffd8d8d8), dim (0xff8a8d93),
                   pad (0xff2e3036), hit (0xfff2f2f2), drum (0xffe0782a), keys (0xff2a7fe0), ok (0xff4cc26b),
                   warn (0xffe0b42a);
}

constexpr int kMargin = 16, kRowH = 26, kLabelW = 118, kGap = 8;
}

// ---------------------------------------------------------------- griglia dei pad

class MainComponent::PadGrid final : public juce::Component
{
public:
    explicit PadGrid (Bridge& b) : bridge (b) {}

    void paint (juce::Graphics& g) override
    {
        const bool keys = bridge.usesKeyBase();
        const bool chords = bridge.chordMode();
        const float cell = float (getWidth()) / 4.0f, gap = cell * 0.08f;

        for (int p = 0; p < 16; ++p)
        {
            const int col = p % 4, row = 3 - p / 4;   // pad 1 in basso a sinistra
            auto r = juce::Rectangle<float> (col * cell, row * cell, cell, cell).reduced (gap * 0.5f);

            const int vel = bridge.mirror.velocity[p].load();
            const float press = juce::jlimit (0.0f, 1.0f, bridge.mirror.pressure[p].load() / 3200.0f);
            const int note = bridge.noteForPad (p);

            g.setColour (Col::pad);
            g.fillRoundedRectangle (r, 5.0f);

            if (press > 0.02f)   // pressione grezza, anche sotto soglia: utile per tarare
            {
                auto fill = r.withTrimmedTop (r.getHeight() * (1.0f - press));
                g.setColour ((keys ? Col::keys : Col::drum).withAlpha (0.35f));
                g.fillRoundedRectangle (fill, 5.0f);
            }
            if (vel > 0)
            {
                g.setColour (Col::hit.withAlpha (0.25f + 0.6f * vel / 127.0f));
                g.fillRoundedRectangle (r, 5.0f);
            }
            else if (bridge.mirror.feedback[p].load() > 0)
            {
                g.setColour (Col::ok.withAlpha (0.5f));
                g.drawRoundedRectangle (r.reduced (1.5f), 5.0f, 3.0f);
            }

            g.setColour (vel > 0 ? Col::bg : Col::dim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (p + 1), r.reduced (6.0f), juce::Justification::topLeft);
            g.setColour (vel > 0 ? Col::bg : Col::text);
            if (chords)   // nome dell'accordo e grado: "Am" sopra, "vi" sotto
            {
                const auto label = bridge.chordLabel (p);
                g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
                g.drawText (label.upToFirstOccurrenceOf (" ", false, false), r.withTrimmedBottom (r.getHeight() * 0.25f), juce::Justification::centred);
                g.setColour (vel > 0 ? Col::bg : Col::dim);
                g.setFont (juce::FontOptions (11.5f));
                g.drawText (label.fromFirstOccurrenceOf (" ", false, false), r.withTrimmedTop (r.getHeight() * 0.45f), juce::Justification::centred);
            }
            else
            {
                g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
                g.drawText (Bridge::noteName (note), r, juce::Justification::centred);
            }
            if (vel > 0)
            {
                g.setFont (juce::FontOptions (11.0f));
                g.drawText (juce::String (vel), r.reduced (6.0f), juce::Justification::bottomRight);
            }
        }
    }

private:
    Bridge& bridge;
};

// ---------------------------------------------------------------- specchio del display

class MainComponent::DisplayMirror final : public juce::Component
{
public:
    explicit DisplayMirror (Bridge& b) : bridge (b) {}

    // true se il frame è cambiato
    bool update()
    {
        mikro::Display d;
        bridge.copyDisplay (d);
        if (! (d != shown))
            return false;
        shown = d;
        return true;
    }

    void paint (juce::Graphics& g) override
    {
        mikro::DisplayRenderer::paintPreview (g, getLocalBounds().toFloat(), shown);
    }

private:
    Bridge& bridge;
    mikro::Display shown;
};

// ---------------------------------------------------------------- finestra

MainComponent::MainComponent (Bridge& b, juce::PropertiesFile& p) : bridge (b), props (p)
{
    grid = std::make_unique<PadGrid> (bridge);
    addAndMakeVisible (*grid);
    display = std::make_unique<DisplayMirror> (bridge);
    addAndMakeVisible (*display);

    status.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (status);

    legend.setFont (juce::FontOptions (12.5f));
    legend.setColour (juce::Label::textColourId, Col::dim);
    legend.setJustificationType (juce::Justification::topLeft);
    legend.setText (juce::String ("Nel DAW scegli \"") + Bridge::kPortName + "\" come ingresso MIDI (e come uscita, se vuoi le luci).\n"
                    "Pad: note sul canale pad.  PAD MODE: batteria / tastiera in scala / accordi (pad 1-7 = gradi I-VII, 9-15 con la settima).  SHIFT + PAD MODE: armonia.  SHIFT + pad 13/14: semitono (in scala: tonalita'), 15/16: banco o ottava.\n"
                    "Tasti: CC 20-46 sul canale controlli (con SHIFT: CC 70-96).  SHIFT + pad 1-12: CC 102-113.  Encoder: CC 14 relativo (SHIFT: CC 15).",
                    juce::dontSendNotification);
    addAndMakeVisible (legend);

    mode.addItem ("Batteria (16 pad)", 1);
    mode.addItem ("Tastiera (in scala)", 2);
    mode.addItem ("Accordi (pad = grado)", 3);
    for (int r = 0; r < 12; ++r)
        root.addItem (scales::kRootNames[r], r + 1);
    for (int i = 0; i < scales::kNumScales; ++i)
        scale.addItem (scales::kScales[i].name, i + 1);
    for (int ch = 1; ch <= 16; ++ch)
    {
        padCh.addItem (juce::String (ch), ch);
        ctrlCh.addItem (juce::String (ch), ch);
    }
    curve.addItem ("Morbida", 1);
    curve.addItem ("Lineare", 2);
    curve.addItem ("Dura", 3);
    curve.addItem ("Fissa (127)", 4);

    threshold.setSliderStyle (juce::Slider::LinearHorizontal);
    threshold.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
    threshold.setRange (60.0, 1200.0, 10.0);
    crosstalk.setSliderStyle (juce::Slider::LinearHorizontal);
    crosstalk.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
    crosstalk.setRange (0.0, 80.0, 1.0);
    crosstalk.setTextValueSuffix (" %");
    crosstalk.setTooltip ("Colpi fantasma: un pad parte solo se ha almeno questa % della pressione del pad piu' premuto. 0 = spento");

    addRow (modeL, mode, "Modo");
    addRow (baseL, base, "Pad 1 suona");
    addRow (rootL, root, juce::String (juce::CharPointer_UTF8 ("Tonalit\xc3\xa0")));
    addRow (scaleL, scale, "Scala (tastiera)");
    addRow (padChL, padCh, "Canale pad");
    addRow (ctrlChL, ctrlCh, "Canale controlli");
    addRow (curveL, curve, "Velocity");
    addRow (threshL, threshold, "Soglia colpo");
    addRow (crosstalkL, crosstalk, "Anti-fantasma");
    addAndMakeVisible (harmony);
    addAndMakeVisible (aftertouch);
    addAndMakeVisible (flipped);

    load();

    mode.onChange   = [this] { bridge.settings.mode = mode.getSelectedId() - 1; save(); };
    base.onChange   = [this]
    {
        auto& target = bridge.usesKeyBase() ? bridge.settings.keyBase : bridge.settings.drumBase;
        target = base.getSelectedId() - 1;
        save();
    };
    root.onChange   = [this] { bridge.settings.root = root.getSelectedId() - 1; save(); };
    scale.onChange  = [this] { bridge.settings.scale = scale.getSelectedId() - 1; save(); };
    padCh.onChange  = [this] { bridge.settings.padChannel = padCh.getSelectedId(); save(); };
    ctrlCh.onChange = [this] { bridge.settings.ctrlChannel = ctrlCh.getSelectedId(); save(); };
    curve.onChange  = [this] { bridge.settings.curve = curve.getSelectedId() - 1; save(); };
    threshold.onValueChange = [this] { bridge.settings.threshold = int (threshold.getValue()); save(); };
    crosstalk.onValueChange = [this] { bridge.settings.crosstalk = int (crosstalk.getValue()); save(); };
    harmony.onClick    = [this] { bridge.settings.harmony = harmony.getToggleState(); save(); };
    aftertouch.onClick = [this] { bridge.settings.aftertouch = aftertouch.getToggleState(); save(); };
    flipped.onClick    = [this] { bridge.settings.flippedPads = flipped.getToggleState(); save(); };

    setSize (760, 790);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    save();
}

void MainComponent::addRow (juce::Label& label, juce::Component& c, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, Col::dim);
    addAndMakeVisible (label);
    addAndMakeVisible (c);
}

void MainComponent::fillBaseNotes()
{
    base.clear (juce::dontSendNotification);
    for (int n = 0; n <= 127 - 15; ++n)
        base.addItem (Bridge::noteName (n) + "  (" + juce::String (n) + ")", n + 1);
}

void MainComponent::load()
{
    auto& s = bridge.settings;
    s.mode        = juce::jlimit (0, Bridge::kNumModes - 1, props.getIntValue ("mode", s.mode));
    s.drumBase    = props.getIntValue ("drumBase", s.drumBase);
    s.keyBase     = props.getIntValue ("keyBase", s.keyBase);
    s.scale       = juce::jlimit (0, scales::kNumScales - 1, props.getIntValue ("scale", s.scale));
    s.root        = juce::jlimit (0, 11, props.getIntValue ("root", s.root));
    s.padChannel  = props.getIntValue ("padChannel", s.padChannel);
    s.ctrlChannel = props.getIntValue ("ctrlChannel", s.ctrlChannel);
    s.curve       = props.getIntValue ("curve", s.curve);
    s.threshold   = props.getIntValue ("threshold", s.threshold);
    s.crosstalk   = juce::jlimit (0, 80, props.getIntValue ("crosstalk", s.crosstalk));
    s.harmony     = props.getBoolValue ("harmony", s.harmony);
    s.aftertouch  = props.getBoolValue ("aftertouch", s.aftertouch);
    s.flippedPads = props.getBoolValue ("flippedPads", s.flippedPads);

    fillBaseNotes();
    mode.setSelectedId (s.mode + 1, juce::dontSendNotification);
    root.setSelectedId (s.root + 1, juce::dontSendNotification);
    scale.setSelectedId (s.scale + 1, juce::dontSendNotification);
    padCh.setSelectedId (s.padChannel, juce::dontSendNotification);
    ctrlCh.setSelectedId (s.ctrlChannel, juce::dontSendNotification);
    curve.setSelectedId (s.curve + 1, juce::dontSendNotification);
    threshold.setValue (s.threshold, juce::dontSendNotification);
    crosstalk.setValue (s.crosstalk, juce::dontSendNotification);
    harmony.setToggleState (s.harmony, juce::dontSendNotification);
    aftertouch.setToggleState (s.aftertouch, juce::dontSendNotification);
    flipped.setToggleState (s.flippedPads, juce::dontSendNotification);
}

void MainComponent::save()
{
    auto& s = bridge.settings;
    props.setValue ("mode", s.mode.load());
    props.setValue ("drumBase", s.drumBase.load());
    props.setValue ("keyBase", s.keyBase.load());
    props.setValue ("scale", s.scale.load());
    props.setValue ("root", s.root.load());
    props.setValue ("padChannel", s.padChannel.load());
    props.setValue ("ctrlChannel", s.ctrlChannel.load());
    props.setValue ("curve", s.curve.load());
    props.setValue ("threshold", s.threshold.load());
    props.setValue ("crosstalk", s.crosstalk.load());
    props.setValue ("harmony", s.harmony.load());
    props.setValue ("aftertouch", s.aftertouch.load());
    props.setValue ("flippedPads", s.flippedPads.load());
    props.saveIfNeeded();
}

void MainComponent::timerCallback()
{
    // La Mikro cambia modo e nota base da sé (PAD MODE, SHIFT + pad): la UI insegue
    const int m = bridge.settings.mode.load();
    const int b = bridge.usesKeyBase() ? bridge.settings.keyBase.load() : bridge.settings.drumBase.load();
    if (m != shownMode || (base.getSelectedId() != b + 1 && ! base.isPopupActive()))
    {
        if (m != shownMode)
            save();
        shownMode = m;
        mode.setSelectedId (m + 1, juce::dontSendNotification);
        base.setSelectedId (b + 1, juce::dontSendNotification);
    }
    if (const bool h = bridge.settings.harmony.load(); h != shownHarmony)
    {
        shownHarmony = h;
        harmony.setToggleState (h, juce::dontSendNotification);
        save();
    }
    if (const int r = bridge.settings.root.load(); r != shownRoot && ! root.isPopupActive())
    {
        if (shownRoot >= 0)
            save();
        shownRoot = r;
        root.setSelectedId (r + 1, juce::dontSendNotification);
    }

    if (! bridge.hasVirtualPorts())
    {
        status.setText ("Porta MIDI virtuale non disponibile su questo sistema", juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, Col::warn);
    }
    else switch (Bridge::Status (bridge.mirror.status.load()))
    {
        case Bridge::Status::Connected:
            status.setText ("Mikro MK2 collegata", juce::dontSendNotification);
            status.setColour (juce::Label::textColourId, Col::ok);
            break;
        case Bridge::Status::Busy:
            status.setText ("Mikro trovata ma occupata: chiudi NIHardwareAgent / Maschine", juce::dontSendNotification);
            status.setColour (juce::Label::textColourId, Col::warn);
            break;
        case Bridge::Status::Searching:
            status.setText ("In attesa della Mikro MK2 (collegala via USB)", juce::dontSendNotification);
            status.setColour (juce::Label::textColourId, Col::dim);
            break;
    }

    grid->repaint();
    if (display->update())
        display->repaint();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Col::bg);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (kMargin);
    status.setBounds (area.removeFromTop (kRowH));
    area.removeFromTop (kGap);

    legend.setBounds (area.removeFromBottom (80));
    area.removeFromBottom (kGap);

    // Colonna sinistra: specchio del display (2:1, come il 128x64) sopra la griglia dei pad
    const int side = area.getWidth() / 2;
    auto left = area.removeFromLeft (side);
    display->setBounds (left.removeFromTop (side / 2));
    left.removeFromTop (kGap * 2);
    const int gridSide = juce::jmin (side, left.getHeight());
    grid->setBounds (left.removeFromTop (gridSide).withSizeKeepingCentre (gridSide, gridSide));
    area.removeFromLeft (kMargin + kGap);

    auto row = [&area] { auto r = area.removeFromTop (kRowH); area.removeFromTop (kGap); return r; };
    juce::Label* labels[] { &modeL, &baseL, &rootL, &scaleL, &padChL, &ctrlChL, &curveL, &threshL, &crosstalkL };
    juce::Component* comps[] { &mode, &base, &root, &scale, &padCh, &ctrlCh, &curve, &threshold, &crosstalk };
    for (size_t i = 0; i < std::size (labels); ++i)
    {
        auto r = row();
        labels[i]->setBounds (r.removeFromLeft (kLabelW));
        comps[i]->setBounds (r);
    }
    harmony.setBounds (row());
    aftertouch.setBounds (row());
    flipped.setBounds (row());
}
