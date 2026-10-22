#pragma once
#include <JuceHeader.h>
#include "Layout.h"
#include "analysis/Compare.h"

// Palette dell'MVP (la grafica finale è la Fase 4). Colori, non posizioni: le posizioni stanno in layout.json.
namespace Theme
{
    const juce::Colour background { 0xff15171c };
    const juce::Colour panel      { 0xff1e2128 };
    const juce::Colour grid       { 0xff2e323b };
    const juce::Colour text       { 0xffe6e8ec };
    const juce::Colour dim        { 0xff8a909c };
    const juce::Colour mix        { 0xff4fd1c5 };   // il tuo mix
    const juce::Colour ref        { 0xfff6ad55 };   // il riferimento
    const juce::Colour warn       { 0xfffc8181 };
    const juce::Colour good       { 0xff68d391 };
}

// Spettro: tuo mix vs riferimento a parità di loudness, più la differenza.
class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (const Layout& l) : layout (l) {}
    void setData (const mc::Snapshot& mix, const mc::Snapshot* ref);
    void paint (juce::Graphics&) override;

private:
    float xFor (double hz, juce::Rectangle<float> area) const;
    const Layout& layout;
    mc::Snapshot mix;
    mc::Snapshot ref;
    bool hasRef = false;
};

// Tabella dei numeri: loudness, picchi, compressione, stereo. Tuo mix | riferimento.
class MetersView : public juce::Component
{
public:
    explicit MetersView (const Layout& l) : layout (l) {}
    void setData (const mc::Snapshot& mix, const mc::Snapshot* ref);
    void paint (juce::Graphics&) override;

private:
    const Layout& layout;
    mc::Snapshot mix;
    mc::Snapshot ref;
    bool hasRef = false;
};

// "Cosa sta succedendo": le frasi del confronto.
class FindingsView : public juce::Component
{
public:
    explicit FindingsView (const Layout& l) : layout (l) {}
    void setFindings (std::vector<mc::Finding> f) { findings = std::move (f); repaint(); }
    void paint (juce::Graphics&) override;

private:
    const Layout& layout;
    std::vector<mc::Finding> findings;
};
