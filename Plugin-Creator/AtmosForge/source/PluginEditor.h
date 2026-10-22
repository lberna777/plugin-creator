#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include "Layout.h"

// Zona di trascinamento: dopo un render, si prende il WAV e lo si tira dentro al DAW.
class DragZone final : public juce::Component
{
public:
    std::function<juce::File()> getFile;                 // file corrente (o non esistente)
    std::function<juce::String()> getCaption;

    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    bool hover = false;
};

class AtmosForgeAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer,
                                             private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit AtmosForgeAudioProcessorEditor (AtmosForgeAudioProcessor&);
    ~AtmosForgeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttach  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void parameterChanged (const juce::String& id, float newValue) override;
    void applySceneColour();
    void doRender (bool thenAskWhereToSave);
    int  currentScene() const;

    AtmosForgeAudioProcessor& proc;
    AtmosLookAndFeel lnf;
    Layout layout;

    juce::TextButton tabs[4];
    juce::Slider knobs[6];                      // density, motion, tone, depth, width, level
    std::unique_ptr<SliderAttach> knobAttach[6];

    juce::ComboBox surfaceBox, lengthBox;
    juce::ToggleButton autoToggle { "AUTO" };
    juce::Slider seedSlider;
    juce::TextButton diceButton { "SEME A CASO" };
    juce::TextButton smallTrigger { "EVENTO" }, bigTrigger { "EVENTO GROSSO" };
    juce::TextButton renderButton { "GENERA WAV" }, saveButton { "SALVA CON NOME" };
    juce::Label statusLabel;
    DragZone dragZone;

    std::unique_ptr<ComboAttach>  surfaceAttach;
    std::unique_ptr<ButtonAttach> autoAttach;
    std::unique_ptr<SliderAttach> seedAttach;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::File lastRendered;
    float meterLevel = 0.0f;       // picco con caduta lenta, solo per l'occhio
    int   sceneShown = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AtmosForgeAudioProcessorEditor)
};
