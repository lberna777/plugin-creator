// Pezzi della faccia dell'RSU-10: tasti, manopole, pad, ribbon, i due schermi.
#pragma once

#include "../PluginProcessor.h"
#include "Skin.h"

class RSU10Editor;

// Stato della UI condiviso dai pezzi (thread dei messaggi)
struct UiState
{
    int bank = 0;            // banco mostrato sui 16 pad
    int viewLayer = 0;       // strato mostrato nello schermo
    bool shift = false;      // SHIFT agganciato col mouse
    juce::File browserSelection;
};

// ---------------------------------------------------------------- tasto

class Key final : public juce::Button
{
public:
    Key (const juce::String& label, skin::KeyStyle s) : juce::Button (label), style (s) {}
    void paintButton (juce::Graphics& g, bool, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        skin::drawKey (g, r, style, down);
        // ENTER, -1/+1 e frecce hanno la scritta sul tasto, come sull'SU10
        if (style == skin::KeyStyle::Enter || style == skin::KeyStyle::Tiny)
            skin::drawSilk (g, getButtonText(), r.translated (0.0f, down ? 1.0f : 0.0f), juce::Justification::centred,
                            r.getHeight() * 0.42f, juce::Colour (0xff2a2a2a));
        // tasti in gomma delle pagine: scritta sul tasto, ambra quando la pagina è attiva
        else if (labelOnKey)
            skin::drawSilk (g, getButtonText(), r.reduced (r.getWidth() * 0.08f, 0.0f).translated (0.0f, down ? 1.0f : 0.0f),
                            juce::Justification::centred, r.getHeight() * 0.2f,
                            led != nullptr && led() ? skin::col::amber : skin::col::silk.withAlpha (0.75f));
    }
    skin::KeyStyle style;
    bool labelOnKey = false;
    std::function<bool()> led;   // LED moderno accanto all'etichetta (nullptr = nessuno)
};

// ---------------------------------------------------------------- manopola

class Knob final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob (RSU10AudioProcessor& p, UiState& u) : proc (p), ui (u) {}

    void setParam (int ppIndex) { pp = ppIndex; repaint(); }   // -1 = slot vuoto
    int param() const noexcept { return pp; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hovering = false; repaint(); }

    juce::Rectangle<float> knobArea() const;

private:
    float value01() const;
    void setValue01 (float v);

    RSU10AudioProcessor& proc;
    UiState& ui;
    int pp = -1;
    float dragStart01 = 0.0f;
    bool hovering = false, dragging = false;
};

// ---------------------------------------------------------------- pad

class PadView final : public juce::Component,
                      public juce::DragAndDropTarget,
                      public juce::FileDragAndDropTarget
{
public:
    PadView (RSU10AudioProcessor& p, UiState& u, int index) : proc (p), ui (u), slot (index) {}

    int padIndex() const noexcept { return ui.bank * rsu::kPadsPerBank + slot; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { hover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { hover = false; repaint(); }

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { hover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override            { hover = false; repaint(); }

    juce::Rectangle<float> padArea() const;
    float glow = 0.0f;   // LED, decade nel timer dell'editor

private:
    void dropFiles (const juce::Array<juce::File>& files, bool asLayer);
    void showMenu();

    RSU10AudioProcessor& proc;
    UiState& ui;
    int slot;
    bool pressed = false, hover = false, dragging = false;
    int playing = 0;   // pad che sta suonando per questo clic (in 16 VELOCITY / KEYBOARD non è questo)
};

// ---------------------------------------------------------------- ribbon

class Ribbon final : public juce::Component
{
public:
    explicit Ribbon (RSU10AudioProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> stripArea() const;
    juce::Rectangle<float> chipArea (int i) const;
    void setFromX (float x);

    RSU10AudioProcessor& proc;
    bool touching = false;
};

// ---------------------------------------------------------------- schermo del browser

class BrowserScreen final : public juce::Component, private juce::ListBoxModel
{
public:
    BrowserScreen (RSU10AudioProcessor& p, UiState& u);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    void openFolder (const juce::File& folder);
    void up();
    void step (int delta, bool preview);   // BWD / FWD
    void previewSelection();
    juce::File selection() const;

private:
    int getNumRows() override { return (int) entries.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int row) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;

    struct Place { juce::String name; juce::File folder; };
    juce::Rectangle<int> placeArea (int i) const;
    juce::Rectangle<int> headerArea() const;

    RSU10AudioProcessor& proc;
    UiState& ui;
    juce::ListBox list;
    std::vector<juce::File> entries;
    std::vector<Place> places;
    bool autoPreview = true;
};

// ---------------------------------------------------------------- schermo principale

class MainScreen final : public juce::Component
{
public:
    MainScreen (RSU10AudioProcessor& p, UiState& u) : proc (p), ui (u) {}

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    std::function<juce::Array<juce::Rectangle<int>>()> knobColumns;   // colonne delle 8 manopole, per i valori
    std::function<void()> onSettingsChanged;

private:
    juce::Rectangle<float> waveArea() const;
    juce::Rectangle<float> topBar() const;
    juce::Rectangle<float> valueBar() const;
    juce::Rectangle<float> chip (int i) const;   // 0..3 strati, 4 SCALE, 5 sorgente REC
    int markerAt (juce::Point<float>) const;
    float xToFrac (float x) const;
    float fracToX (float f) const;

    RSU10AudioProcessor& proc;
    UiState& ui;
    int dragMarker = -1;   // 0 inizio, 1 fine, 2 loop
    int hoverMarker = -1;
};

// ---------------------------------------------------------------- schermino del kit

class KitScreen final : public juce::Component
{
public:
    explicit KitScreen (RSU10AudioProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    std::function<void()> onClick;

private:
    RSU10AudioProcessor& proc;
};
