#pragma once

#include "PluginProcessor.h"
#include "ui/Layout.h"
#include "ui/Widgets.h"

class RSU10Editor final : public juce::AudioProcessorEditor,
                          public juce::DragAndDropContainer,
                          private juce::Timer
{
public:
    explicit RSU10Editor (RSU10AudioProcessor&);
    ~RSU10Editor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void renderBackground();
    void setupKey (Key& k, std::function<void()> onClick, std::function<bool()> led = nullptr, bool labelAbove = true);
    void selectPad (int pad);
    bool editorCommand (Cmd c);
    void enterKey();
    juce::Rectangle<int> gridCell (const char* group, int col, int row) const;
    void kitMenu();
    void saveKitAs();
    void openKit();
    void toggleParam (const char* id);
    void setRibbonMode (int mode);
    void transportPlay();

    RSU10AudioProcessor& proc;
    Layout layout;
    UiState ui;

    KitScreen kitScreen { proc };
    BrowserScreen browser { proc, ui };
    MainScreen mainScreen { proc, ui };
    Ribbon ribbon { proc };
    std::array<std::unique_ptr<Knob>, rsu::kKnobs> knobs;
    std::array<std::unique_ptr<PadView>, rsu::kPadsPerBank> padViews;

    Key keySave { "SAVE", skin::KeyStyle::Grey }, keyLoad { "LOAD", skin::KeyStyle::Grey };
    Key keyHold { "HOLD", skin::KeyStyle::Grey }, keyExit { "EXIT", skin::KeyStyle::Violet };
    Key keyRev { "REV", skin::KeyStyle::Grey }, keyScratch { "SCRATCH", skin::KeyStyle::Grey };
    Key keyShift { "SHIFT", skin::KeyStyle::Grey }, keyBank { "BANK", skin::KeyStyle::Grey };
    std::array<std::unique_ptr<Key>, rsu::kPages> pageKeys;

    // Funzioni della Maschine (blocco 3x3 a sinistra)
    Key keyMute { "MUTE", skin::KeyStyle::Grey }, keySolo { "SOLO", skin::KeyStyle::Grey }, keySelect { "SELECT", skin::KeyStyle::Grey };
    Key keyDup { "DUPLIC.", skin::KeyStyle::Grey }, keyErase { "ERASE", skin::KeyStyle::Grey }, keyUndo { "UNDO", skin::KeyStyle::Grey };
    Key keyRepeat { "REPEAT", skin::KeyStyle::Grey }, keyPadMode { "PAD MODE", skin::KeyStyle::Grey }, keyFixVel { "FIX VEL", skin::KeyStyle::Grey };
    Key keyEnter { "ENTER", skin::KeyStyle::Enter }, keyMinus { "-1", skin::KeyStyle::Tiny }, keyPlus { "+1", skin::KeyStyle::Tiny };
    Key keyLeft { "<", skin::KeyStyle::Tiny }, keyRight { ">", skin::KeyStyle::Tiny };
    Key keyRec { "REC", skin::KeyStyle::Rec }, keyTop { "TOP", skin::KeyStyle::Rubber }, keyBwd { "BWD", skin::KeyStyle::Rubber };
    Key keyStop { "STOP", skin::KeyStyle::Rubber }, keyPlay { "PLAY", skin::KeyStyle::Rubber }, keyFwd { "FWD", skin::KeyStyle::Rubber };

    struct Labelled { Key* key; bool above; };
    std::vector<Labelled> labelled;

    juce::Image background;
    juce::ComponentBoundsConstrainer constrainer;
    uint32_t lastGeneration = 0, lastSampleGen = 0;
    int lastSelected = -1, lastPage = -1;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RSU10Editor)
};
