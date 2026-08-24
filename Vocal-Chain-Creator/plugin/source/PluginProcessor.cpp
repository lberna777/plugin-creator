#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <BinaryData.h>

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
    juce::String promptProperty  = "promptText";
    juce::String profileProperty = "sourceProfile";

    std::unique_ptr<juce::AudioParameterFloat> floatParam (juce::String id, juce::String name,
                                                           float min, float max, float def,
                                                           juce::String unit = {}, float skew = 1.0f)
    {
        juce::NormalisableRange<float> range (min, max);
        range.setSkewForCentre (skew > 0.0f && skew != 1.0f ? skew : (min + max) * 0.5f);
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, def,
            juce::AudioParameterFloatAttributes().withLabel (unit));
    }

    std::unique_ptr<juce::AudioParameterBool> boolParam (juce::String id, juce::String name, bool def)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def);
    }

    std::unique_ptr<juce::AudioParameterChoice> choiceParam (juce::String id, juce::String name,
                                                             juce::StringArray choices, int def)
    {
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, def);
    }
}

//==============================================================================
APVTS::ParameterLayout VocalForgeProcessor::createLayout()
{
    APVTS::ParameterLayout layout;

    // globali
    layout.add (floatParam ("inTrim",  "Input Trim", -24.0f, 24.0f, 0.0f, "dB"),
                boolParam  ("polarity", "Polarity", false),
                floatParam ("outGain", "Output", -24.0f, 12.0f, 0.0f, "dB"),
                floatParam ("mix",     "Mix", 0.0f, 100.0f, 100.0f, "%"),
                floatParam ("intensity", "Intensity", 0.0f, 100.0f, 60.0f, "%"));

    // gate
    layout.add (boolParam ("gateOn", "Gate", true),
                floatParam ("gateThresh", "Gate Threshold", -80.0f, 0.0f, -40.0f, "dB"),
                floatParam ("gateRange", "Gate Range", 0.0f, 40.0f, 12.0f, "dB"),
                floatParam ("gateAtk", "Gate Attack", 0.1f, 20.0f, 2.0f, "ms", 3.0f),
                floatParam ("gateRel", "Gate Release", 10.0f, 1000.0f, 150.0f, "ms", 150.0f),
                floatParam ("gateKeyLo", "Key HP", 60.0f, 400.0f, 120.0f, "Hz", 150.0f),
                floatParam ("gateKeyHi", "Key LP", 1000.0f, 12000.0f, 4000.0f, "Hz", 4000.0f));

    // hpf
    layout.add (boolParam ("hpfOn", "HPF", true),
                floatParam ("hpfFreq", "HPF Freq", 40.0f, 200.0f, 90.0f, "Hz", 90.0f),
                choiceParam ("hpfSlope", "HPF Slope", { "12 dB/oct", "24 dB/oct" }, 1));

    // room tamer
    layout.add (boolParam ("roomOn", "Room Tamer", true),
                floatParam ("roomThresh", "Room Threshold", -60.0f, 0.0f, -28.0f, "dB"));
    for (int band = 1; band <= 3; ++band)
    {
        const auto b = juce::String (band);
        layout.add (floatParam ("room" + b + "Freq", "Room " + b + " Freq", 80.0f, 800.0f, 92.0f * static_cast<float> (band), "Hz", 200.0f),
                    floatParam ("room" + b + "Q", "Room " + b + " Q", 1.0f, 12.0f, 5.0f),
                    floatParam ("room" + b + "Depth", "Room " + b + " Depth", -12.0f, 0.0f, -4.0f, "dB"));
    }

    // eq sottrattiva
    layout.add (boolParam ("eqSubOn", "EQ Sottrattiva", true));
    const float subDefaults[3] { 240.0f, 1000.0f, 2800.0f };
    for (int band = 1; band <= 3; ++band)
    {
        const auto b = juce::String (band);
        layout.add (floatParam ("sub" + b + "Freq", "Sub " + b + " Freq", 60.0f, 8000.0f, subDefaults[band - 1], "Hz", 800.0f),
                    floatParam ("sub" + b + "Q", "Sub " + b + " Q", 0.4f, 8.0f, 2.0f),
                    floatParam ("sub" + b + "Gain", "Sub " + b + " Gain", -12.0f, 0.0f, -2.0f, "dB"));
    }

    // de-esser 1
    layout.add (boolParam ("ds1On", "De-esser 1", true),
                floatParam ("ds1Freq", "DS1 Freq", 3000.0f, 12000.0f, 6650.0f, "Hz", 7000.0f),
                floatParam ("ds1Thresh", "DS1 Threshold", -40.0f, 0.0f, -20.0f, "dB"),
                floatParam ("ds1Range", "DS1 Range", 0.0f, 12.0f, 6.0f, "dB"),
                choiceParam ("ds1Mode", "DS1 Mode", { "split", "wide" }, 0));

    // compressori
    layout.add (boolParam ("c1On", "Compressore 1", true),
                floatParam ("c1Thresh", "C1 Threshold", -40.0f, 0.0f, -20.0f, "dB"),
                floatParam ("c1Ratio", "C1 Ratio", 1.0f, 10.0f, 3.0f, ":1"),
                floatParam ("c1Atk", "C1 Attack", 0.1f, 30.0f, 8.0f, "ms", 5.0f),
                floatParam ("c1Rel", "C1 Release", 10.0f, 300.0f, 90.0f, "ms", 90.0f),
                floatParam ("c1Knee", "C1 Knee", 0.0f, 12.0f, 6.0f, "dB"),
                floatParam ("c1Makeup", "C1 Makeup", 0.0f, 18.0f, 3.0f, "dB"));

    layout.add (boolParam ("c2On", "Compressore 2", true),
                floatParam ("c2Thresh", "C2 Threshold", -40.0f, 0.0f, -20.0f, "dB"),
                floatParam ("c2Ratio", "C2 Ratio", 1.0f, 6.0f, 2.0f, ":1"),
                floatParam ("c2Atk", "C2 Attack", 5.0f, 100.0f, 30.0f, "ms", 30.0f),
                floatParam ("c2Rel", "C2 Release", 50.0f, 1000.0f, 250.0f, "ms", 250.0f),
                floatParam ("c2Knee", "C2 Knee", 0.0f, 12.0f, 8.0f, "dB"),
                floatParam ("c2Makeup", "C2 Makeup", 0.0f, 18.0f, 2.0f, "dB"));

    // saturazione
    layout.add (boolParam ("satOn", "Saturazione", true),
                floatParam ("satDrive", "Drive", 0.0f, 100.0f, 30.0f, "%"),
                choiceParam ("satType", "Sat Type", { "tape", "tube", "transistor" }, 0),
                floatParam ("satTilt", "Sat Tilt", -6.0f, 6.0f, 0.0f, "dB"));

    // eq tonale
    layout.add (boolParam ("eqToneOn", "EQ Tonale", true),
                boolParam ("airOn", "Air", false),
                floatParam ("airGain", "Air Gain", 0.0f, 6.0f, 0.0f, "dB"));
    const float toneDefaults[3] { 180.0f, 3200.0f, 8000.0f };
    for (int band = 1; band <= 3; ++band)
    {
        const auto b = juce::String (band);
        layout.add (floatParam ("tone" + b + "Freq", "Tone " + b + " Freq", 80.0f, 16000.0f, toneDefaults[band - 1], "Hz", 1500.0f),
                    floatParam ("tone" + b + "Q", "Tone " + b + " Q", 0.4f, 6.0f, 1.0f),
                    floatParam ("tone" + b + "Gain", "Tone " + b + " Gain", -9.0f, 9.0f, 0.0f, "dB"));
    }

    // de-esser 2
    layout.add (boolParam ("ds2On", "De-esser 2", true),
                floatParam ("ds2Freq", "DS2 Freq", 3000.0f, 12000.0f, 7350.0f, "Hz", 7000.0f),
                floatParam ("ds2Thresh", "DS2 Threshold", -40.0f, 0.0f, -16.0f, "dB"),
                floatParam ("ds2Range", "DS2 Range", 0.0f, 12.0f, 5.0f, "dB"),
                choiceParam ("ds2Mode", "DS2 Mode", { "split", "wide" }, 1));

    // limiter
    layout.add (boolParam ("limOn", "Limiter", true),
                floatParam ("limCeiling", "Ceiling", -6.0f, 0.0f, -1.0f, "dBFS"),
                floatParam ("limRelease", "Lim Release", 10.0f, 500.0f, 100.0f, "ms", 100.0f));

    // mandate — bus paralleli, mai in serie
    layout.add (choiceParam ("sendsMode", "Sends", { "internal", "logic" }, 0));
    layout.add (boolParam ("revOn", "Reverb Send", true),
                choiceParam ("revVariant", "Reverb", { "none", "ambience", "room", "hall", "plate" }, 4),
                floatParam ("revDecay", "Rev Decay", 0.2f, 4.0f, 1.4f, "s", 1.5f),
                floatParam ("revPredelay", "Rev Predelay", 0.0f, 80.0f, 20.0f, "ms"),
                floatParam ("revSize", "Rev Size", 10.0f, 100.0f, 55.0f, "%"),
                floatParam ("revHpf", "Rev HP", 100.0f, 900.0f, 300.0f, "Hz", 350.0f),
                floatParam ("revLpf", "Rev LP", 2000.0f, 12000.0f, 7000.0f, "Hz", 6000.0f),
                floatParam ("revWidth", "Rev Width", 0.0f, 100.0f, 85.0f, "%"),
                floatParam ("revDuck", "Rev Duck", 0.0f, 9.0f, 5.0f, "dB"),
                floatParam ("revSend", "Rev Send", -40.0f, -6.0f, -24.0f, "dB"));

    layout.add (boolParam ("dlyOn", "Delay Send", true),
                choiceParam ("dlyVariant", "Delay", { "slap", "eighth", "quarter" }, 1),
                boolParam ("dlySync", "Delay Sync", true),
                floatParam ("dlyTime", "Delay Time", 60.0f, 1500.0f, 375.0f, "ms", 400.0f),
                choiceParam ("dlyDivision", "Division", { "1/4", "1/8 dotted", "1/8", "1/16" }, 1),
                floatParam ("dlyFeedback", "Feedback", 0.0f, 45.0f, 25.0f, "%"),
                floatParam ("dlyHpf", "Delay HP", 100.0f, 900.0f, 350.0f, "Hz", 350.0f),
                floatParam ("dlyLpf", "Delay LP", 1500.0f, 8000.0f, 4000.0f, "Hz", 4000.0f),
                floatParam ("dlyDuck", "Delay Duck", 0.0f, 9.0f, 6.0f, "dB"),
                floatParam ("dlySend", "Delay Send Level", -40.0f, -8.0f, -24.0f, "dB"));

    return layout;
}

