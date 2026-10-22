#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    struct KnobDef { const char* id; const char* label; bool bipolar; };

    // Testi serigrafati. Le POSIZIONI stanno in layout.json (knobs/<id>).
    const KnobDef knobDefs[] {
        { pid::oscWave, "WAVE", false },     { pid::oscTune, "TUNE", true },
        { pid::osc2Detune, "OSC 2", true },  { pid::glide, "GLIDE", false },
        { pid::oscSub, "SUB", false },       { pid::oscNoise, "NOISE", false },
        { pid::fold, "FOLD", false },        { pid::drift, "DRIFT", false },
        { pid::cutoff, "CUTOFF", false },    { pid::reso, "RESO", false },
        { pid::filterEnv, "ENV AMT", true }, { pid::filterDrive, "DRIVE", false },
        { pid::attack, "ATTACK", false },    { pid::decay, "DECAY", false },
        { pid::sustain, "SUSTAIN", false },
        { pid::lfoRate, "RATE", false },     { pid::chaos, "CHAOS", false },
        { pid::lfoPitch, "> PITCH", false }, { pid::lfoCutoff, "> CUTOFF", false },
        { pid::lfoFold, "> FOLD", false },
        { pid::seqLength, "LENGTH", false }, { pid::seqProb, "PROB", false },
        { pid::seqMutate, "MUTATE", false }, { pid::seqRatchet, "RATCHET", false },
        { pid::seqSwing, "SWING", false },
        { pid::delayTime, "TIME", false },   { pid::delayFb, "FEEDBACK", false },
        { pid::delayMix, "DELAY", false },   { pid::width, "WIDTH", false },
        { pid::spaceSize, "SPACE", false },  { pid::spaceMix, "REVERB", false },
        { pid::volume, "VOLUME", false },
    };

    const char* sectionIds[] { "osc", "filter", "env", "mod", "seq", "fx" };

    constexpr int timerHz = 30;
    constexpr float meterFloorDb = -48.0f;
}

//==============================================================================
MacchinePiselloEditor::MacchinePiselloEditor (MacchinePiselloProcessor& p)
    : AudioProcessorEditor (&p), machineProcessor (p),
      layout (BinaryData::layout_json, BinaryData::layout_jsonSize),
      scope (p.machine)
{
    jassert (layout.isValid());
    setLookAndFeel (&lnf);

    auto setupSlider = [this] (juce::Slider& s, const juce::String& id)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s.setPopupDisplayEnabled (false, false, nullptr);
        if (auto* param = machineProcessor.apvts.getParameter (id))
            s.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        addAndMakeVisible (s);
    };

    for (const auto& def : knobDefs)
    {
        auto k = std::make_unique<Knob>();
        k->id = def.id; k->label = def.label; k->layoutGroup = "knobs";
        k->slider.getProperties().set ("bipolar", def.bipolar);
        setupSlider (k->slider, k->id);
        k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (machineProcessor.apvts, k->id, k->slider);
        knobs.push_back (std::move (k));
    }
    for (int i = 0; i < pid::numSteps; ++i)
    {
        auto k = std::make_unique<Knob>();
        k->id = pid::step (i); k->label = juce::String (i + 1); k->layoutGroup = "steps";
        k->slider.getProperties().set ("bipolar", true);
        setupSlider (k->slider, k->id);
        k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (machineProcessor.apvts, k->id, k->slider);
        knobs.push_back (std::move (k));
    }

    auto setupCombo = [this] (juce::ComboBox& box, const juce::StringArray& items)
    {
        box.addItemList (items, 1);
        addAndMakeVisible (box);
    };
    setupCombo (lfoShape, choices::lfoShapes);
    setupCombo (seqRate, choices::seqRates);
    setupCombo (seqScale, choices::scales);
    lfoShapeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (machineProcessor.apvts, pid::lfoShape, lfoShape);
    seqRateAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (machineProcessor.apvts, pid::seqRate, seqRate);
    seqScaleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (machineProcessor.apvts, pid::seqScale, seqScale);

    addAndMakeVisible (drone);
    addAndMakeVisible (seqOn);
    droneAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (machineProcessor.apvts, pid::drone, drone);
    seqOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (machineProcessor.apvts, pid::seqOn, seqOn);

    addAndMakeVisible (scope);

    const int baseW = (int) layout.number ("window/w");
    const int baseH = (int) layout.number ("window/h");
    setResizable (true, true);
    setResizeLimits (juce::roundToInt (baseW * layout.number ("window/minScale")),
                     juce::roundToInt (baseH * layout.number ("window/minScale")),
                     juce::roundToInt (baseW * layout.number ("window/maxScale")),
                     juce::roundToInt (baseH * layout.number ("window/maxScale")));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseW / (double) baseH);
    setSize (baseW, baseH);

    startTimerHz (timerHz);
}

