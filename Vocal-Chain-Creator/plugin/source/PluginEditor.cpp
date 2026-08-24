#include "PluginEditor.h"

namespace
{
    // piastra e serigrafia: colori dichiarati una volta sola
    const juce::Colour plateDark   { 0xff14161a };
    const juce::Colour plateMid    { 0xff1d2127 };
    const juce::Colour plateLight  { 0xff262b33 };
    const juce::Colour ink         { 0xffd8dde5 };
    const juce::Colour inkDim      { 0xff8b94a3 };
    const juce::Colour accent      { 0xffe0a04a };
    const juce::Colour accentCold  { 0xff4aa3e0 };
    const juce::Colour ledOn       { 0xff6fdc8c };

    // i 13 moduli, nell'ordine fisso della catena
    const juce::StringArray moduleIds {
        "trim", "gate", "hpf", "room", "eqSub", "ds1", "comp1", "comp2", "sat", "eqTone", "ds2", "limiter", "out"
    };
    const juce::StringArray moduleNames {
        "TRIM", "GATE", "HPF", "ROOM", "EQ SUB", "DE-ESS 1", "COMP 1", "COMP 2", "SAT", "EQ TONE", "DE-ESS 2", "LIMIT", "OUT"
    };

    // parametri mostrati quando si seleziona un modulo (id APVTS)
    const std::map<juce::String, juce::StringArray> moduleParams {
        { "trim",    { "inTrim", "polarity" } },
        { "gate",    { "gateOn", "gateThresh", "gateRange", "gateAtk", "gateRel", "gateKeyLo", "gateKeyHi" } },
        { "hpf",     { "hpfOn", "hpfFreq", "hpfSlope" } },
        { "room",    { "roomOn", "roomThresh", "room1Freq", "room1Q", "room1Depth",
                       "room2Freq", "room2Q", "room2Depth", "room3Freq", "room3Q", "room3Depth" } },
        { "eqSub",   { "eqSubOn", "sub1Freq", "sub1Q", "sub1Gain", "sub2Freq", "sub2Q", "sub2Gain",
                       "sub3Freq", "sub3Q", "sub3Gain" } },
        { "ds1",     { "ds1On", "ds1Freq", "ds1Thresh", "ds1Range", "ds1Mode" } },
        { "comp1",   { "c1On", "c1Thresh", "c1Ratio", "c1Atk", "c1Rel", "c1Knee", "c1Makeup" } },
        { "comp2",   { "c2On", "c2Thresh", "c2Ratio", "c2Atk", "c2Rel", "c2Knee", "c2Makeup" } },
        { "sat",     { "satOn", "satDrive", "satType", "satTilt" } },
        { "eqTone",  { "eqToneOn", "tone1Freq", "tone1Q", "tone1Gain", "tone2Freq", "tone2Q", "tone2Gain",
                       "tone3Freq", "tone3Q", "tone3Gain", "airOn", "airGain" } },
        { "ds2",     { "ds2On", "ds2Freq", "ds2Thresh", "ds2Range", "ds2Mode" } },
        { "limiter", { "limOn", "limCeiling", "limRelease" } },
        { "out",     { "outGain", "mix" } }
    };

    void drawPlate (juce::Graphics& g, juce::Rectangle<int> area, juce::Colour base, float corner = 6.0f)
    {
        juce::ColourGradient gradient (base.brighter (0.06f), area.toFloat().getTopLeft(),
                                       base.darker (0.25f),  area.toFloat().getBottomLeft(), false);
        g.setGradientFill (gradient);
        g.fillRoundedRectangle (area.toFloat(), corner);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), corner, 1.0f);
    }

    void drawMeterBar (juce::Graphics& g, juce::Rectangle<float> area, float amount, juce::Colour colour)
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (area, 2.0f);
        const auto filled = area.withWidth (area.getWidth() * juce::jlimit (0.0f, 1.0f, amount));
        g.setColour (colour);
        g.fillRoundedRectangle (filled, 2.0f);
    }
}

//==============================================================================
VocalForgeEditor::ModuleCard::ModuleCard (int moduleIndex, VocalForgeEditor& owner)
    : index (moduleIndex), editor (owner), label (moduleNames[moduleIndex])
{
    setInterceptsMouseClicks (true, false);
}

void VocalForgeEditor::ModuleCard::mouseDown (const juce::MouseEvent&)
{
    editor.selectModule (index);
}

