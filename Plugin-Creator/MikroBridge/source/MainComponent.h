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
    class DisplayMirror;

    void timerCallback() override;
    void load();
    void save();
    void fillBaseNotes();
    void addRow (juce::Label& label, juce::Component& c, const juce::String& text);

    Bridge& bridge;
    juce::PropertiesFile& props;

    std::unique_ptr<PadGrid> grid;
    std::unique_ptr<DisplayMirror> display;
    juce::Label status, legend;
    juce::Label modeL, baseL, rootL, scaleL, padChL, ctrlChL, curveL, threshL;
    juce::ComboBox mode, base, root, scale, padCh, ctrlCh, curve;
    juce::Slider threshold, crosstalk, ledLevel;
    juce::Label crosstalkL, ledLevelL;
    juce::ToggleButton aftertouch { "Aftertouch polifonico" }, flipped { "Pad numerati al contrario" };
    juce::ToggleButton harmony { "Armonia: in tastiera ogni combinazione di pad suona bene" };
    bool shownHarmony = false;

    // Manopola e arpeggiatore (colonna destra)
    juce::Label knobL, arpPatternL, arpRateL, arpOctL, arpGateL, arpBpmL;
    juce::ComboBox knob, arpPattern, arpRate, arpOct;
    juce::Slider arpGate, arpBpm;
    juce::ToggleButton arpOn { "Arpeggiatore (SHIFT + GROUP)" }, arpLatch { "Latch: continua anche staccando le dita" };
    int shownKnob = -1, shownArpRate = -1;
    bool shownArp = false;
    int shownMode = -1, shownRoot = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
