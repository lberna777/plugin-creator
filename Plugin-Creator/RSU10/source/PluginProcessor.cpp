#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace rsu;

namespace
{
juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::master, 1 }, "Volume", NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.5f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::ribbon, 1 }, "Ribbon", NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::ribbonTouch, 1 }, "Ribbon tocco", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::ribbonMode, 1 }, "Ribbon modo",
                                                   StringArray { "Scratch", "Pitch", "Filtro", "Reverse", "Crossfade" }, 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::ribbonHold, 1 }, "Ribbon hold", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::ribbonRange, 1 }, "Ribbon pitch",
                                                  NormalisableRange<float> (1.0f, 24.0f, 1.0f), 12.0f, AudioParameterFloatAttributes().withLabel ("st")));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::scale, 1 }, "Scala", false));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { ParamID::scaleRoot, 1 }, "Scala radice", 0, 127, 60));
    return l;
}

} // namespace

juce::AudioProcessor::BusesProperties RSU10AudioProcessor::buses()
{
    auto b = BusesProperties()
                 .withInput ("Ingresso", juce::AudioChannelSet::stereo(), true)
                 .withOutput ("Main", juce::AudioChannelSet::stereo(), true);
    for (int i = 2; i <= kNumOutputs; ++i)
        b = b.withOutput ("Out " + juce::String (i), juce::AudioChannelSet::stereo(), false);
    return b;
}

namespace
{
constexpr const char* kStateTag = "RSU10";
constexpr const char* kKitTag = "KIT";
constexpr const char* kKitExtension = ".rsu10kit";
} // namespace

RSU10AudioProcessor::RSU10AudioProcessor()
    : AudioProcessor (buses()), apvts (*this, nullptr, "PARAMS", createLayout())
{
    pMaster = apvts.getRawParameterValue (ParamID::master);
    pRibbon = apvts.getRawParameterValue (ParamID::ribbon);
    pTouch  = apvts.getRawParameterValue (ParamID::ribbonTouch);
    pMode   = apvts.getRawParameterValue (ParamID::ribbonMode);
    pHold   = apvts.getRawParameterValue (ParamID::ribbonHold);
    pRange  = apvts.getRawParameterValue (ParamID::ribbonRange);
    pScale  = apvts.getRawParameterValue (ParamID::scale);
    pRoot   = apvts.getRawParameterValue (ParamID::scaleRoot);

    browserFolder = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    loadDemoKit();
    startTimerHz (10);
}

RSU10AudioProcessor::~RSU10AudioProcessor()
{
    stopTimer();
    loader.removeAllJobs (true, 5000);
}

// ---------------------------------------------------------------- audio

bool RSU10AudioProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto& main = layout.getMainOutputChannelSet();
    if (main != juce::AudioChannelSet::stereo() && main != juce::AudioChannelSet::mono())
        return false;
    for (int i = 1; i < layout.outputBuses.size(); ++i)
    {
        const auto& set = layout.outputBuses[i];
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }
    for (const auto& in : layout.inputBuses)
        if (! in.isDisabled() && in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono())
            return false;
    return true;
}

void RSU10AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    inputCopy.setSize (2, samplesPerBlock);
    master.reset (sampleRate, 0.03);
    master.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pMaster->load(), -60.0f));
}

void RSU10AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    // Blocchi più grandi del previsto (alcuni host): si spezzano
    if (n > inputCopy.getNumSamples())
    {
        inputCopy.setSize (2, n, false, false, true);
        engine.prepare (getSampleRate(), n);
    }

    // L'ingresso condivide i canali con l'uscita: va copiato prima
    int inChannels = 0;
    if (getBusCount (true) > 0 && getBus (true, 0)->isEnabled())
    {
        auto in = getBusBuffer (buffer, true, 0);
        inChannels = juce::jmin (2, in.getNumChannels());
        for (int ch = 0; ch < inChannels; ++ch)
            inputCopy.copyFrom (ch, 0, in, ch, 0, n);
    }
    const float* inputs[2] { inputCopy.getReadPointer (0), inputCopy.getReadPointer (1) };

    rsu::OutputPointers out;
    for (int b = 0; b < getBusCount (false) && b < kNumOutputs; ++b)
    {
        if (! getBus (false, b)->isEnabled())
            continue;
        auto bus = getBusBuffer (buffer, false, b);
        if (bus.getNumChannels() == 0)
            continue;
        out.left[b] = bus.getWritePointer (0);
        out.right[b] = bus.getNumChannels() > 1 ? bus.getWritePointer (1) : nullptr;
    }
    // Canali in più non usati dai bus (alcuni host): silenzio
    for (int ch = getTotalNumOutputChannels(); ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    rsu::EngineGlobals g;
    g.ribbon = pRibbon->load();
    g.ribbonTouch = pTouch->load() > 0.5f;
    g.ribbonMode = RibbonMode (juce::roundToInt (pMode->load()));
    g.ribbonHold = pHold->load() > 0.5f;
    g.ribbonPitchRange = pRange->load();
    g.scale = pScale->load() > 0.5f;
    g.scaleRoot = juce::roundToInt (pRoot->load());
    g.scalePad = scalePad.load();

    master.setTargetValue (juce::Decibels::decibelsToGain (pMaster->load(), -60.0f));
    g.masterGain = master.getNextValue();
    master.skip (n - 1);

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                g.hostBpm = *bpm;

    engine.process (out, inChannels > 0 ? inputs : nullptr, inChannels, n, midi, g);
}

