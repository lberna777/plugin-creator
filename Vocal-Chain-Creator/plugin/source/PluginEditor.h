#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <vector>

/*  VOCAL FORGE — editor.

    Regole rispettate (Plugin-Creator/CLAUDE.md, sezione UI):
      - lo sfondo è una piastra inerte disegnata a codice: nessun valore o indicatore "cotto" dentro;
      - tutto ciò che cambia (valori, LED, meter, motivazioni, badge) è codice, ridisegnato dai dati;
      - la UI legge il DSP solo da std::atomic.

    Questa è la versione funzionale: knob e piastra sono disegnati da codice. Quando arrivano gli
    asset fotorealistici, si sostituisce il LookAndFeel e si legge layout.json — la struttura non cambia.
*/
class VocalForgeEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit VocalForgeEditor (VocalForgeProcessor&);
    ~VocalForgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct ModuleCard : public juce::Component
    {
        ModuleCard (int moduleIndex, VocalForgeEditor& owner);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        int index = 0;
        VocalForgeEditor& editor;
        juce::String label;
        bool enabled = false, selected = false;
        float grDb = 0.0f;
    };

    void timerCallback() override;
    void forgeFromPrompt();
    void forgeFromArtist (int menuIndex);
    std::vector<vf::RulesEngine::ArtistEntry> artists;
    void selectModule (int index);
    void rebuildControls();
    void refreshFromPreset();
    juce::String statusText() const;

    VocalForgeProcessor& processor;

    juce::TextEditor   promptBox;
    juce::TextButton   forgeButton { "FORGE" };
    juce::ComboBox     profileBox, sendsModeBox, artistBox;
    juce::Label        titleLabel, statusLabel, whyTitle, whyLabel, sendsLabel, artistLabel;
    juce::TextEditor   whyBox, sendsBox;
    juce::Viewport     controlsViewport;
    juce::Component    controlsHolder;

    juce::OwnedArray<ModuleCard> cards;
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label> sliderLabels;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<juce::ComboBox> combos;
    /*  Ordine di posizionamento: ogni controllo con la SUA etichetta.
        Prima `resized()` consumava le etichette per tipo e le sfalsava su ogni modulo con un menu. */
    struct PlacedControl { juce::Component* control; juce::Label* label; };
    std::vector<PlacedControl> placement;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachments;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachments;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sendsModeAttachment;

    int selectedModule = 6;                       // Comp 1: il modulo che si guarda per primo
    float inLevel = 0.0f, outLevel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalForgeEditor)
};
