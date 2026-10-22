#pragma once
#include <JuceHeader.h>

// ID dei parametri: CONGELATI (rinominarli rompe l'automazione nei progetti salvati).
namespace pid
{
    inline constexpr const char* oscWave     = "oscWave";
    inline constexpr const char* oscTune     = "oscTune";
    inline constexpr const char* osc2Detune  = "osc2Detune";
    inline constexpr const char* glide       = "glide";
    inline constexpr const char* oscSub      = "oscSub";
    inline constexpr const char* oscNoise    = "oscNoise";
    inline constexpr const char* fold        = "fold";
    inline constexpr const char* drift       = "drift";

    inline constexpr const char* cutoff      = "cutoff";
    inline constexpr const char* reso        = "reso";
    inline constexpr const char* filterEnv   = "filterEnv";
    inline constexpr const char* filterDrive = "filterDrive";

    inline constexpr const char* attack      = "attack";
    inline constexpr const char* decay       = "decay";
    inline constexpr const char* sustain     = "sustain";
    inline constexpr const char* drone       = "drone";

    inline constexpr const char* lfoRate     = "lfoRate";
    inline constexpr const char* lfoShape    = "lfoShape";
    inline constexpr const char* lfoPitch    = "lfoPitch";
    inline constexpr const char* lfoCutoff   = "lfoCutoff";
    inline constexpr const char* lfoFold     = "lfoFold";
    inline constexpr const char* chaos       = "chaos";

    inline constexpr const char* seqOn       = "seqOn";
    inline constexpr const char* seqRate     = "seqRate";
    inline constexpr const char* seqScale    = "seqScale";
    inline constexpr const char* seqLength   = "seqLength";
    inline constexpr const char* seqProb     = "seqProb";
    inline constexpr const char* seqMutate   = "seqMutate";
    inline constexpr const char* seqRatchet  = "seqRatchet";
    inline constexpr const char* seqSwing    = "seqSwing";

    inline constexpr int numSteps = 8;
    inline const juce::String step (int index) { return "step" + juce::String (index + 1); }

    inline constexpr const char* delayTime   = "delayTime";
    inline constexpr const char* delayFb     = "delayFb";
    inline constexpr const char* delayMix    = "delayMix";
    inline constexpr const char* spaceSize   = "spaceSize";
    inline constexpr const char* spaceMix    = "spaceMix";
    inline constexpr const char* width       = "width";
    inline constexpr const char* volume      = "volume";
}

namespace choices
{
    inline const juce::StringArray lfoShapes { "Sine", "Triangle", "Ramp", "Square", "S&H", "Drift" };
    inline const juce::StringArray seqRates  { "1/2", "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" };
    inline constexpr double seqRatePpq[]     { 2.0, 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
    inline const juce::StringArray scales    { "Chromatic", "Major", "Minor", "Dorian", "Phrygian",
                                               "Pent. Minor", "Whole Tone", "Hirajoshi" };
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