// ---------------------------------------------------------------- campioni

juce::File RSU10AudioProcessor::userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("RSU-10");
}

void RSU10AudioProcessor::loadSample (int pad, int layer, const juce::File& f)
{
    loader.addJob ([this, pad, layer, f]
    {
        juce::String err;
        if (auto s = Sample::loadFromFile (f, &err))
        {
            samples.set (pad, layer, s);
            if (layer == 0)
                pads.set (pad, PP::Start, 0.0f), pads.set (pad, PP::End, 1.0f);
            postMessage (padLabel (pad) + (layer > 0 ? " strato " + juce::String (layer + 1) : juce::String()) + ": " + s->name());
        }
        else
        {
            postMessage (err);
        }
    });
}

void RSU10AudioProcessor::loadSamplesFrom (int firstPad, const juce::Array<juce::File>& files)
{
    int pad = firstPad;
    for (const auto& f : files)
    {
        if (pad >= kNumPads)
            break;
        if (Sample::isAudioFile (f))
            loadSample (pad++, 0, f);
    }
}

void RSU10AudioProcessor::stepSampleInFolder (int pad, int delta)
{
    auto* s = samples.get (pad, 0);
    if (s == nullptr || ! s->file().existsAsFile())
    {
        postMessage ("Il pad non ha un file da cui partire");
        return;
    }
    auto siblings = s->file().getParentDirectory().findChildFiles (juce::File::findFiles, false);
    siblings.removeIf ([] (const juce::File& f) { return ! Sample::isAudioFile (f); });
    siblings.sort();
    const int i = siblings.indexOf (s->file());
    if (i < 0 || siblings.isEmpty())
        return;
    loadSample (pad, 0, siblings[(i + delta + siblings.size()) % siblings.size()]);
}

void RSU10AudioProcessor::clearPad (int pad)
{
    samples.clearPad (pad);
    pads.resetPad (pad);
}

void RSU10AudioProcessor::copyPad (int from, int to)
{
    if (from == to)
        return;
    for (int l = 0; l < kMaxLayers; ++l)
        samples.set (to, l, samples.get (from, l));
    for (int i = 0; i < kNumPP; ++i)
        pads.set (to, PP (i), pads.get (from, PP (i)));
}

void RSU10AudioProcessor::loadDemoKit()
{
    samples.clearAll();
    pads.resetAll();
    auto kit = makeDemoKit();
    for (size_t i = 0; i < kit.size() && i < kPadsPerBank; ++i)
        samples.set (int (i), 0, kit[i]);
    // charleston chiuso e aperto si zittiscono a vicenda, come su una batteria vera
    pads.set (3, PP::Choke, 1.0f);
    pads.set (4, PP::Choke, 1.0f);
    kitName = "KIT DI PROVA";
    kitFile = juce::File();
}

// ---------------------------------------------------------------- kit

