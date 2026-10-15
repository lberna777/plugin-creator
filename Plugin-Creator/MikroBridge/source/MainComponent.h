#pragma once

#include "Bridge.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Finestra del ponte: specchio dei 16 pad, stato della connessione, impostazioni.
class MainComponent final : public juce::Component, private juce::Timer
{
public:
    MainComponent (Bridge& b, juce::PropertiesFile& props);
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    class PadGrid;

    void timerCallback() override;
    void load();
    void save();
    void fillBaseNotes();
    void addRow (juce::Label& label, juce::Component& c, const juce::String& text);

    Bridge& bridge;
    juce::PropertiesFile& props;

    std::unique_ptr<PadGrid> grid;
    juce::Label status, legend;
    juce::Label modeL, baseL, padChL, ctrlChL, curveL, threshL;
    juce::ComboBox mode, base, padCh, ctrlCh, curve;
    juce::Slider threshold;
    juce::ToggleButton aftertouch { "Aftertouch polifonico" }, flipped { "Pad numerati al contrario" };
    int shownMode = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
