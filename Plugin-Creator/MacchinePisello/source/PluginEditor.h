#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include "Layout.h"

class ScopeView : public juce::Component
{
public:
    explicit ScopeView (mp::Machine& m) : machine (m) {}
    void paint (juce::Graphics&) override;

private:
    mp::Machine& machine;
    std::array<float, mp::Machine::scopeSize> snapshot {};
};

class MacchinePiselloEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MacchinePiselloEditor (MacchinePiselloProcessor&);
    ~MacchinePiselloEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void renderPanel();
    void paintLabels (juce::Graphics&);
    void paintLeds (juce::Graphics&);
    void paintMeter (juce::Graphics&);

    struct Knob
    {
        juce::String id, label, layoutGroup;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        bool wasHot = false;
    };

    MacchinePiselloProcessor& machineProcessor;
    Layout layout;
    MachineLookAndFeel lnf;

    std::vector<std::unique_ptr<Knob>> knobs;
    juce::ComboBox lfoShape, seqRate, seqScale;
    juce::ToggleButton drone { "DRONE" }, seqOn { "RUN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lfoShapeAtt, seqRateAtt, seqScaleAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> droneAtt, seqOnAtt;
    ScopeView scope;

    juce::Image panelCache;
    int   lastStep = -2;
    float meterLevel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MacchinePiselloEditor)
};
