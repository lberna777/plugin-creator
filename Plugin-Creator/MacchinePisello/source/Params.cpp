#include "Params.h"

namespace
{
    using Attr = juce::AudioParameterFloatAttributes;

    juce::String pct (float v, int)   { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
    juce::String semis (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 2) + " st"; }
    juce::String bip (float v, int)   { return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
    juce::String ms (float v, int)    { return v < 1000.0f ? juce::String (v, v < 10.0f ? 1 : 0) + " ms"
                                                           : juce::String (v / 1000.0f, 2) + " s"; }
    juce::String hz (float v, int)    { return v < 1000.0f ? juce::String (v, v < 10.0f ? 2 : 0) + " Hz"
                                                           : juce::String (v / 1000.0f, 2) + " kHz"; }
    juce::String db (float v, int)    { return v <= -59.9f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; }

    // Range logaritmico con skew calcolato dal centro desiderato.
    juce::NormalisableRange<float> logRange (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    void addFloat (Layout& l, const juce::String& id, const juce::String& name,
                   juce::NormalisableRange<float> range, float def,
                   juce::String (*toText) (float, int))
    {
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def,
                                                            Attr().withStringFromValueFunction (toText)));
    }

    void addUnit (Layout& l, const juce::String& id, const juce::String& name, float def)
    {
        addFloat (l, id, name, { 0.0f, 1.0f }, def, pct);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    Layout l;

    // OSC
    addUnit  (l, pid::oscWave,    "Wave",    0.40f);
    addFloat (l, pid::oscTune,    "Tune",    { -24.0f, 24.0f }, 0.0f, semis);
    addFloat (l, pid::osc2Detune, "Osc 2",   { -12.0f, 12.0f }, 0.07f, semis);
    addFloat (l, pid::glide,      "Glide",   logRange (0.0f, 2000.0f, 150.0f), 25.0f, ms);
    addUnit  (l, pid::oscSub,     "Sub",     0.30f);
    addUnit  (l, pid::oscNoise,   "Noise",   0.04f);
    addUnit  (l, pid::fold,       "Fold",    0.15f);
    addUnit  (l, pid::drift,      "Drift",   0.35f);

    // FILTER
    addFloat (l, pid::cutoff,      "Cutoff",   logRange (20.0f, 18000.0f, 800.0f), 700.0f, hz);
    addUnit  (l, pid::reso,        "Reso",     0.45f);
    addFloat (l, pid::filterEnv,   "Env Amt",  { -1.0f, 1.0f }, 0.50f, bip);
    addUnit  (l, pid::filterDrive, "Drive",    0.30f);

    // ENV / VCA
    addFloat (l, pid::attack,  "Attack",  logRange (0.5f, 5000.0f, 80.0f), 4.0f, ms);
    addFloat (l, pid::decay,   "Decay",   logRange (10.0f, 10000.0f, 600.0f), 420.0f, ms);
    addUnit  (l, pid::sustain, "Sustain", 0.0f);
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { pid::drone, 1 }, "Drone", false));

    // MOD
    addFloat (l, pid::lfoRate, "LFO Rate", logRange (0.01f, 30.0f, 1.0f), 0.25f, hz);
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid::lfoShape, 1 }, "LFO Shape",
                                                         choices::lfoShapes, 0));
    addUnit  (l, pid::lfoPitch,  "LFO > Pitch",  0.0f);
    addUnit  (l, pid::lfoCutoff, "LFO > Cutoff", 0.30f);
    addUnit  (l, pid::lfoFold,   "LFO > Fold",   0.20f);
    addUnit  (l, pid::chaos,     "Chaos",        0.15f);

    // SEQUENCER
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { pid::seqOn, 1 }, "Seq Run", true));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid::seqRate, 1 }, "Seq Rate",
                                                         choices::seqRates, 3));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid::seqScale, 1 }, "Seq Scale",
                                                         choices::scales, 2));
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { pid::seqLength, 1 }, "Seq Length",
                                                      1, pid::numSteps, pid::numSteps));
    addUnit  (l, pid::seqProb,    "Probability", 0.85f);
    addUnit  (l, pid::seqMutate,  "Mutate",      0.10f);
    addUnit  (l, pid::seqRatchet, "Ratchet",     0.10f);
    addFloat (l, pid::seqSwing,   "Swing", { 0.0f, 0.6f }, 0.0f, pct);

    const float stepDefaults[pid::numSteps] { 0.0f, 0.0f, 7.0f, 0.0f, 12.0f, 3.0f, 0.0f, -5.0f };
    for (int i = 0; i < pid::numSteps; ++i)
        addFloat (l, pid::step (i), "Step " + juce::String (i + 1), { -12.0f, 12.0f }, stepDefaults[i], semis);

    // FX / OUT
    addFloat (l, pid::delayTime, "Delay Time",     logRange (10.0f, 2000.0f, 300.0f), 375.0f, ms);
    addFloat (l, pid::delayFb,   "Delay Feedback", { 0.0f, 0.97f }, 0.45f, pct);
    addUnit  (l, pid::delayMix,  "Delay Mix",      0.25f);
    addUnit  (l, pid::spaceSize, "Space",          0.70f);
    addUnit  (l, pid::spaceMix,  "Reverb Mix",     0.35f);
    addUnit  (l, pid::width,     "Width",          0.60f);
    addFloat (l, pid::volume,    "Volume",         { -60.0f, 6.0f }, -8.0f, db);

    return l;
}