void VocalForgeEditor::ModuleCard::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    drawPlate (g, area, selected ? plateLight : plateMid, 4.0f);

    if (selected)
    {
        g.setColour (accent.withAlpha (0.9f));
        g.drawRoundedRectangle (area.toFloat().reduced (1.0f), 4.0f, 1.4f);
    }

    auto content = area.reduced (8, 6);
    auto ledArea = content.removeFromLeft (10).withSizeKeepingCentre (8, 8);
    g.setColour (enabled ? ledOn : juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (ledArea.toFloat());
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (ledArea.toFloat(), 1.0f);

    content.removeFromLeft (6);
    g.setColour (enabled ? ink : inkDim.withAlpha (0.6f));
    g.setFont (juce::Font (juce::FontOptions (12.0f)).boldened());
    g.drawText (label, content.removeFromLeft (76), juce::Justification::centredLeft);

    if (grDb > 0.01f)
    {
        auto meter = content.removeFromRight (juce::jmin (90, content.getWidth())).toFloat().withSizeKeepingCentre (
            static_cast<float> (juce::jmin (90, content.getWidth())), 6.0f);
        drawMeterBar (g, meter, grDb / 12.0f, accentCold);
        g.setColour (inkDim);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("-" + juce::String (grDb, 1) + " dB", content, juce::Justification::centredRight);
    }
}

//==============================================================================
VocalForgeEditor::VocalForgeEditor (VocalForgeProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (nullptr);
    getLookAndFeel().setColour (juce::Slider::rotarySliderFillColourId, accent);
    getLookAndFeel().setColour (juce::Slider::rotarySliderOutlineColourId, plateDark);
    getLookAndFeel().setColour (juce::Slider::thumbColourId, ink);
    getLookAndFeel().setColour (juce::TextEditor::backgroundColourId, plateDark);
    getLookAndFeel().setColour (juce::TextEditor::textColourId, ink);
    getLookAndFeel().setColour (juce::TextEditor::outlineColourId, plateLight);
    getLookAndFeel().setColour (juce::ComboBox::backgroundColourId, plateDark);
    getLookAndFeel().setColour (juce::ComboBox::textColourId, ink);
    getLookAndFeel().setColour (juce::Label::textColourId, ink);
    getLookAndFeel().setColour (juce::ToggleButton::textColourId, ink);

    titleLabel.setText ("VOCAL FORGE", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (22.0f)).boldened());
    titleLabel.setColour (juce::Label::textColourId, ink);
    addAndMakeVisible (titleLabel);

    promptBox.setMultiLine (false, false);
    promptBox.setReturnKeyStartsNewLine (false);
    promptBox.setTextToShowWhenEmpty ("scrivi che voce vuoi — es. \"voce trap aggressiva ma non stridula\"", inkDim);
    promptBox.setText (processor.getPrompt(), juce::dontSendNotification);
    promptBox.onReturnKey = [this] { forgeFromPrompt(); };
    addAndMakeVisible (promptBox);

    forgeButton.setColour (juce::TextButton::buttonColourId, accent.darker (0.3f));
    forgeButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    forgeButton.onClick = [this] { forgeFromPrompt(); };
    addAndMakeVisible (forgeButton);

    for (const auto& id : processor.rulesEngine.getProfileIds())
        profileBox.addItem (id, profileBox.getNumItems() + 1);
    profileBox.setText (processor.getProfileId(), juce::dontSendNotification);
    profileBox.onChange = [this] { if (promptBox.getText().isNotEmpty()) forgeFromPrompt(); };
    addAndMakeVisible (profileBox);

    sendsModeBox.addItemList ({ "internal", "logic" }, 1);
    sendsModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "sendsMode", sendsModeBox);
    addAndMakeVisible (sendsModeBox);

    statusLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    statusLabel.setColour (juce::Label::textColourId, inkDim);
    addAndMakeVisible (statusLabel);

    whyTitle.setText ("PERCHÉ", juce::dontSendNotification);
    whyTitle.setFont (juce::Font (juce::FontOptions (12.0f)).boldened());
    whyTitle.setColour (juce::Label::textColourId, accent);
    addAndMakeVisible (whyTitle);

    whyBox.setMultiLine (true, true);
    whyBox.setReadOnly (true);
    whyBox.setScrollbarsShown (true);
    whyBox.setCaretVisible (false);
    whyBox.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (whyBox);

    sendsLabel.setText ("SENDS — bus paralleli, mai in serie", juce::dontSendNotification);
    sendsLabel.setFont (juce::Font (juce::FontOptions (12.0f)).boldened());
    sendsLabel.setColour (juce::Label::textColourId, accentCold);
    addAndMakeVisible (sendsLabel);

    sendsBox.setMultiLine (true, true);
    sendsBox.setReadOnly (true);
    sendsBox.setScrollbarsShown (true);
    sendsBox.setCaretVisible (false);
    sendsBox.setFont (juce::Font (juce::FontOptions (11.5f)));
    addAndMakeVisible (sendsBox);

    for (int i = 0; i < moduleIds.size(); ++i)
    {
        auto* card = cards.add (new ModuleCard (i, *this));
        addAndMakeVisible (card);
    }

    controlsViewport.setViewedComponent (&controlsHolder, false);
    controlsViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (controlsViewport);

    processor.onPresetGenerated = [this] { refreshFromPreset(); };

    rebuildControls();
    refreshFromPreset();
    startTimerHz (24);
    setResizable (true, true);
    setResizeLimits (900, 560, 1600, 1100);
    setSize (1080, 680);
}

