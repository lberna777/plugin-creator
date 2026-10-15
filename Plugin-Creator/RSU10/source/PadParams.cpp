#include "PadParams.h"

namespace rsu
{
juce::String formatValue (PP p, float v)
{
    const auto& d = def (p);
    if (d.choices != nullptr)
        return d.choices[juce::jlimit (0, d.numChoices - 1, juce::roundToInt (v))];

    if (p == PP::Volume)
        return v <= -59.9f ? juce::String ("-INF") : juce::String (v, 1) + " dB";
    if (p == PP::Pan)
    {
        if (std::abs (v) < 0.01f) return "C";
        return (v < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (v) * 100.0f));
    }
    if (p == PP::Tune)
        return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st";
    if (p == PP::Fine)
        return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " ct";
    if (p == PP::Attack || p == PP::Decay || p == PP::Release || p == PP::FadeIn || p == PP::FadeOut)
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, v < 10.0f ? 1 : 0) + " ms";
    if (p == PP::Cutoff)
        return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    if (p == PP::Split2 || p == PP::Split3 || p == PP::Split4)
        return juce::String (juce::roundToInt (v));
    return juce::String (juce::roundToInt (v * 100.0f)) + "%";
}
} // namespace rsu
