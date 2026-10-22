#include "PluginEditor.h"

using namespace juce;

namespace
{
    const StringArray kKnobNames { "DENSITA", "MOVIMENTO", "TIMBRO", "DISTANZA", "AMPIEZZA", "LIVELLO" };
    const char* kKnobParams[6] = { ParamID::density, ParamID::motion, ParamID::tone,
                                   ParamID::depth,   ParamID::width,  ParamID::level };

    // Cosa vuol dire ogni manopola nella scena corrente: la stessa manopola cambia mestiere.
    const char* kHints[4] =
    {
        "traffico  |  velocita dei veicoli  |  brillantezza  |  distanza dalla strada",
        "crepitii  |  (non usato)  |  fiamma viva o brace  |  distanza dal focolare",
        "intensita  |  vento  |  neve < > pioggia  |  distanza del temporale",
        "peso del passo  |  andatura  |  durezza della suola  |  distanza"
    };

    Font uiFont (float size, bool bold = false)
    {
        return Font (FontOptions (size, bold ? Font::bold : Font::plain));
    }
}

//==============================================================================
void DragZone::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool ready = getFile && getFile().existsAsFile();

    g.setColour (ready ? Palette::ok.withAlpha (hover ? 0.16f : 0.09f) : Palette::panel.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 8.0f);

    Path dashed;
    dashed.addRoundedRectangle (r, 8.0f);
    const float dashes[] = { 5.0f, 4.0f };
    PathStrokeType (1.2f).createDashedStroke (dashed, dashed, dashes, 2);
    g.setColour (ready ? Palette::ok.withAlpha (0.8f) : Palette::stroke);
    g.fillPath (dashed);

    auto inner = r.reduced (10.0f);
    auto top = inner.removeFromTop (inner.getHeight() * 0.55f);

    g.setColour (ready ? Palette::text : Palette::textDim);
    g.setFont (uiFont (13.0f, true));
    g.drawText (ready ? "TRASCINA IL WAV NEL PROGETTO" : "NESSUN SAMPLE GENERATO",
                top, Justification::centredBottom);

    g.setColour (Palette::textDim);
    g.setFont (uiFont (11.5f));
    g.drawText (getCaption ? getCaption() : String(), inner, Justification::centredTop);
}