//==============================================================================
VocalForgeProcessor::VocalForgeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VOCAL_FORGE", createLayout())
{
    rulesEngine.loadFromJson (juce::String::fromUTF8 (BinaryData::rules_json, BinaryData::rules_jsonSize));

    promptState.setProperty (promptProperty, "", nullptr);
    promptState.setProperty (profileProperty, vf::RulesEngine::defaultProfileId(), nullptr);

    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            apvts.addParameterListener (withId->paramID, this);
}

VocalForgeProcessor::~VocalForgeProcessor()
{
    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            apvts.removeParameterListener (withId->paramID, this);
}

void VocalForgeProcessor::parameterChanged (const juce::String&, float)
{
    settingsDirty.store (true);
    if (! writingPreset.load() && chainGenerated.load())
        touchedByHand.store (true);
}

//==============================================================================
bool VocalForgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    return in.size() <= out.size();          // mono→stereo e stereo→stereo, mai il contrario
}

bool VocalForgeProcessor::isBusesLayoutSupported (const BusesProperties&) const { return true; }

void VocalForgeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    chain.prepare (sampleRate, samplesPerBlock, juce::jmax (getTotalNumOutputChannels(), 1));
    settingsDirty.store (true);

    // latenza DICHIARATA: solo il lookahead del limiter. Logic compensa solo se gliela dici.
    const auto latencyMs = lastPreset.declaredLatencyMs > 0.0 ? lastPreset.declaredLatencyMs : 2.0;
    setLatencySamples (static_cast<int> (sampleRate * latencyMs * 0.001));
}