juce::ValueTree RSU10AudioProcessor::kitToTree (const juce::File& relativeTo) const
{
    juce::ValueTree kit (kKitTag);
    kit.setProperty ("name", kitName, nullptr);
    kit.setProperty ("version", JucePlugin_VersionString, nullptr);

    for (int p = 0; p < kNumPads; ++p)
    {
        juce::ValueTree pad ("PAD");
        pad.setProperty ("index", p, nullptr);
        bool any = false;
        for (int i = 0; i < kNumPP; ++i)
        {
            const float v = pads.get (p, PP (i));
            if (! juce::exactlyEqual (v, ppDefs()[size_t (i)].def))
            {
                pad.setProperty (ppDefs()[size_t (i)].id, v, nullptr);
                any = true;
            }
        }
        for (int l = 0; l < kMaxLayers; ++l)
        {
            auto* s = samples.get (p, l);
            if (s == nullptr)
                continue;
            juce::ValueTree layer ("LAYER");
            layer.setProperty ("index", l, nullptr);
            if (s->demoIndex >= 0)
                layer.setProperty ("demo", s->demoIndex, nullptr);
            else if (relativeTo != juce::File() && s->file().isAChildOf (relativeTo))
                layer.setProperty ("file", s->file().getRelativePathFrom (relativeTo), nullptr);
            else
                layer.setProperty ("file", s->file().getFullPathName(), nullptr);
            pad.appendChild (layer, nullptr);
            any = true;
        }
        if (any)
            kit.appendChild (pad, nullptr);
    }
    return kit;
}

void RSU10AudioProcessor::kitFromTree (const juce::ValueTree& kit, const juce::File& relativeTo)
{
    samples.clearAll();
    pads.resetAll();
    kitName = kit.getProperty ("name", "KIT").toString();

    std::vector<Sample::Ptr> demo;
    for (const auto& pad : kit)
    {
        const int p = pad.getProperty ("index", -1);
        if (p < 0 || p >= kNumPads)
            continue;
        for (int i = 0; i < kNumPP; ++i)
            if (pad.hasProperty (ppDefs()[size_t (i)].id))
                pads.set (p, PP (i), (float) pad.getProperty (ppDefs()[size_t (i)].id));

        for (const auto& layer : pad)
        {
            const int l = layer.getProperty ("index", 0);
            if (l < 0 || l >= kMaxLayers)
                continue;
            if (layer.hasProperty ("demo"))
            {
                if (demo.empty())
                    demo = makeDemoKit();
                const int d = layer.getProperty ("demo");
                if (d >= 0 && d < (int) demo.size())
                    samples.set (p, l, demo[size_t (d)]);
                continue;
            }
            const auto path = layer.getProperty ("file").toString();
            const juce::File f = juce::File::isAbsolutePath (path) ? juce::File (path) : relativeTo.getChildFile (path);
            juce::String err;
            if (auto s = Sample::loadFromFile (f, &err))
                samples.set (p, l, s);
            else
                postMessage ("Manca " + f.getFileName());
        }
    }
}

bool RSU10AudioProcessor::saveKit (const juce::File& chosen, juce::String& error)
{
    const auto file = chosen.withFileExtension (kKitExtension);
    const auto folder = file.getParentDirectory().getChildFile (file.getFileNameWithoutExtension() + " - campioni");
    if (! folder.createDirectory())
    {
        error = "Non riesco a creare " + folder.getFullPathName();
        return false;
    }

    // Kit portabile: ogni campione viene copiato accanto al file del kit
    kitName = file.getFileNameWithoutExtension().toUpperCase();
    for (int p = 0; p < kNumPads; ++p)
        for (int l = 0; l < kMaxLayers; ++l)
        {
            auto* s = samples.get (p, l);
            if (s == nullptr)
                continue;
            if (s->demoIndex >= 0 || ! s->file().existsAsFile())
            {
                // senza file (kit di prova, registrazioni perse): si scrive un WAV
                const auto dest = folder.getChildFile (padLabel (p) + "_" + juce::File::createLegalFileName (s->name()) + ".wav");
                dest.deleteFile();
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> os (dest.createOutputStream());
                if (os != nullptr)
                    if (auto* w = wav.createWriterFor (os.get(), s->rate(), (unsigned) s->channels(), 24, {}, 0))
                    {
                        os.release();
                        std::unique_ptr<juce::AudioFormatWriter> writer (w);
                        writer->writeFromAudioSampleBuffer (s->audio(), 0, s->length());
                    }
                if (auto re = Sample::loadFromFile (dest))
                    samples.set (p, l, re);
            }
            else if (! s->file().isAChildOf (folder))
            {
                const auto dest = folder.getChildFile (s->file().getFileName());
                if (! dest.existsAsFile())
                    s->file().copyFileTo (dest);
                if (auto re = Sample::loadFromFile (dest))
                    samples.set (p, l, re);
            }
        }

    auto xml = kitToTree (file.getParentDirectory()).createXml();
    if (xml == nullptr || ! xml->writeTo (file))
    {
        error = "Non riesco a scrivere " + file.getFullPathName();
        return false;
    }
    kitFile = file;
    return true;
}