MacchinePiselloEditor::~MacchinePiselloEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void MacchinePiselloEditor::resized()
{
    const auto area = getLocalBounds();

    for (auto& k : knobs)
        k->slider.setBounds (layout.rect (k->layoutGroup + "/" + k->id, area));

    lfoShape.setBounds (layout.rect ("combos/lfoShape", area));
    seqRate.setBounds  (layout.rect ("combos/seqRate", area));
    seqScale.setBounds (layout.rect ("combos/seqScale", area));
    drone.setBounds    (layout.rect ("toggles/drone", area));
    seqOn.setBounds    (layout.rect ("toggles/seqOn", area));
    scope.setBounds    (layout.rect ("header/scope", area));

    renderPanel();
}

// Tutto ciò che non cambia mai viene disegnato una volta sola in un'immagine (per dimensione).
void MacchinePiselloEditor::renderPanel()
{
    const auto area = getLocalBounds();
    if (area.isEmpty()) return;

    const float scale = juce::Desktop::getInstance().getGlobalScaleFactor() * 2.0f;
    panelCache = juce::Image (juce::Image::RGB, juce::roundToInt ((float) area.getWidth() * scale),
                              juce::roundToInt ((float) area.getHeight() * scale), true);
    juce::Graphics g (panelCache);
    g.addTransform (juce::AffineTransform::scale (scale));

    const float H = (float) area.getHeight();
    auto style = [this, H] (const char* key) { return layout.number (juce::String ("style/") + key) * H; };

    // piastra
    juce::ColourGradient bg (palette::panelHi, 0.0f, 0.0f, palette::panel, 0.0f, H, false);
    g.setGradientFill (bg);
    g.fillAll();

    // micro-spazzolatura orizzontale
    juce::Random brush (7);
    for (int y = 0; y < area.getHeight(); y += 2)
    {
        g.setColour (juce::Colours::white.withAlpha (brush.nextFloat() * 0.018f));
        g.drawHorizontalLine (y, 0.0f, (float) area.getWidth());
    }

    // header
    g.setColour (palette::ink);
    g.setFont (MachineLookAndFeel::machineFont (style ("titleFont")));
    g.drawText ("MACCHINE PISELLO", layout.rect ("header/title", area), juce::Justification::bottomLeft, false);
    g.setColour (palette::inkDim);
    g.setFont (MachineLookAndFeel::machineFont (style ("taglineFont"), false));
    g.drawText ("SEMI-MODULAR AMBIENT MACHINE  -  MONO VOICE  -  8 STEP", layout.rect ("header/tagline", area),
                juce::Justification::centredLeft, false);
    g.setFont (MachineLookAndFeel::machineFont (style ("versionFont")));
    g.setColour (palette::amber);
    g.drawText ("MP-1  v" MP_VERSION_STRING, layout.rect ("header/version", area), juce::Justification::centredRight, false);

    // moduli
    const float corner = style ("cornerRadius");
    const float titleH = style ("sectionTitleH");
    const float screwR = style ("screwRadius");
    const float inset  = style ("screwInset");
    for (auto* id : sectionIds)
    {
        const auto r = layout.rect (juce::String ("sections/") + id, area).toFloat();

        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f), corner);
        g.setColour (palette::module);
        g.fillRoundedRectangle (r, corner);
        g.setColour (palette::moduleEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);

        const auto bar = r.withHeight (titleH);
        g.setColour (palette::titleBar);
        g.fillRoundedRectangle (bar.reduced (1.0f), corner);
        g.setColour (palette::amber);
        g.fillRect (bar.withTop (bar.getBottom() - 2.0f).reduced (1.0f, 0.0f));

        g.setColour (palette::ink);
        g.setFont (MachineLookAndFeel::machineFont (style ("sectionFont")));
        g.drawText (layout.text (juce::String ("sections/") + id + "/title"),
                    bar.withTrimmedLeft (inset * 2.5f).withTrimmedRight (inset * 2.5f), juce::Justification::centredLeft, false);

        for (auto corner2 : { r.getTopLeft(), r.getTopRight(), r.getBottomLeft(), r.getBottomRight() })
        {
            const auto c = juce::Point<float> (corner2.x < r.getCentreX() ? r.getX() + inset : r.getRight() - inset,
                                               corner2.y < r.getCentreY() ? r.getY() + titleH * 0.5f : r.getBottom() - inset);
            g.setColour (palette::metal.darker (0.6f));
            g.fillEllipse (juce::Rectangle<float> (screwR * 2.0f, screwR * 2.0f).withCentre (c));
            g.setColour (palette::panel);
            g.drawLine (c.x - screwR * 0.7f, c.y - screwR * 0.7f, c.x + screwR * 0.7f, c.y + screwR * 0.7f, screwR * 0.35f);
        }
    }

    // anelli-scala dei knob (tacche serigrafate)
    for (auto& k : knobs)
    {
        const auto kr = layout.rect (k->layoutGroup + "/" + k->id, area).toFloat();
        const auto c  = kr.getCentre();
        const float rOut = kr.getWidth() * 0.5f;
        g.setColour (palette::inkDim.withAlpha (0.5f));
        for (int t = 0; t <= 10; ++t)
        {
            const float a = juce::MathConstants<float>::pi * (1.25f + 1.5f * (float) t / 10.0f);
            g.drawLine ({ c.getPointOnCircumference (rOut * 1.04f, a), c.getPointOnCircumference (rOut * 1.12f, a) }, 1.0f);
        }
    }

    // separatore nel modulo sequencer
    g.setColour (palette::moduleEdge);
    g.fillRect (layout.rect ("labels/seqDivider", area));

    // etichette dei combo (fisse)
    g.setColour (palette::ink);
    g.setFont (MachineLookAndFeel::machineFont (style ("labelFont")));
    const float gap = style ("labelGap"), lh = style ("labelH");
    for (auto [key, text] : { std::pair<const char*, const char*> { "combos/lfoShape", "SHAPE" },
                              { "combos/seqRate", "RATE" }, { "combos/seqScale", "SCALE" },
                              { "toggles/drone", "VCA MODE" }, { "toggles/seqOn", "CLOCK" } })
    {
        const auto r = layout.rect (key, area).toFloat();
        g.drawText (text, juce::Rectangle<float> (r.getX() - r.getWidth(), r.getBottom() + gap, r.getWidth() * 3.0f, lh),
                    juce::Justification::centredTop, false);
    }

    const auto mr = layout.rect ("meter", area).toFloat();
    g.drawText ("OUT", juce::Rectangle<float> (mr.getX() - mr.getWidth() * 2.0f, mr.getBottom() + gap, mr.getWidth() * 5.0f, lh),
                juce::Justification::centredTop, false);
}

