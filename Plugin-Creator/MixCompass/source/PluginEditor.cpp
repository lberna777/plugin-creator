#include "PluginEditor.h"
#include "BinaryData.h"

MixCompassEditor::MixCompassEditor (MixCompassProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      layout (BinaryData::layout_json, BinaryData::layout_jsonSize),
      abAttachment (p.apvts, ParamIds::listenRef, abButton),
      matchAttachment (p.apvts, ParamIds::levelMatch, matchButton)
{
    title.setText (juce::String (MC_PRODUCT_NAME) + "  " + MC_VERSION_STRING, juce::dontSendNotification);
    title.setColour (juce::Label::textColourId, Theme::text);
    addAndMakeVisible (title);

    loadButton.onClick = [this] { chooseReference(); };
    addAndMakeVisible (loadButton);

    refLabel.setColour (juce::Label::textColourId, Theme::dim);
    refLabel.setText ("Nessun riferimento", juce::dontSendNotification);
    refLabel.setMinimumHorizontalScale (0.6f);
    addAndMakeVisible (refLabel);

    abButton.setClickingTogglesState (true);
    abButton.setColour (juce::TextButton::buttonOnColourId, Theme::ref.darker (0.4f));
    abButton.setColour (juce::TextButton::buttonColourId, Theme::mix.darker (0.6f));
    abButton.onStateChange = [this] { updateAbText(); };
    addAndMakeVisible (abButton);
    updateAbText();

    matchButton.setColour (juce::ToggleButton::textColourId, Theme::text);
    matchButton.setTooltip ("Il riferimento suona allo stesso volume del tuo mix: confronti il suono, non il volume.");
    addAndMakeVisible (matchButton);

    resetButton.setTooltip ("Ricomincia a misurare il tuo mix da adesso.");
    resetButton.onClick = [this] { proc.requestReset(); };
    addAndMakeVisible (resetButton);

    addAndMakeVisible (spectrum);
    addAndMakeVisible (meters);
    addAndMakeVisible (findings);

    setResizable (true, true);
    setResizeLimits ((int) layout.number ("window/minW"), (int) layout.number ("window/minH"),
                     (int) layout.number ("window/maxW"), (int) layout.number ("window/maxH"));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (layout.number ("window/w") / layout.number ("window/h"));
    setSize ((int) layout.number ("window/w"), (int) layout.number ("window/h"));

    timerCallback();
    startTimerHz (30);
}

MixCompassEditor::~MixCompassEditor()
{
    stopTimer();
}

void MixCompassEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void MixCompassEditor::resized()
{
    const auto area = getLocalBounds();
    const float h = (float) getHeight();
    title.setFont (juce::FontOptions (layout.number ("fonts/title") * h).withStyle ("Bold"));
    refLabel.setFont (juce::FontOptions (layout.number ("fonts/control") * h));

    title.setBounds (layout.rect ("top/title", area));
    loadButton.setBounds (layout.rect ("top/load", area));
    refLabel.setBounds (layout.rect ("top/refName", area));
    abButton.setBounds (layout.rect ("top/ab", area));
    matchButton.setBounds (layout.rect ("top/match", area));
    resetButton.setBounds (layout.rect ("top/reset", area));
    spectrum.setBounds (layout.rect ("spectrum", area));
    meters.setBounds (layout.rect ("meters", area));
    findings.setBounds (layout.rect ("findings", area));
}

void MixCompassEditor::updateAbText()
{
    abButton.setButtonText (abButton.getToggleState() ? juce::String::fromUTF8 ("B · RIFERIMENTO")
                                                      : juce::String::fromUTF8 ("A · TUO MIX"));
}

void MixCompassEditor::chooseReference()
{
    chooser = std::make_unique<juce::FileChooser> ("Scegli un pezzo di riferimento",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   ReferenceLoader::supportedWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file.existsAsFile())
                                  proc.loadReference (file);
                          });
}

void MixCompassEditor::timerCallback()
{
    bool fresh = false;
    if (const auto* s = proc.readMixSnapshot())
    {
        mix = *s;
        fresh = true;
    }

    const auto status = proc.getReferenceStatus();
    const auto* ref = status.analysis.get();

    switch (status.state)
    {
        case MixCompassProcessor::RefState::none:
            refLabel.setText ("Nessun riferimento: caricane uno", juce::dontSendNotification);
            refLabel.setColour (juce::Label::textColourId, Theme::dim);
            break;
        case MixCompassProcessor::RefState::loading:
            refLabel.setText (status.message, juce::dontSendNotification);
            refLabel.setColour (juce::Label::textColourId, Theme::dim);
            break;
        case MixCompassProcessor::RefState::ready:
            refLabel.setText (status.name, juce::dontSendNotification);
            refLabel.setColour (juce::Label::textColourId, Theme::ref);
            break;
        case MixCompassProcessor::RefState::error:
            refLabel.setText (status.message, juce::dontSendNotification);
            refLabel.setColour (juce::Label::textColourId, Theme::warn);
            break;
    }
    refLabel.setTooltip (status.message.isNotEmpty() ? status.message : status.name);
    abButton.setEnabled (ref != nullptr);

    if (fresh)
    {
        spectrum.setData (mix, ref);
        meters.setData (mix, ref);
    }
    // le frasi cambiano lentamente: una volta al secondo, o subito al primo giro
    if (ticks++ % 30 == 0)
        findings.setFindings (mc::compare (mix, ref));
}