vf::ChainSettings VocalForgeProcessor::currentSettings() const
{
    auto value = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    auto flag  = [&value] (const char* id) { return value (id) > 0.5f; };

    vf::ChainSettings s;
    s.inTrim = value ("inTrim");  s.polarity = flag ("polarity");
    s.outGain = value ("outGain"); s.mix = value ("mix");

    s.gateOn = flag ("gateOn");
    s.gateThresh = value ("gateThresh"); s.gateRange = value ("gateRange");
    s.gateAtk = value ("gateAtk"); s.gateRel = value ("gateRel");
    s.gateKeyLo = value ("gateKeyLo"); s.gateKeyHi = value ("gateKeyHi");

    s.hpfOn = flag ("hpfOn"); s.hpfFreq = value ("hpfFreq");
    s.hpfSlope = value ("hpfSlope") > 0.5f ? 24 : 12;

    s.roomOn = flag ("roomOn"); s.roomThresh = value ("roomThresh");
    for (int band = 0; band < 3; ++band)
    {
        const auto b = juce::String (band + 1);
        s.roomFreq[band]  = value (("room" + b + "Freq").toRawUTF8());
        s.roomQ[band]     = value (("room" + b + "Q").toRawUTF8());
        s.roomDepth[band] = value (("room" + b + "Depth").toRawUTF8());
        s.subFreq[band]   = value (("sub" + b + "Freq").toRawUTF8());
        s.subQ[band]      = value (("sub" + b + "Q").toRawUTF8());
        s.subGain[band]   = value (("sub" + b + "Gain").toRawUTF8());
        s.toneFreq[band]  = value (("tone" + b + "Freq").toRawUTF8());
        s.toneQ[band]     = value (("tone" + b + "Q").toRawUTF8());
        s.toneGain[band]  = value (("tone" + b + "Gain").toRawUTF8());
    }
    s.eqSubOn = flag ("eqSubOn"); s.eqToneOn = flag ("eqToneOn");
    s.airOn = flag ("airOn"); s.airGain = value ("airGain");

    s.ds1On = flag ("ds1On"); s.ds1Freq = value ("ds1Freq");
    s.ds1Thresh = value ("ds1Thresh"); s.ds1Range = value ("ds1Range");
    s.ds1Split = value ("ds1Mode") < 0.5f;
    s.ds2On = flag ("ds2On"); s.ds2Freq = value ("ds2Freq");
    s.ds2Thresh = value ("ds2Thresh"); s.ds2Range = value ("ds2Range");
    s.ds2Split = value ("ds2Mode") < 0.5f;

    s.c1On = flag ("c1On"); s.c1Thresh = value ("c1Thresh"); s.c1Ratio = value ("c1Ratio");
    s.c1Atk = value ("c1Atk"); s.c1Rel = value ("c1Rel"); s.c1Knee = value ("c1Knee"); s.c1Makeup = value ("c1Makeup");
    s.c2On = flag ("c2On"); s.c2Thresh = value ("c2Thresh"); s.c2Ratio = value ("c2Ratio");
    s.c2Atk = value ("c2Atk"); s.c2Rel = value ("c2Rel"); s.c2Knee = value ("c2Knee"); s.c2Makeup = value ("c2Makeup");

    s.satOn = flag ("satOn"); s.satDrive = value ("satDrive");
    s.satTilt = value ("satTilt"); s.satType = static_cast<int> (value ("satType"));

    s.limOn = flag ("limOn"); s.limCeiling = value ("limCeiling"); s.limRelease = value ("limRelease");

    // in modo "logic" i bus interni si spengono: le mandate le fa Logic
    const bool internalSends = value ("sendsMode") < 0.5f;
    s.revOn = internalSends && flag ("revOn");
    s.revDecay = value ("revDecay"); s.revPredelay = value ("revPredelay"); s.revSize = value ("revSize");
    s.revHpf = value ("revHpf"); s.revLpf = value ("revLpf"); s.revWidth = value ("revWidth");
    s.revDuck = value ("revDuck"); s.revSend = value ("revSend");

    s.dlyOn = internalSends && flag ("dlyOn");
    s.dlySync = flag ("dlySync"); s.dlyTime = value ("dlyTime");
    s.dlyFeedback = value ("dlyFeedback"); s.dlyHpf = value ("dlyHpf"); s.dlyLpf = value ("dlyLpf");
    s.dlyDuck = value ("dlyDuck"); s.dlySend = value ("dlySend");
    return s;
}

void VocalForgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto totalIn  = getTotalNumInputChannels();
    const auto totalOut = getTotalNumOutputChannels();

    // mono in / stereo out: si duplica, non si esce a mani vuote (lezione di NEMO)
    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.copyFrom (ch, 0, buffer, juce::jmin (ch - 1, totalIn - 1), 0, buffer.getNumSamples());

    if (settingsDirty.exchange (false))
        chain.setSettings (currentSettings());

    double bpm = 120.0;
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (auto hostBpm = position->getBpm())
                bpm = *hostBpm;

    chain.process (buffer, bpm);
}

void VocalForgeProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const auto totalIn  = getTotalNumInputChannels();
    for (int ch = totalIn; ch < getTotalNumOutputChannels(); ++ch)
        buffer.copyFrom (ch, 0, buffer, juce::jmin (ch - 1, totalIn - 1), 0, buffer.getNumSamples());
}

//==============================================================================
void VocalForgeProcessor::writePresetToParameters (const vf::Preset& preset)
{
    writingPreset.store (true);

    auto setValue = [this] (const juce::String& id, float value)
    {
        if (auto* parameter = apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };
    auto setChoice = [this] (const juce::String& id, const juce::String& text)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
        {
            const auto index = choice->choices.indexOf (text, true);
            if (index >= 0) choice->setValueNotifyingHost (choice->convertTo0to1 (static_cast<float> (index)));
        }
    };

    for (const auto& module : preset.modules)
    {
        // ogni modulo ha il suo interruttore: <id>On, tranne i due che non lo hanno
        static const std::map<juce::String, juce::String> switchIds {
            { "gate", "gateOn" },   { "hpf", "hpfOn" },     { "room", "roomOn" },
            { "eqSub", "eqSubOn" }, { "ds1", "ds1On" },     { "comp1", "c1On" },
            { "comp2", "c2On" },    { "sat", "satOn" },     { "eqTone", "eqToneOn" },
            { "ds2", "ds2On" },     { "limiter", "limOn" }
        };
        const auto it = switchIds.find (module.id);
        if (it != switchIds.end()) setValue (it->second, module.enabled ? 1.0f : 0.0f);
        if (! module.enabled) continue;

        for (const auto& param : module.params)
        {
            if (param.value.isString())
            {
                const auto id = (param.id == "ds1Mode" || param.id == "ds2Mode" || param.id == "satType")
                                ? param.id : param.id;
                setChoice (id, param.value.toString());
            }
            else if (param.id == "hpfSlope")
            {
                setValue ("hpfSlope", param.asDouble() >= 24.0 ? 1.0f : 0.0f);
            }
            else if (param.value.isBool())
            {
                setValue (param.id, param.asBool() ? 1.0f : 0.0f);
            }
            else
            {
                setValue (param.id, static_cast<float> (param.asDouble()));
            }
        }
    }

    // mandate: una per gruppo, e se il gruppo è vuoto il bus si spegne
    const auto* reverb = preset.sendOfGroup ("reverb");
    setValue ("revOn", reverb != nullptr ? 1.0f : 0.0f);
    if (reverb != nullptr)
    {
        setChoice ("revVariant", reverb->id.fromFirstOccurrenceOf ("rev_", false, false));
        auto get = [reverb] (const char* id, float fallback)
        {
            const auto* found = reverb->find (id);
            return found != nullptr ? static_cast<float> (found->asDouble()) : fallback;
        };
        setValue ("revDecay", get ("decay_s", 1.4f));
        setValue ("revPredelay", get ("predelay", 20.0f));
        setValue ("revSize", get ("size_pct", 55.0f));
        setValue ("revHpf", get ("hpf", 300.0f));
        setValue ("revLpf", get ("lpf", 7000.0f));
        setValue ("revWidth", get ("width_pct", 85.0f));
        setValue ("revDuck", get ("duck_db", 5.0f));
        setValue ("revSend", get ("send_db", -24.0f));
    }

    const auto* delay = preset.sendOfGroup ("delay");
    setValue ("dlyOn", delay != nullptr ? 1.0f : 0.0f);
    if (delay != nullptr)
    {
        setChoice ("dlyVariant", delay->id.fromFirstOccurrenceOf ("dly_", false, false));
        auto get = [delay] (const char* id, float fallback)
        {
            const auto* found = delay->find (id);
            return found != nullptr ? static_cast<float> (found->asDouble()) : fallback;
        };
        if (const auto* sync = delay->find ("sync")) setValue ("dlySync", sync->asBool() ? 1.0f : 0.0f);
        if (const auto* division = delay->find ("division")) setChoice ("dlyDivision", division->value.toString());
        setValue ("dlyTime", get ("time_ms", 375.0f));
        setValue ("dlyFeedback", get ("feedback", 25.0f));
        setValue ("dlyHpf", get ("hpf", 350.0f));
        setValue ("dlyLpf", get ("lpf", 4000.0f));
        setValue ("dlyDuck", get ("duck_db", 6.0f));
        setValue ("dlySend", get ("send_db", -24.0f));
    }

    setValue ("outGain", [&preset]
    {
        if (const auto* out = preset.find ("out"))
            if (const auto* gain = out->find ("outGain"))
                return static_cast<float> (gain->asDouble());
        return 0.0f;
    }());

    writingPreset.store (false);
    settingsDirty.store (true);
}