VocalForgeEditor::~VocalForgeEditor()
{
    processor.onPresetGenerated = nullptr;
    setLookAndFeel (nullptr);
}

//==============================================================================
void VocalForgeEditor::forgeFromPrompt()
{
    processor.applyPrompt (promptBox.getText(), profileBox.getText());
}

void VocalForgeEditor::selectModule (int index)
{
    selectedModule = index;
    for (auto* card : cards) card->selected = (card->index == index);
    rebuildControls();
    refreshFromPreset();
    repaint();
}

void VocalForgeEditor::rebuildControls()
{
    sliderAttachments.clear();
    buttonAttachments.clear();
    comboAttachments.clear();
    sliders.clear();
    sliderLabels.clear();
    toggles.clear();
    combos.clear();

    const auto moduleId = moduleIds[selectedModule];
    const auto it = moduleParams.find (moduleId);
    if (it == moduleParams.end()) return;

    for (const auto& paramId : it->second)
    {
        auto* parameter = processor.apvts.getParameter (paramId);
        if (parameter == nullptr) continue;

        if (dynamic_cast<juce::AudioParameterBool*> (parameter) != nullptr)
        {
            auto* toggle = toggles.add (new juce::ToggleButton (parameter->getName (24)));
            controlsHolder.addAndMakeVisible (toggle);
            buttonAttachments.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (
                processor.apvts, paramId, *toggle));
        }
        else if (dynamic_cast<juce::AudioParameterChoice*> (parameter) != nullptr)
        {
            auto* combo = combos.add (new juce::ComboBox());
            combo->addItemList (dynamic_cast<juce::AudioParameterChoice*> (parameter)->choices, 1);
            controlsHolder.addAndMakeVisible (combo);
            auto* label = sliderLabels.add (new juce::Label ({}, parameter->getName (24)));
            label->setFont (juce::Font (juce::FontOptions (11.0f)));
            label->setColour (juce::Label::textColourId, inkDim);
            label->setJustificationType (juce::Justification::centred);
            controlsHolder.addAndMakeVisible (label);
            comboAttachments.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (
                processor.apvts, paramId, *combo));
        }
        else
        {
            auto* slider = sliders.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag,
                                                          juce::Slider::TextBoxBelow));
            slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 16);
            slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            slider->setColour (juce::Slider::textBoxTextColourId, ink);
            controlsHolder.addAndMakeVisible (slider);

            auto* label = sliderLabels.add (new juce::Label ({}, parameter->getName (24)));
            label->setFont (juce::Font (juce::FontOptions (11.0f)));
            label->setColour (juce::Label::textColourId, inkDim);
            label->setJustificationType (juce::Justification::centred);
            controlsHolder.addAndMakeVisible (label);

            sliderAttachments.add (new juce::AudioProcessorValueTreeState::SliderAttachment (
                processor.apvts, paramId, *slider));
        }
    }
    resized();
}

juce::String VocalForgeEditor::statusText() const
{
    const auto& preset = processor.getLastPreset();
    if (! processor.hasGeneratedChain())
        return "nessuna catena generata — scrivi una richiesta e premi FORGE";

    juce::String text;
    text << "genere: " << (preset.intent.genre.isNotEmpty() ? preset.intent.genre : juce::String ("—"))
         << "   ·   registro: " << preset.intent.pitchClass
         << "   ·   esecuzione: " << preset.intent.delivery
         << "   ·   target: " << juce::String (preset.loudnessTargetLufs, 1) << " LUFS"
         << "   ·   regole v" << processor.getRulesVersion();
    if (processor.wasTouchedByHand()) text << "   ·   [ MODIFICATO A MANO ]";
    return text;
}

