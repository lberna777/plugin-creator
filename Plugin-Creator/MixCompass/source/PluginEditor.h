#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Views.h"

class MixCompassEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MixCompassEditor (MixCompassProcessor&);
    ~MixCompassEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseReference();
    void updateAbText();

    MixCompassProcessor& proc;
    Layout layout;

    juce::Label title;
    juce::TextButton loadButton { "Carica riferimento" };
    juce::Label refLabel;
    juce::TextButton abButton;
    juce::ToggleButton matchButton { "Match" };
    juce::TextButton resetButton { "Reset" };

    juce::AudioProcessorValueTreeState::ButtonAttachment abAttachment, matchAttachment;

    SpectrumView spectrum { layout };
    MetersView meters { layout };
    FindingsView findings { layout };

    std::unique_ptr<juce::FileChooser> chooser;
    mc::Snapshot mix;
    int ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixCompassEditor)
};