//==============================================================================
void MacchinePiselloEditor::paint (juce::Graphics& g)
{
    if (panelCache.isValid())
        g.drawImage (panelCache, getLocalBounds().toFloat());

    paintLabels (g);
    paintLeds (g);
    paintMeter (g);
}

void MacchinePiselloEditor::paintLabels (juce::Graphics& g)
{
    const auto area = getLocalBounds();
    const float H = (float) area.getHeight();
    const float gap = layout.number ("style/labelGap") * H;
    const float lh  = layout.number ("style/labelH") * H;

    for (auto& k : knobs)
    {
        const auto r = k->slider.getBounds().toFloat();
        const bool hot = k->slider.isMouseOverOrDragging();
        const bool isStep = k->layoutGroup == "steps";
        const auto* param = machineProcessor.apvts.getParameter (k->id);

        g.setFont (MachineLookAndFeel::machineFont (layout.number (isStep ? "style/stepLabelFont" : "style/labelFont") * H));
        g.setColour (hot ? palette::amber : palette::ink);
        const juce::String text = hot && param != nullptr ? param->getCurrentValueAsText() : k->label;
        g.drawText (text, juce::Rectangle<float> (r.getCentreX() - r.getWidth(), r.getBottom() + gap, r.getWidth() * 2.0f, lh),
                    juce::Justification::centredTop, false);
    }
}

void MacchinePiselloEditor::paintLeds (juce::Graphics& g)
{
    const auto area   = getLocalBounds();
    const int  step   = machineProcessor.machine.uiStep.load();
    const int  length = (int) machineProcessor.apvts.getRawParameterValue (pid::seqLength)->load();

    for (int i = 0; i < pid::numSteps; ++i)
    {
        const auto r = layout.rect ("leds/step" + juce::String (i + 1), area).toFloat();
        const bool lit = i == step;
        if (lit)
        {
            g.setColour (palette::amber.withAlpha (0.3f));
            g.fillEllipse (r.expanded (r.getWidth() * 0.45f));
        }
        g.setColour (lit ? palette::amber : (i < length ? palette::amberDim : palette::screen));
        g.fillEllipse (r);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (r, 1.0f);
    }
}