bool RSU10AudioProcessor::loadKit (const juce::File& file, juce::String& error)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (kKitTag))
    {
        error = "Non è un kit RSU-10: " + file.getFileName();
        return false;
    }
    engine.panic();
    kitFromTree (juce::ValueTree::fromXml (*xml), file.getParentDirectory());
    kitFile = file;
    return true;
}

// ---------------------------------------------------------------- campionamento

void RSU10AudioProcessor::startRecording()
{
    recPad = selectedPad.load();
    engine.armRecording (Engine::RecSource (recSource.load()), 0.01f);
    postMessage ("REC su " + padLabel (recPad) + ": in attesa del segnale");
}

void RSU10AudioProcessor::stopRecording()
{
    engine.stopRecording();
}

void RSU10AudioProcessor::finishRecording()
{
    juce::AudioBuffer<float> audio;
    double rate = 44100.0;
    const int pad = recPad.exchange (-1);
    if (! engine.takeRecording (audio, rate) || pad < 0)
        return;

    const auto folder = userFolder().getChildFile ("Registrazioni");
    folder.createDirectory();
    const auto file = folder.getNonexistentChildFile ("REC " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S"), ".wav");

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
    if (os != nullptr)
        if (auto* w = wav.createWriterFor (os.get(), rate, 2, 24, {}, 0))
        {
            os.release();
            std::unique_ptr<juce::AudioFormatWriter> writer (w);
            writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        }

    if (auto s = Sample::loadFromFile (file))
    {
        samples.set (pad, 0, s);
        pads.resetPad (pad);
        postMessage (padLabel (pad) + ": registrati " + juce::String (s->seconds(), 1) + " s");
    }
}

void RSU10AudioProcessor::timerCallback()
{
    samples.purge();
    engine.purgePreviews();
    if (engine.recState() == Engine::RecState::Done)
        finishRecording();
}

// ---------------------------------------------------------------- messaggi

void RSU10AudioProcessor::postMessage (const juce::String& m)
{
    const juce::ScopedLock sl (messageLock);
    message = m;
    messageTime = juce::Time::getMillisecondCounterHiRes();
}

juce::String RSU10AudioProcessor::getMessage (double& ageMs) const
{
    const juce::ScopedLock sl (messageLock);
    ageMs = juce::Time::getMillisecondCounterHiRes() - messageTime;
    return message;
}

// ---------------------------------------------------------------- stato

void RSU10AudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree state (kStateTag);
    state.appendChild (apvts.copyState(), nullptr);
    state.appendChild (kitToTree ({}), nullptr);
    state.setProperty ("selectedPad", selectedPad.load(), nullptr);
    state.setProperty ("page", page.load(), nullptr);
    state.setProperty ("scalePad", scalePad.load(), nullptr);
    state.setProperty ("recSource", recSource.load(), nullptr);
    state.setProperty ("browser", browserFolder.getFullPathName(), nullptr);
    state.setProperty ("kitFile", kitFile.getFullPathName(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void RSU10AudioProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (kStateTag))
        return;
    const auto state = juce::ValueTree::fromXml (*xml);

    const auto params = state.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);

    engine.panic();
    const auto kit = state.getChildWithName (kKitTag);
    if (kit.isValid())
        kitFromTree (kit, {});

    selectedPad = juce::jlimit (0, kNumPads - 1, (int) state.getProperty ("selectedPad", 0));
    page = juce::jlimit (0, 3, (int) state.getProperty ("page", 0));
    scalePad = juce::jlimit (0, kNumPads - 1, (int) state.getProperty ("scalePad", 0));
    recSource = juce::jlimit (0, 1, (int) state.getProperty ("recSource", 0));
    const juce::File folder (state.getProperty ("browser").toString());
    if (folder.isDirectory())
        browserFolder = folder;
    const auto kf = state.getProperty ("kitFile").toString();
    kitFile = kf.isNotEmpty() ? juce::File (kf) : juce::File();
}

juce::AudioProcessorEditor* RSU10AudioProcessor::createEditor()
{
    return new RSU10Editor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RSU10AudioProcessor();
}
