#pragma once
#include <JuceHeader.h>

// ID dei parametri: CONGELATI dopo il primo rilascio (rinominarli rompe automazioni e preset).
namespace ParamID
{
inline constexpr const char* input   = "input";    // livello di registrazione (dB)
inline constexpr const char* tape    = "tape";     // Type I / II / IV
inline constexpr const char* speed   = "speed";    // 4,76 / 9,5 cm/s
inline constexpr const char* bias    = "bias";     // sotto/sovra-polarizzazione (%)
inline constexpr const char* wow     = "wow";      // %
inline constexpr const char* flutter = "flutter";  // %
inline constexpr const char* hiss    = "hiss";     // %
inline constexpr const char* age     = "age";      // usura del nastro (%)
inline constexpr const char* output  = "output";   // dB
inline constexpr const char* autoGain= "autogain"; // compensa l'input sull'output
inline constexpr const char* mix     = "mix";      // %
inline constexpr const char* bypass  = "bypass";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using F = AudioParameterFloat;
    using C = AudioParameterChoice;
    using B = AudioParameterBool;
    using A = AudioParameterFloatAttributes;

    auto dB  = A().withLabel ("dB");
    auto pct = A().withLabel ("%");

    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back (std::make_unique<F> (ParameterID { ParamID::input, 1 },   "Record Level", NormalisableRange<float> (-18.f, 18.f, 0.1f), 0.f, dB));
    p.push_back (std::make_unique<C> (ParameterID { ParamID::tape, 1 },    "Tape", StringArray { "Type I", "Type II", "Type IV" }, 1));
    p.push_back (std::make_unique<C> (ParameterID { ParamID::speed, 1 },   "Speed", StringArray { "4.76 cm/s", "9.5 cm/s" }, 0));
    p.push_back (std::make_unique<F> (ParameterID { ParamID::bias, 1 },    "Bias", NormalisableRange<float> (-100.f, 100.f, 0.1f), 0.f, pct));
    p.push_back (std::make_unique<F> (ParameterID { ParamID::wow, 1 },     "Wow", NormalisableRange<float> (0.f, 100.f, 0.1f), 20.f, pct));
    p.push_back (std::make_unique<F> (ParameterID { ParamID::flutter, 1 }, "Flutter", NormalisableRange<float> (0.f, 100.f, 0.1f), 20.f, pct));
    p.push_back (std::make_unique<F> (ParameterID { ParamID::hiss, 1 },    "Hiss", NormalisableRange<float> (0.f, 100.f, 0.1f), 30.f, pct));
    p.push_back (std::make_unique<F> (ParameterID { ParamID::age, 1 },     "Age", NormalisableRange<float> (0.f, 100.f, 0.1f), 10.f, pct));
    p.push_back (std::make_unique<F> (ParameterID { ParamID::output, 1 },  "Output", NormalisableRange<float> (-18.f, 18.f, 0.1f), 0.f, dB));
    p.push_back (std::make_unique<B> (ParameterID { ParamID::autoGain, 1 },"Auto Gain", true));
    p.push_back (std::make_unique<F> (ParameterID { ParamID::mix, 1 },     "Mix", NormalisableRange<float> (0.f, 100.f, 0.1f), 100.f, pct));
    p.push_back (std::make_unique<B> (ParameterID { ParamID::bypass, 1 },  "Bypass", false));
    return { p.begin(), p.end() };
}