void MacchinePiselloEditor::paintMeter (juce::Graphics& g)
{
    const auto r = layout.rect ("meter", getLocalBounds()).toFloat();
    g.setColour (palette::screen);
    g.fillRoundedRectangle (r, r.getWidth() * 0.2f);

    const float db   = juce::Decibels::gainToDecibels (meterLevel, meterFloorDb);
    const float norm = juce::jlimit (0.0f, 1.0f, (db - meterFloorDb) / -meterFloorDb);
    const auto  inner = r.reduced (r.getWidth() * 0.22f);
    g.setColour (db > -1.0f ? juce::Colours::red : palette::amber);
    g.fillRect (inner.withTop (inner.getBottom() - inner.getHeight() * norm));
    g.setColour (palette::moduleEdge);
    g.drawRoundedRectangle (r, r.getWidth() * 0.2f, 1.0f);
}

void MacchinePiselloEditor::timerCallback()
{
    const auto area = getLocalBounds();

    const int step = machineProcessor.machine.uiStep.load();
    if (step != lastStep)
    {
        lastStep = step;
        for (int i = 0; i < pid::numSteps; ++i)
            repaint (layout.rect ("leds/step" + juce::String (i + 1), area).expanded (8));
    }

    const float peak = machineProcessor.machine.uiPeak.load();
    const float newLevel = juce::jmax (peak, meterLevel * 0.85f);
    if (std::abs (newLevel - meterLevel) > 1.0e-4f)
    {
        meterLevel = newLevel;
        repaint (layout.rect ("meter", area));
    }

    const float H = (float) area.getHeight();
    const int gap = (int) std::ceil ((layout.number ("style/labelGap") + layout.number ("style/labelH")) * H);
    for (auto& k : knobs)
    {
        const bool hot = k->slider.isMouseOverOrDragging();
        if (hot || hot != k->wasHot)
        {
            const auto r = k->slider.getBounds();
            repaint (r.withY (r.getBottom()).withHeight (gap).expanded (r.getWidth() / 2, 0));
        }
        k->wasHot = hot;
    }

    scope.repaint();
}

//==============================================================================
void ScopeView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float cr = r.getHeight() * 0.12f;
    g.setColour (palette::screen);
    g.fillRoundedRectangle (r, cr);

    g.setColour (palette::trace.withAlpha (0.08f));
    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine (juce::roundToInt (r.getX() + r.getWidth() * (float) i / 8.0f), r.getY(), r.getBottom());
    g.drawHorizontalLine (juce::roundToInt (r.getCentreY()), r.getX(), r.getRight());

    // snapshot lock-free: dal più vecchio al più recente
    const int write = machine.uiScopeWrite.load();
    const int n = mp::Machine::scopeSize;
    for (int i = 0; i < n; ++i)
        snapshot[(size_t) i] = machine.uiScope[(size_t) ((write + i) % n)].load (std::memory_order_relaxed);

    // trigger sul fronte di salita nella prima metà → forma d'onda ferma
    const int shown = n / 2;
    int start = 0;
    for (int i = 1; i < n - shown; ++i)
        if (snapshot[(size_t) i - 1] < 0.0f && snapshot[(size_t) i] >= 0.0f) { start = i; break; }

    float peak = 1.0e-3f;
    for (int i = start; i < start + shown; ++i) peak = juce::jmax (peak, std::abs (snapshot[(size_t) i]));
    const float gain = 0.42f * r.getHeight() / juce::jmax (0.25f, peak);

    juce::Path trace;
    for (int i = 0; i < shown; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / (float) (shown - 1);
        const float y = r.getCentreY() - snapshot[(size_t) (start + i)] * gain;
        if (i == 0) trace.startNewSubPath (x, y); else trace.lineTo (x, y);
    }
    g.setColour (palette::trace.withAlpha (0.25f));
    g.strokePath (trace, juce::PathStrokeType (3.0f));
    g.setColour (palette::trace);
    g.strokePath (trace, juce::PathStrokeType (1.2f));

    g.setColour (palette::moduleEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), cr, 1.0f);
}