void DragZone::mouseDrag (const MouseEvent&)
{
    if (! getFile) return;
    const auto f = getFile();
    if (f.existsAsFile())
        DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

//==============================================================================
AtmosForgeAudioProcessorEditor::AtmosForgeAudioProcessorEditor (AtmosForgeAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      layout (BinaryData::layout_json, BinaryData::layout_jsonSize)
{
    setLookAndFeel (&lnf);

    // --- schede scena
    for (int i = 0; i < 4; ++i)
    {
        tabs[i].setButtonText (sceneNames()[i]);
        tabs[i].setClickingTogglesState (true);
        tabs[i].onClick = [this, i]
        {
            if (auto* param = proc.apvts.getParameter (ParamID::scene))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->convertTo0to1 ((float) i));
                param->endChangeGesture();
            }
        };
        addAndMakeVisible (tabs[i]);
    }

    // --- manopole
    for (int i = 0; i < 6; ++i)
    {
        knobs[i].setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        knobs[i].setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        knobs[i].setDoubleClickReturnValue (true, knobs[i].getDoubleClickReturnValue());
        addAndMakeVisible (knobs[i]);
        knobAttach[i] = std::make_unique<SliderAttach> (proc.apvts, kKnobParams[i], knobs[i]);
    }

    // --- materiale / auto / seme
    surfaceBox.addItemList (surfaceNames(), 1);
    addAndMakeVisible (surfaceBox);
    surfaceAttach = std::make_unique<ComboAttach> (proc.apvts, ParamID::surface, surfaceBox);

    addAndMakeVisible (autoToggle);
    autoAttach = std::make_unique<ButtonAttach> (proc.apvts, ParamID::autoGen, autoToggle);

    seedSlider.setSliderStyle (Slider::LinearBar);
    seedSlider.setTextBoxStyle (Slider::TextBoxLeft, false, 60, 20);
    seedSlider.setColour (Slider::trackColourId, Palette::stroke);
    addAndMakeVisible (seedSlider);
    seedAttach = std::make_unique<SliderAttach> (proc.apvts, ParamID::seed, seedSlider);

    diceButton.onClick = [this] { seedSlider.setValue (Random::getSystemRandom().nextInt (10000),
                                                       sendNotificationSync); };
    addAndMakeVisible (diceButton);

    // --- innesco manuale (gli stessi eventi delle note MIDI)
    smallTrigger.onClick = [this] { proc.triggerManualEvent (false); };
    bigTrigger.onClick   = [this] { proc.triggerManualEvent (true); };
    addAndMakeVisible (smallTrigger);
    addAndMakeVisible (bigTrigger);

    // --- export
    lengthBox.addItemList ({ "5 secondi", "10 secondi", "30 secondi", "60 secondi", "3 minuti" }, 1);
    lengthBox.setSelectedId (2, dontSendNotification);
    addAndMakeVisible (lengthBox);

    renderButton.onClick = [this] { doRender (false); };
    saveButton.onClick   = [this] { doRender (true); };
    addAndMakeVisible (renderButton);
    addAndMakeVisible (saveButton);

    statusLabel.setFont (uiFont (11.5f));
    statusLabel.setColour (Label::textColourId, Palette::textDim);
    statusLabel.setJustificationType (Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    dragZone.getFile    = [this] { return lastRendered; };
    dragZone.getCaption = [this] { return lastRendered.existsAsFile() ? lastRendered.getFileName()
                                                                      : String ("premi GENERA WAV"); };
    addAndMakeVisible (dragZone);

    proc.apvts.addParameterListener (ParamID::scene, this);
    applySceneColour();

    const int w = (int) layout.number ("window/w", 780.0f);
    const int h = (int) layout.number ("window/h", 540.0f);
    setResizable (true, true);
    setResizeLimits (w * 3 / 4, h * 3 / 4, w * 2, h * 2);
    getConstrainer()->setFixedAspectRatio ((double) w / (double) h);
    setSize (w, h);

    startTimerHz (30);
}

AtmosForgeAudioProcessorEditor::~AtmosForgeAudioProcessorEditor()
{
    proc.apvts.removeParameterListener (ParamID::scene, this);
    setLookAndFeel (nullptr);
}

int AtmosForgeAudioProcessorEditor::currentScene() const
{
    return (int) proc.apvts.getRawParameterValue (ParamID::scene)->load();
}

void AtmosForgeAudioProcessorEditor::parameterChanged (const String&, float)
{
    // arriva dal thread audio: qui si tocca solo un flag, la UI si aggiorna nel timer
    sceneShown = -1;
}

void AtmosForgeAudioProcessorEditor::applySceneColour()
{
    const int s = currentScene();
    if (s == sceneShown) return;
    sceneShown = s;

    lnf.setAccent (Palette::forScene (s));
    for (int i = 0; i < 4; ++i)
        tabs[i].setToggleState (i == s, dontSendNotification);

    // il materiale serve a passi e strada; nelle altre scene resta spento
    const bool surfaceUsed = (s == 0 || s == 3);
    surfaceBox.setEnabled (surfaceUsed);
    knobs[1].setEnabled (s != 1);       // il camino non ha "movimento"

    repaint();
}

void AtmosForgeAudioProcessorEditor::timerCallback()
{
    applySceneColour();

    const float peak = proc.getPeakLevel();
    meterLevel = jmax (peak, meterLevel * 0.88f);   // caduta lenta, salita immediata
    repaint (layout.rect ("meter/area", getLocalBounds()).expanded (2));
    repaint (layout.rect ("knobLabels/valueRow", getLocalBounds()));
}

//==============================================================================
void AtmosForgeAudioProcessorEditor::doRender (bool thenAskWhereToSave)
{
    static const double lengths[] = { 5.0, 10.0, 30.0, 60.0, 180.0 };
    const double seconds = lengths[jlimit (0, 4, lengthBox.getSelectedId() - 1)];
    const double sampleRate = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;

    auto dest = File::getSpecialLocation (File::tempDirectory)
                    .getChildFile (proc.suggestedFileName (seconds));

    statusLabel.setText ("rendering...", dontSendNotification);
    repaint();

    const bool ok = proc.renderToFile (dest, seconds, sampleRate);
    if (! ok)
    {
        statusLabel.setText ("render fallito", dontSendNotification);
        return;
    }

    lastRendered = dest;
    statusLabel.setText (String (roundToInt (seconds)) + " s @ "
                         + String (roundToInt (sampleRate / 1000.0)) + " kHz - pronto",
                         dontSendNotification);
    dragZone.repaint();

    if (! thenAskWhereToSave) return;

    chooser = std::make_unique<FileChooser> ("Salva il sample", File::getSpecialLocation (File::userMusicDirectory)
                                                                    .getChildFile (dest.getFileName()), "*.wav");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles,
                          [this, dest] (const FileChooser& fc)
    {
        const auto target = fc.getResult();
        if (target == File()) return;
        target.deleteFile();
        if (dest.copyFileTo (target))
        {
            lastRendered = target;
            statusLabel.setText ("salvato in " + target.getParentDirectory().getFileName(),
                                 dontSendNotification);
            dragZone.repaint();
        }
    });
}

