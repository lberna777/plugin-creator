#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// UI PROVVISORIA, solo per provare le funzioni della piastra finché non arriva la grafica (Gemini).
// Comandi + contatore + linea del nastro col collage delle passate + parametri generici.
class C60AudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit C60AudioProcessorEditor (C60AudioProcessor&);
    ~C60AudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::String formatTime (int64_t samples, double sr) const;

    class Timeline : public juce::Component
    {
    public:
        explicit Timeline (C60AudioProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
    private:
        C60AudioProcessor& proc;
    };

    C60AudioProcessor& proc;
    juce::TextButton punchIn { "PUNCH IN" }, punchOut { "PUNCH OUT" }, undo { "UNDO" }, memo { "MEMO" },
                     cal { "CAL" }, erase { "ERASE" }, exportWav { "EXPORT" };
    juce::Label status;
    Timeline timeline { proc };
    juce::GenericAudioProcessorEditor params { proc };
    std::unique_ptr<juce::FileChooser> chooser;
    int calAnim = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (C60AudioProcessorEditor)
};
