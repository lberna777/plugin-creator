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
        const bool keys = bridge.settings.mode.load() == int (Bridge::Mode::Keys);
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
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText (Bridge::noteName (note), r, juce::Justification::centred);
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

// ---------------------------------------------------------------- finestra

MainComponent::MainComponent (Bridge& b, juce::PropertiesFile& p) : bridge (b), props (p)
{
    grid = std::make_unique<PadGrid> (bridge);
    addAndMakeVisible (*grid);

    status.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (status);

    legend.setFont (juce::FontOptions (12.5f));
    legend.setColour (juce::Label::textColourId, Col::dim);
    legend.setJustificationType (juce::Justification::topLeft);
    legend.setText (juce::String ("Nel DAW scegli \"") + Bridge::kPortName + "\" come ingresso MIDI (e come uscita, se vuoi le luci).\n"
                    "Pad: note sul canale pad.  PAD MODE: batteria / tastiera.  SHIFT + pad 13/14: semitono, 15/16: banco o ottava.\n"
                    "Tasti: CC 20-46 sul canale controlli (con SHIFT: CC 70-96).  SHIFT + pad 1-12: CC 102-113.  Encoder: CC 14 relativo (SHIFT: CC 15).",
                    juce::dontSendNotification);
    addAndMakeVisible (legend);

    mode.addItem ("Batteria (16 pad)", 1);
    mode.addItem ("Tastiera (cromatica)", 2);
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

    addRow (modeL, mode, "Modo");
    addRow (baseL, base, "Pad 1 suona");
    addRow (padChL, padCh, "Canale pad");
    addRow (ctrlChL, ctrlCh, "Canale controlli");
    addRow (curveL, curve, "Velocity");
    addRow (threshL, threshold, "Soglia colpo");
    addAndMakeVisible (aftertouch);
    addAndMakeVisible (flipped);

    load();

    mode.onChange   = [this] { bridge.settings.mode = mode.getSelectedId() - 1; save(); };
    base.onChange   = [this]
    {
        auto& target = bridge.settings.mode.load() == int (Bridge::Mode::Keys) ? bridge.settings.keyBase : bridge.settings.drumBase;
        target = base.getSelectedId() - 1;
        save();
    };
    padCh.onChange  = [this] { bridge.settings.padChannel = padCh.getSelectedId(); save(); };
    ctrlCh.onChange = [this] { bridge.settings.ctrlChannel = ctrlCh.getSelectedId(); save(); };
    curve.onChange  = [this] { bridge.settings.curve = curve.getSelectedId() - 1; save(); };
    threshold.onValueChange = [this] { bridge.settings.threshold = int (threshold.getValue()); save(); };
    aftertouch.onClick = [this] { bridge.settings.aftertouch = aftertouch.getToggleState(); save(); };
    flipped.onClick    = [this] { bridge.settings.flippedPads = flipped.getToggleState(); save(); };

    setSize (720, 470);
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
    s.mode        = props.getIntValue ("mode", s.mode);
    s.drumBase    = props.getIntValue ("drumBase", s.drumBase);
    s.keyBase     = props.getIntValue ("keyBase", s.keyBase);
    s.padChannel  = props.getIntValue ("padChannel", s.padChannel);
    s.ctrlChannel = props.getIntValue ("ctrlChannel", s.ctrlChannel);
    s.curve       = props.getIntValue ("curve", s.curve);
    s.threshold   = props.getIntValue ("threshold", s.threshold);
    s.aftertouch  = props.getBoolValue ("aftertouch", s.aftertouch);
    s.flippedPads = props.getBoolValue ("flippedPads", s.flippedPads);

    fillBaseNotes();
    mode.setSelectedId (s.mode + 1, juce::dontSendNotification);
    padCh.setSelectedId (s.padChannel, juce::dontSendNotification);
    ctrlCh.setSelectedId (s.ctrlChannel, juce::dontSendNotification);
    curve.setSelectedId (s.curve + 1, juce::dontSendNotification);
    threshold.setValue (s.threshold, juce::dontSendNotification);
    aftertouch.setToggleState (s.aftertouch, juce::dontSendNotification);
    flipped.setToggleState (s.flippedPads, juce::dontSendNotification);
}

void MainComponent::save()
{
    auto& s = bridge.settings;
    props.setValue ("mode", s.mode.load());
    props.setValue ("drumBase", s.drumBase.load());
    props.setValue ("keyBase", s.keyBase.load());
    props.setValue ("padChannel", s.padChannel.load());
    props.setValue ("ctrlChannel", s.ctrlChannel.load());
    props.setValue ("curve", s.curve.load());
    props.setValue ("threshold", s.threshold.load());
    props.setValue ("aftertouch", s.aftertouch.load());
    props.setValue ("flippedPads", s.flippedPads.load());
    props.saveIfNeeded();
}

void MainComponent::timerCallback()
{
    // La Mikro cambia modo e nota base da sé (PAD MODE, SHIFT + pad): la UI insegue
    const int m = bridge.settings.mode.load();
    const int b = m == int (Bridge::Mode::Keys) ? bridge.settings.keyBase.load() : bridge.settings.drumBase.load();
    if (m != shownMode || (base.getSelectedId() != b + 1 && ! base.isPopupActive()))
    {
        if (m != shownMode)
            save();
        shownMode = m;
        mode.setSelectedId (m + 1, juce::dontSendNotification);
        base.setSelectedId (b + 1, juce::dontSendNotification);
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

    legend.setBounds (area.removeFromBottom (64));
    area.removeFromBottom (kGap);

    const int side = juce::jmin (area.getHeight(), area.getWidth() / 2);
    grid->setBounds (area.removeFromLeft (side).withHeight (side));
    area.removeFromLeft (kMargin + kGap);

    auto row = [&area] { auto r = area.removeFromTop (kRowH); area.removeFromTop (kGap); return r; };
    juce::Label* labels[] { &modeL, &baseL, &padChL, &ctrlChL, &curveL, &threshL };
    juce::Component* comps[] { &mode, &base, &padCh, &ctrlCh, &curve, &threshold };
    for (size_t i = 0; i < std::size (labels); ++i)
    {
        auto r = row();
        labels[i]->setBounds (r.removeFromLeft (kLabelW));
        comps[i]->setBounds (r);
    }
    aftertouch.setBounds (row());
    flipped.setBounds (row());
}