void VocalForgeEditor::refreshFromPreset()
{
    const auto& preset = processor.getLastPreset();
    statusLabel.setText (statusText(), juce::dontSendNotification);

    // il pannello "perché": prima il modulo selezionato, poi tutto il resto
    juce::String why;
    if (processor.hasGeneratedChain())
    {
        const auto moduleId = moduleIds[selectedModule];
        if (const auto* module = preset.find (moduleId))
        {
            why << moduleNames[selectedModule] << (module->enabled ? "  [ ON ]" : "  [ OFF ]") << juce::newLine
                << module->why << juce::newLine << juce::newLine;
            for (const auto& param : module->params)
                why << "  " << param.id << " = " << param.value.toString()
                    << (param.unit.isNotEmpty() ? " " + param.unit : juce::String())
                    << juce::newLine << "      " << param.why << juce::newLine;
        }

        why << juce::newLine << "———— tutta la catena ————" << juce::newLine;
        for (const auto& module : preset.modules)
            why << (module.enabled ? "[ON ] " : "[off] ") << module.label << " — " << module.why << juce::newLine;

        if (! preset.warnings.isEmpty())
        {
            why << juce::newLine << "———— avvisi ————" << juce::newLine;
            for (const auto& warning : preset.warnings) why << "· " << warning << juce::newLine;
        }
    }
    else
    {
        why << "Scrivi che voce vuoi e premi FORGE." << juce::newLine << juce::newLine
            << "Il testo non sceglie un preset da una lista: genera i valori, uno per uno, "
            << "sapendo che il segnale arriva da una stanza non trattata, un microfono Focusrite "
            << "e una Scarlett a due ingressi." << juce::newLine << juce::newLine
            << "Ogni valore che vedi qui sotto ha una motivazione. Se non ce l'ha, è un bug.";
    }
    whyBox.setText (why, false);

    // sezione mandate
    juce::String sends;
    if (preset.sends.empty())
    {
        sends << (processor.hasGeneratedChain()
                  ? "Nessuna mandata per questa richiesta: la voce resta asciutta."
                  : "Le mandate compaiono qui dopo FORGE.");
    }
    else
    {
        for (const auto& send : preset.sends)
        {
            sends << juce::CharacterFunctions::toUpperCase (send.group[0]) << send.group.substring (1)
                  << " — " << send.label << "   (" << send.pluginLogic << ")" << juce::newLine
                  << "  " << send.why << juce::newLine << "  ";
            for (const auto& setting : send.settings)
                sends << setting.id << "=" << setting.value.toString()
                      << (setting.unit.isNotEmpty() ? setting.unit : juce::String()) << "  ";
            sends << juce::newLine << juce::newLine;
        }
    }
    sendsBox.setText (sends, false);

    for (auto* card : cards)
        if (const auto* module = preset.find (moduleIds[card->index]))
            card->enabled = module->enabled;

    repaint();
}

//==============================================================================
void VocalForgeEditor::timerCallback()
{
    auto& meters = processor.getMeters();
    const auto smooth = [] (float current, float target) { return current + 0.35f * (target - current); };

    inLevel  = smooth (inLevel,  juce::Decibels::gainToDecibels (meters.inPeak.load(), -60.0f));
    outLevel = smooth (outLevel, juce::Decibels::gainToDecibels (meters.outPeak.load(), -60.0f));

    const std::map<juce::String, float> gr {
        { "gate",  meters.gateGr.load() },  { "ds1", meters.ds1Gr.load() },
        { "comp1", meters.comp1Gr.load() }, { "comp2", meters.comp2Gr.load() },
        { "ds2",   meters.ds2Gr.load() },   { "limiter", meters.limGr.load() }
    };
    for (auto* card : cards)
    {
        const auto it = gr.find (moduleIds[card->index]);
        card->grDb = (it != gr.end()) ? it->second : 0.0f;
        card->repaint();
    }
    repaint (getLocalBounds().removeFromBottom (28));
}