//==============================================================================
void AtmosForgeAudioProcessorEditor::paint (Graphics& g)
{
    auto area = getLocalBounds();

    // --- piastra di fondo: gradiente + vignetta, nessun testo cotto
    ColourGradient bg (Palette::plateLight, 0.0f, 0.0f,
                       Palette::plate, 0.0f, (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();

    const auto accent = Palette::forScene (sceneShown);

    // alone della scena corrente in alto
    g.setGradientFill (ColourGradient (accent.withAlpha (0.10f), getWidth() * 0.5f, 0.0f,
                                       Colours::transparentBlack, getWidth() * 0.5f, getHeight() * 0.42f, false));
    g.fillRect (area);

    // --- intestazione
    g.setColour (Palette::text);
    g.setFont (uiFont (jmax (18.0f, getHeight() * 0.045f), true));
    g.drawText ("ATMOS FORGE", layout.rect ("header/title", area), Justification::centredLeft);

    g.setColour (Palette::textDim);
    g.setFont (uiFont (11.0f));
    g.drawText ("v" + String (JucePlugin_VersionString) + "   generatore di ambienti",
                layout.rect ("header/version", area), Justification::centredRight);

    // --- etichette delle manopole + valori (disegnate, mai baked)
    auto nameRow = layout.rect ("knobLabels/row", area);
    auto valueRow = layout.rect ("knobLabels/valueRow", area);
    for (int i = 0; i < 6; ++i)
    {
        auto k = layout.rect ("knobs/" + String (kKnobParams[i]), area);
        auto nameR = nameRow.withX (k.getX() - k.getWidth() / 4).withWidth (k.getWidth() * 3 / 2);
        auto valR  = valueRow.withX (nameR.getX()).withWidth (nameR.getWidth());

        g.setColour (knobs[i].isEnabled() ? Palette::textDim : Palette::textDim.withAlpha (0.4f));
        g.setFont (uiFont (10.5f, true));
        g.drawText (kKnobNames[i], nameR, Justification::centred);

        g.setColour (knobs[i].isEnabled() ? Palette::text : Palette::textDim.withAlpha (0.5f));
        g.setFont (uiFont (12.0f));
        if (auto* param = proc.apvts.getParameter (kKnobParams[i]))
            g.drawText (param->getCurrentValueAsText(), valR, Justification::centred);
    }

    // --- riga dei controlli: etichette
    g.setColour (Palette::textDim);
    g.setFont (uiFont (10.5f, true));
    g.drawText ("MATERIALE", layout.rect ("controls/surfaceLabel", area), Justification::centredLeft);
    g.drawText ("SEME", layout.rect ("controls/seedLabel", area), Justification::centredLeft);

    // --- meter di picco
    auto meter = layout.rect ("meter/area", area);
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (meter.toFloat(), 4.0f);
    g.setColour (Palette::stroke);
    g.drawRoundedRectangle (meter.toFloat().reduced (0.5f), 4.0f, 1.0f);

    const float level = jlimit (0.0f, 1.0f, meterLevel);
    if (level > 0.001f)
    {
        auto fill = meter.toFloat().reduced (2.0f);
        fill.setWidth (fill.getWidth() * level);
        g.setColour (level > 0.95f ? Palette::hot : accent);
        g.fillRoundedRectangle (fill, 3.0f);
    }

    g.setColour (Palette::textDim);
    g.setFont (uiFont (10.0f));
    g.drawText (kHints[jlimit (0, 3, sceneShown)],
                layout.rect ("meter/hint", area), Justification::centredLeft);

    // --- pannello export
    auto panel = layout.rect ("export/panel", area);
    g.setColour (Palette::panel.withAlpha (0.55f));
    g.fillRoundedRectangle (panel.toFloat(), 8.0f);
    g.setColour (Palette::stroke);
    g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 8.0f, 1.0f);
}

void AtmosForgeAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    static const char* tabKeys[4] = { "road", "fire", "weather", "steps" };

    for (int i = 0; i < 4; ++i)
        tabs[i].setBounds (layout.rect (String ("tabs/") + tabKeys[i], area));

    for (int i = 0; i < 6; ++i)
        knobs[i].setBounds (layout.rect ("knobs/" + String (kKnobParams[i]), area));

    surfaceBox.setBounds  (layout.rect ("controls/surface", area));
    autoToggle.setBounds  (layout.rect ("controls/auto", area));
    seedSlider.setBounds  (layout.rect ("controls/seed", area));
    diceButton.setBounds  (layout.rect ("controls/dice", area));

    smallTrigger.setBounds (layout.rect ("triggers/small", area));
    bigTrigger.setBounds   (layout.rect ("triggers/big", area));

    lengthBox.setBounds    (layout.rect ("export/length", area));
    renderButton.setBounds (layout.rect ("export/render", area));
    saveButton.setBounds   (layout.rect ("export/save", area));
    statusLabel.setBounds  (layout.rect ("export/status", area));
    dragZone.setBounds     (layout.rect ("export/drag", area));
}
