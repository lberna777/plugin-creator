#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Layout.h"

// UI fotorealistica: piastra inerte (bg_plate) + asset mobili Gemini + tutto ciò che cambia stato
// disegnato in codice (display VFD, spie, nastro, etichette). Posizioni da layout.json.

//==============================================================================
// Knob e selettori: l'asset ha l'indicatore a ore 12, qui ruota col valore.
class ImageKnob : public juce::Slider
{
public:
    ImageKnob (juce::Image image, int numSteps = 0);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    juce::Image img;
    int steps;
};

// Tasti (trasporto, larghi, quadrati): immagine su/giù + spia disegnata nella fessura.
class DeckKey : public juce::Button
{
public:
    DeckKey (juce::Image up, juce::Image down, juce::Colour ledColour, juce::Rectangle<float> ledArea);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    std::function<float()> ledLevel;   // 0 spenta .. 1 accesa
private:
    juce::Image upImg, downImg;
    juce::Colour led;
    juce::Rectangle<float> ledRel;
};

// Levetta: su = ON. L'immagine si aggancia al foro con un punto d'ancoraggio verticale.
class DeckToggle : public juce::Button
{
public:
    DeckToggle (juce::Image up, juce::Image down) : juce::Button ({}), upImg (up), downImg (down) { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool, bool) override;
    float anchorUp = 0.72f, anchorDown = 0.28f;   // base della leva, frazione dell'altezza dell'immagine
    int imageH = 0;                               // altezza dell'immagine (il componente è più alto)
private:
    juce::Image upImg, downImg;
};

// Vano cassetta: perni che girano, nastro avvolto che passa da una bobina all'altra, vetro fumé.
class CassetteView : public juce::Component
{
public:
    explicit CassetteView (C60AudioProcessor& p);
    void paint (juce::Graphics&) override;
    void tick (double dt);
    juce::Rectangle<float> bodyInWindow;   // dove sta la cassetta, in coordinate locali
private:
    C60AudioProcessor& proc;
    juce::Image shells[3], hubImg;
    double angleL = 0, angleR = 0;
};

// Display fluorescente: meter con picco, contagiri a 7 segmenti, linea del collage, indicatori.
class VfdDisplay : public juce::Component
{
public:
    explicit VfdDisplay (C60AudioProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void tick (double dt);
    bool calBusy = false;
private:
    void drawMeter (juce::Graphics&, juce::Rectangle<float>, float db, float peakDb, const juce::String& ch);
    void drawCounter (juce::Graphics&, juce::Rectangle<float>, int64_t samples, double sr);
    void drawTimeline (juce::Graphics&, juce::Rectangle<float>);
    void drawIndicators (juce::Graphics&, juce::Rectangle<float>);
    C60AudioProcessor& proc;
    float levelDb[2] { -90.f, -90.f }, peakDb[2] { -90.f, -90.f }, peakHold[2] { 0.f, 0.f };
};

//==============================================================================
class C60AudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit C60AudioProcessorEditor (C60AudioProcessor&);
    ~C60AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    ImageKnob& addKnob (const char* paramId, juce::Image img, int steps = 0);
    DeckKey& addKey (juce::Image up, juce::Image down, juce::Colour led, juce::Rectangle<float> ledArea);
    void drawLabel (juce::Graphics&, const juce::String&, juce::Rectangle<int> under, float gap, juce::Justification = juce::Justification::centredTop);

    C60AudioProcessor& proc;
    Layout layout;
    juce::Image plate;

    juce::OwnedArray<ImageKnob> knobs;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAtt;
    juce::OwnedArray<DeckKey> keys;
    juce::OwnedArray<DeckToggle> toggles;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAtt;

    // riferimenti per nome
    std::map<juce::String, juce::Component*> byName;
    DeckKey *recKey = nullptr, *punchInKey = nullptr, *punchOutKey = nullptr, *autoPunchKey = nullptr, *recMuteKey = nullptr,
            *undoKey = nullptr, *eraseKey = nullptr, *exportKey = nullptr, *calKey = nullptr, *autoAzKey = nullptr, *memoKey = nullptr;

    CassetteView cassette;
    VfdDisplay display;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };

    double calTimer = 0, blink = 0;
    std::map<DeckKey*, double> flash;   // spie dei tasti momentanei
    juce::uint32 lastTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (C60AudioProcessorEditor)
};