void VocalForgeProcessor::applyPrompt (const juce::String& prompt, const juce::String& profileId)
{
    if (! rulesEngine.isLoaded()) return;

    promptState.setProperty (promptProperty, prompt, nullptr);
    promptState.setProperty (profileProperty, profileId, nullptr);

    lastPreset = rulesEngine.compile (prompt, profileId);
    writePresetToParameters (lastPreset);
    chainGenerated.store (true);
    touchedByHand.store (false);

    if (getSampleRate() > 0.0)      // fuori da un host la latenza la fissa prepareToPlay
        setLatencySamples (static_cast<int> (getSampleRate() * lastPreset.declaredLatencyMs * 0.001));
    if (onPresetGenerated) onPresetGenerated();
}

juce::String VocalForgeProcessor::getPrompt() const
{
    return promptState.getProperty (promptProperty, "").toString();
}

juce::String VocalForgeProcessor::getProfileId() const
{
    return promptState.getProperty (profileProperty, vf::RulesEngine::defaultProfileId()).toString();
}

juce::String VocalForgeProcessor::getLogicSendsRecipe() const
{
    juce::String text;
    text << "VOCAL FORGE — mandate da creare in Logic (bus aux, MAI in serie sulla voce)" << juce::newLine << juce::newLine;
    if (lastPreset.sends.empty())
    {
        text << "Nessuna mandata per questo prompt: la voce resta asciutta." << juce::newLine;
        return text;
    }
    for (const auto& send : lastPreset.sends)
    {
        text << send.label << " — " << send.pluginLogic << juce::newLine
             << "  " << send.why << juce::newLine;
        for (const auto& setting : send.settings)
            text << "    " << setting.id << " = " << setting.value.toString()
                 << (setting.unit.isNotEmpty() ? " " + setting.unit : juce::String()) << juce::newLine;
        text << juce::newLine;
    }
    return text;
}

//==============================================================================
void VocalForgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.appendChild (promptState.createCopy(), nullptr);      // il prompt fa parte dello stato
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VocalForgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid()) return;

        auto storedPrompt = state.getChildWithName ("vfPrompt");
        if (storedPrompt.isValid())
        {
            promptState = storedPrompt.createCopy();
            state.removeChild (storedPrompt, nullptr);
            chainGenerated.store (getPrompt().isNotEmpty());
            if (chainGenerated.load() && rulesEngine.isLoaded())
                lastPreset = rulesEngine.compile (getPrompt(), getProfileId());
        }

        apvts.replaceState (state);
        settingsDirty.store (true);
        if (onPresetGenerated) onPresetGenerated();
    }
}

juce::AudioProcessorEditor* VocalForgeProcessor::createEditor()
{
    return new VocalForgeEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalForgeProcessor();
}