//==============================================================================
void VocalForgeEditor::paint (juce::Graphics& g)
{
    // piastra inerte: nessun testo o valore disegnato qui dentro
    g.fillAll (plateDark);
    auto area = getLocalBounds();

    drawPlate (g, area.removeFromTop (108).reduced (10, 8), plateMid, 8.0f);

    auto body = getLocalBounds().withTrimmedTop (108).reduced (10, 4);
    auto rack = body.removeFromLeft (250);
    drawPlate (g, rack, plateDark.brighter (0.03f), 8.0f);

    body.removeFromLeft (8);
    auto bottom = body.removeFromBottom (150);
    drawPlate (g, body, plateMid.darker (0.15f), 8.0f);
    drawPlate (g, bottom.withTrimmedTop (8), plateMid.darker (0.25f), 8.0f);

    // meter di ingresso/uscita: codice, non serigrafia
    auto footer = getLocalBounds().removeFromBottom (26).reduced (16, 6);
    auto inArea = footer.removeFromLeft (footer.getWidth() / 2).reduced (4, 4);
    auto outArea = footer.reduced (4, 4);
    g.setColour (inkDim);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText ("IN", inArea.removeFromLeft (24), juce::Justification::centredLeft);
    g.drawText ("OUT", outArea.removeFromLeft (28), juce::Justification::centredLeft);
    drawMeterBar (g, inArea.toFloat(),  (inLevel  + 60.0f) / 60.0f, accentCold.withAlpha (0.85f));
    drawMeterBar (g, outArea.toFloat(), (outLevel + 60.0f) / 60.0f,
                  outLevel > -1.0f ? juce::Colours::orangered : ledOn.withAlpha (0.85f));
}

void VocalForgeEditor::resized()
{
    auto area = getLocalBounds().reduced (10, 8);

    auto header = area.removeFromTop (100);
    titleLabel.setBounds (header.removeFromTop (30).removeFromLeft (240).translated (8, 0));

    auto promptRow = header.removeFromTop (34).reduced (8, 0);
    forgeButton.setBounds (promptRow.removeFromRight (96).reduced (0, 2));
    promptRow.removeFromRight (8);
    profileBox.setBounds (promptRow.removeFromRight (240).reduced (0, 2));
    promptRow.removeFromRight (8);
    sendsModeBox.setBounds (promptRow.removeFromRight (110).reduced (0, 2));
    promptRow.removeFromRight (8);
    promptBox.setBounds (promptRow.reduced (0, 2));

    statusLabel.setBounds (header.removeFromTop (22).reduced (10, 0));

    auto body = area.withTrimmedTop (8);
    body.removeFromBottom (22);

    auto rack = body.removeFromLeft (240).reduced (6, 6);
    for (auto* card : cards)
    {
        card->setBounds (rack.removeFromTop (30));
        rack.removeFromTop (4);
    }

    body.removeFromLeft (14);
    auto bottom = body.removeFromBottom (150);

    auto whyArea = body.removeFromRight (juce::jmax (280, body.getWidth() / 2)).reduced (8, 6);
    whyTitle.setBounds (whyArea.removeFromTop (18));
    whyBox.setBounds (whyArea);

    controlsViewport.setBounds (body.reduced (8, 6));

    auto sendsArea = bottom.reduced (8, 8);
    sendsLabel.setBounds (sendsArea.removeFromTop (18));
    sendsBox.setBounds (sendsArea);

    // griglia dei controlli del modulo selezionato
    const int knobWidth = 86, knobHeight = 92, columns = juce::jmax (1, controlsViewport.getWidth() / knobWidth);
    int column = 0, row = 0;
    auto place = [&] (juce::Component& component, juce::Label* label)
    {
        juce::Rectangle<int> cell (column * knobWidth, row * knobHeight, knobWidth, knobHeight);
        if (label != nullptr)
        {
            label->setBounds (cell.removeFromTop (16));
            component.setBounds (cell.reduced (4, 2));
        }
        else
        {
            component.setBounds (cell.reduced (4, 30));
        }
        if (++column >= columns) { column = 0; ++row; }
    };

    int labelIndex = 0;
    for (auto* toggle : toggles) place (*toggle, nullptr);
    for (auto* combo : combos)   place (*combo, sliderLabels[labelIndex++]);
    for (auto* slider : sliders) place (*slider, sliderLabels[labelIndex++]);

    controlsHolder.setSize (controlsViewport.getWidth(), juce::jmax (controlsViewport.getHeight(), (row + 1) * knobHeight));
}
