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
    StringArray roots, scaleNames;
    for (auto* r : scales::kRootNames)
        roots.add (r);
    for (const auto& sc : scales::kScales)
        scaleNames.add (sc.name);
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::keyRoot, 2 }, "Tonalita", roots, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::keyScale, 2 }, "Scala tastiera", scaleNames, 0));
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
    pKeyRoot  = apvts.getRawParameterValue (ParamID::keyRoot);
    pKeyScale = apvts.getRawParameterValue (ParamID::keyScale);

    browserFolder = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    mappedNote.fill (-1);
    loadDemoKit();
    startTimerHz (30);
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
    preparedRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    inputCopy.setSize (2, samplesPerBlock);
    midiWork.ensureSize (16384);   // il MIDI trasformato non alloca durante l'audio
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

    juce::Optional<double> ppq;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())
                g.hostBpm = *bpm;
            ppq = pos->getPpqPosition();
            playing = pos->getIsPlaying();
        }

    transformMidi (midi, n, g.hostBpm, ppq, playing);
    engine.process (out, inChannels > 0 ? inputs : nullptr, inChannels, n, midiWork, g);
}

// ---------------------------------------------------------------- campioni

juce::File RSU10AudioProcessor::userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("RSU-10");
}

void RSU10AudioProcessor::loadSample (int pad, int layer, const juce::File& f)
{
    pushUndo (pad);
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
    // -1/+1 scorrono i campioni della cartella del pad; se il pad non viene da un file
    // (kit di prova, pad vuoto) quelli della cartella aperta nel browser
    auto* s = samples.get (pad, 0);
    const bool fromFile = s != nullptr && s->file().existsAsFile();
    const auto folder = fromFile ? s->file().getParentDirectory() : browserFolder;
    auto siblings = folder.findChildFiles (juce::File::findFiles, false);
    siblings.removeIf ([] (const juce::File& f) { return ! Sample::isAudioFile (f); });
    siblings.sort();
    if (siblings.isEmpty())
    {
        postMessage ("-1/+1: nessun campione in " + folder.getFileName() + ". Apri una cartella di suoni nel browser");
        return;
    }
    const int i = fromFile ? siblings.indexOf (s->file()) : (delta > 0 ? -1 : 0);
    loadSample (pad, 0, siblings[(i + delta + siblings.size()) % siblings.size()]);
}

void RSU10AudioProcessor::clearPad (int pad)
{
    pushUndo (pad);
    samples.clearPad (pad);
    pads.resetPad (pad);
}

void RSU10AudioProcessor::clearSamples (int pad)
{
    pushUndo (pad);
    samples.clearPad (pad);
}

void RSU10AudioProcessor::copyPad (int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= kNumPads || to >= kNumPads)
        return;
    pushUndo (to);
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
        pushUndo (pad);
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

    drainPosted();
}

void RSU10AudioProcessor::drainPosted()
{
    // Azioni e comandi arrivati dal MIDI (tasti e pad della Mikro)
    const auto scope = postFifo.read (postFifo.getNumReady());
    auto run = [this] (int code)
    {
        if (code >= 0)
            padAction (code & 0xFF, PadAction (code >> 8));
        else
            runCommand (Cmd (-code - 1));
    };
    for (int i = 0; i < scope.blockSize1; ++i) run (postCodes[size_t (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i) run (postCodes[size_t (scope.startIndex2 + i)]);
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
    juce::String mutes, solos;
    for (int p = 0; p < kNumPads; ++p)
    {
        mutes << (engine.muted[p].load() ? '1' : '0');
        solos << (engine.soloed[p].load() ? '1' : '0');
    }
    state.setProperty ("muted", mutes, nullptr);
    state.setProperty ("soloed", solos, nullptr);
    state.setProperty ("padMode", padModeStored.load(), nullptr);
    state.setProperty ("fixedVel", fixedVelocity.load(), nullptr);
    state.setProperty ("repeatRate", repeatRate.load(), nullptr);
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
    page = juce::jlimit (0, kPages - 1, (int) state.getProperty ("page", 0));
    const auto mutes = state.getProperty ("muted").toString(), solos = state.getProperty ("soloed").toString();
    for (int p = 0; p < kNumPads; ++p)
    {
        engine.muted[p] = mutes[p] == '1';
        engine.soloed[p] = solos[p] == '1';
    }
    padModeStored = juce::jlimit (0, 1, (int) state.getProperty ("padMode", 0));
    fixedVelocity = (bool) state.getProperty ("fixedVel", false);
    repeatRate = juce::jlimit (0, kRepeatRates - 1, (int) state.getProperty ("repeatRate", 3));
    scalePad = juce::jlimit (0, kNumPads - 1, (int) state.getProperty ("scalePad", 0));
    recSource = juce::jlimit (0, 1, (int) state.getProperty ("recSource", 0));
    const juce::File folder (state.getProperty ("browser").toString());
    if (folder.isDirectory())
        browserFolder = folder;
    const auto kf = state.getProperty ("kitFile").toString();
    kitFile = kf.isNotEmpty() ? juce::File (kf) : juce::File();
}

// ---------------------------------------------------------------- funzioni Maschine

namespace
{
const char* actionName (PadAction a)
{
    switch (a)
    {
        case PadAction::Mute:      return "MUTE";
        case PadAction::Solo:      return "SOLO";
        case PadAction::Select:    return "SELECT";
        case PadAction::Duplicate: return "DUPLICATE";
        case PadAction::Erase:     return "ERASE";
        case PadAction::None:      break;
    }
    return "";
}
} // namespace

void RSU10AudioProcessor::post (int code)
{
    const auto scope = postFifo.write (1);
    if (scope.blockSize1 > 0)
        postCodes[size_t (scope.startIndex1)] = code;
    else if (scope.blockSize2 > 0)
        postCodes[size_t (scope.startIndex2)] = code;
}

// Tasti della Mikro come li manda Mikro Bridge: CC 20 + posizione nella tabella dei pulsanti
// (70 + posizione con SHIFT), SHIFT + pad 1..12 = CC 102..113. Valori 127 premuto / 0 rilasciato:
// gli altri valori (manopole di altri controller) passano al motore come prima.
bool RSU10AudioProcessor::controllerButton (int cc, int value)
{
    if (value != 0 && value != 127)
        return false;
    const bool down = value == 127;

    auto hold = [this, down] (PadAction a)
    {
        if (down)
            heldAction = int (a);
        else if (heldAction.load() == int (a))
        {
            heldAction = 0;
            postCommand (Cmd::ActionEnd);
        }
        return true;
    };

    if ((cc >= 20 && cc <= 46) || (cc >= 70 && cc <= 96))
    {
        const bool shift = cc >= 70;
        switch (cc - (shift ? 70 : 20))
        {
            case 3:  return hold (PadAction::Erase);
            case 22: return hold (PadAction::Duplicate);
            case 23: return hold (PadAction::Select);
            case 24: return hold (PadAction::Solo);
            case 25: return hold (PadAction::Mute);
            case 7:  // NOTE REPEAT: tenuto ripete; SHIFT + NOTE REPEAT cambia la griglia
                if (shift) { if (down) postCommand (Cmd::RepeatRate); }
                else repeatHeld = down;
                return true;
            default: break;
        }
        if (! down)
            return true;
        static constexpr Cmd map[] {
            Cmd::Play, Cmd::Rec, Cmd::Top, Cmd::Stop /*ERASE*/, Cmd::RepeatRate /*GRID*/, Cmd::Bwd, Cmd::Fwd,
            Cmd::Stop /*NOTE REPEAT*/, Cmd::Rec /*SAMPLING*/, Cmd::Enter /*BROWSE*/, Cmd::Bank /*GROUP*/,
            Cmd::Exit /*MAIN*/, Cmd::PrevPad, Cmd::NextPad, Cmd::Stop /*NAV*/, Cmd::NextPage /*CONTROL*/,
            Cmd::Scratch /*F1*/, Cmd::Rev /*F2*/, Cmd::NextRibbonMode /*F3*/, Cmd::SampleMinus /*SCENE*/,
            Cmd::SamplePlus /*PATTERN*/, Cmd::NextPadMode /*VIEW*/, Cmd::Stop, Cmd::Stop, Cmd::Stop, Cmd::Stop,
            Cmd::Hold /*ENCODER premuto*/ };
        const int idx = cc - (shift ? 70 : 20);
        postCommand (shift && idx == 21 ? Cmd::FixedVel : map[idx]);   // SHIFT + VIEW = FIXED VEL
        return true;
    }
    if (cc >= 102 && cc <= 113)   // SHIFT + pad, come stampato sotto i pad della Mikro
    {
        if (down)
            switch (cc - 102)
            {
                case 0:  postCommand (Cmd::Undo); break;
                case 1:  postCommand (Cmd::Redo); break;
                case 8:  postCommand (Cmd::ClearSelected); break;
                case 9:  postCommand (Cmd::Chop); break;   // SHIFT + pad 10
                case 10: postCommand (Cmd::CopyPad); break;
                case 11: postCommand (Cmd::PastePad); break;
                default: break;
            }
        return true;
    }
    return false;
}

void RSU10AudioProcessor::transformMidi (const juce::MidiBuffer& in, int numSamples, double bpm,
                                         const juce::Optional<double>& ppq, bool playing)
{
    midiWork.clear();
    const auto act = action();
    const bool vel16 = padModeStored.load() == int (PadMode::Velocity16) && pScale->load() < 0.5f;
    const bool fixed = fixedVelocity.load();
    const int sel = selectedPad.load();
    // KEYBOARD con una scala: ogni nota in arrivo si aggancia alla nota intonata più vicina
    const int keyScaleIdx = pScale->load() > 0.5f ? juce::roundToInt (pKeyScale->load()) : 0;
    const int tonic = juce::roundToInt (pRoot->load()) + juce::roundToInt (pKeyRoot->load());

    for (const auto meta : in)
    {
        const auto m = meta.getMessage();
        const int pos = meta.samplePosition;

        if (m.isController())
        {
            if (! controllerButton (m.getControllerNumber(), m.getControllerValue()))
                midiWork.addEvent (m, pos);
            continue;
        }
        if (m.isNoteOn())
        {
            const int n0 = m.getNoteNumber();
            const bool isPad = n0 >= kFirstNote && n0 < kFirstNote + kNumPads;
            if (act != PadAction::None && isPad)
            {
                swallowed[size_t (n0)] = true;   // il colpo fa l'azione, non suona
                post ((n0 - kFirstNote) | (int (act) << 8));
                continue;
            }
            int note = n0, vel = m.getVelocity();
            if (vel16 && isPad)
            {
                vel = ((n0 - kFirstNote) % kPadsPerBank + 1) * 8 - 1;   // 16 VELOCITY: pad 1 piano ... pad 16 forte
                note = kFirstNote + sel;
            }
            else if (fixed)
                vel = 127;
            if (keyScaleIdx > 0)
                note = juce::jlimit (0, 127, scales::snap (keyScaleIdx, tonic, n0));
            mappedNote[size_t (n0)] = note;
            heldVelocity[size_t (n0)] = vel;
            midiWork.addEvent (juce::MidiMessage::noteOn (m.getChannel(), note, (juce::uint8) vel), pos);
            continue;
        }
        if (m.isNoteOff())
        {
            const int n0 = m.getNoteNumber();
            heldVelocity[size_t (n0)] = 0;
            if (std::exchange (swallowed[size_t (n0)], false))
                continue;
            const int note = mappedNote[size_t (n0)] >= 0 ? mappedNote[size_t (n0)] : n0;
            mappedNote[size_t (n0)] = -1;
            midiWork.addEvent (juce::MidiMessage::noteOff (m.getChannel(), note), pos);
            continue;
        }
        midiWork.addEvent (m, pos);
    }

    // NOTE REPEAT: i pad tenuti si ripetono sulla griglia, a tempo con l'host
    const bool repeatOn = repeatUi.load() || repeatHeld.load();
    if (repeatOn)
    {
        const double beats = kRepeatBeats[juce::jlimit (0, kRepeatRates - 1, repeatRate.load())];
        const double step = juce::jmax (16.0, beats * 60.0 / juce::jlimit (20.0, 999.0, bpm > 0.0 ? bpm : 120.0) * preparedRate);
        const bool synced = playing && ppq.hasValue();
        if (synced)
            repeatPhase = std::fmod (*ppq, beats) / beats * step;
        else if (! repeatWasOn)
            repeatPhase = 0.0;

        const int uiPad = uiHeldPad.load();
        for (double t = repeatPhase > 0.0 ? step - repeatPhase : (synced ? 0.0 : step); t < numSamples; t += step)
        {
            const int at = int (t);
            for (int k = 0; k < 128; ++k)
                if (heldVelocity[size_t (k)] > 0 && mappedNote[size_t (k)] >= 0)
                {
                    midiWork.addEvent (juce::MidiMessage::noteOff (1, mappedNote[size_t (k)]), at);
                    midiWork.addEvent (juce::MidiMessage::noteOn (1, mappedNote[size_t (k)], (juce::uint8) heldVelocity[size_t (k)]), at);
                }
            if (uiPad >= 0)
            {
                midiWork.addEvent (juce::MidiMessage::noteOff (1, kFirstNote + uiPad), at);
                midiWork.addEvent (juce::MidiMessage::noteOn (1, kFirstNote + uiPad, (juce::uint8) juce::jlimit (1, 127, uiHeldVelocity.load())), at);
            }
        }
        if (! synced)
            repeatPhase = std::fmod (repeatPhase + numSamples, step);
    }
    repeatWasOn = repeatOn;
}

void RSU10AudioProcessor::toggleUiAction (PadAction a)
{
    const bool off = uiAction.load() == int (a);
    uiAction = off ? 0 : int (a);
    duplicateSource = -1;
    if (! off)
        postMessage (juce::String (actionName (a)) + (a == PadAction::Duplicate ? ": tocca il pad da copiare" : ": tocca i pad"));
}

void RSU10AudioProcessor::padAction (int pad, PadAction a)
{
    if (pad < 0 || pad >= kNumPads)
        return;
    switch (a)
    {
        case PadAction::Mute:
            engine.muted[pad] = ! engine.muted[pad].load();
            postMessage (padLabel (pad) + (engine.muted[pad].load() ? " MUTE" : " suona"));
            break;
        case PadAction::Solo:
            engine.soloed[pad] = ! engine.soloed[pad].load();
            postMessage (padLabel (pad) + (engine.soloed[pad].load() ? " SOLO" : " fuori dal solo"));
            break;
        case PadAction::Select:
            selectedPad = pad;
            break;
        case PadAction::Duplicate:
            if (duplicateSource < 0)
            {
                duplicateSource = pad;
                postMessage ("DUPLICATE: " + padLabel (pad) + " su quale pad?");
            }
            else if (pad != duplicateSource)
            {
                copyPad (duplicateSource, pad);
                selectedPad = pad;
                postMessage (padLabel (duplicateSource) + " copiato su " + padLabel (pad));
            }
            break;
        case PadAction::Erase:
            engine.panic();
            clearPad (pad);
            postMessage (padLabel (pad) + " cancellato (UNDO per riaverlo)");
            break;
        case PadAction::None:
            break;
    }
}

int RSU10AudioProcessor::keyboardSemitones (int slot) const noexcept
{
    return juce::roundToInt (pKeyRoot->load()) + scales::degreeToSemitones (juce::roundToInt (pKeyScale->load()), slot);
}

juce::String RSU10AudioProcessor::keyboardNoteName (int slot) const
{
    const int semis = keyboardSemitones (slot);
    return juce::String (scales::kRootNames[scales::mod (semis, 12)]) + juce::String (3 + scales::floorDiv (semis, 12));
}

juce::String RSU10AudioProcessor::scaleName() const
{
    return (juce::String (scales::kRootNames[juce::roundToInt (pKeyRoot->load()) % 12]) + " "
            + scales::get (juce::roundToInt (pKeyScale->load())).name).toUpperCase();
}

PadMode RSU10AudioProcessor::padMode() const noexcept
{
    if (pScale->load() > 0.5f)
        return PadMode::Keyboard;
    return PadMode (padModeStored.load());
}

void RSU10AudioProcessor::setPadMode (PadMode m)
{
    padModeStored = m == PadMode::Velocity16 ? int (PadMode::Velocity16) : int (PadMode::Normal);
    if (m == PadMode::Keyboard)
        scalePad = selectedPad.load();
    if (auto* p = apvts.getParameter (ParamID::scale))
        p->setValueNotifyingHost (m == PadMode::Keyboard ? 1.0f : 0.0f);
    static const char* names[] { "PAD MODE: normale", "PAD MODE: 16 VELOCITY di ", "PAD MODE: KEYBOARD di " };
    postMessage (names[int (m)] + (m == PadMode::Normal ? juce::String() : padLabel (selectedPad))
                 + (m == PadMode::Keyboard ? " in " + scaleName() : juce::String()));
}

void RSU10AudioProcessor::runCommand (Cmd c)
{
    if (editorCommand != nullptr && editorCommand (c))
        return;

    auto setParam = [this] (const char* id, float value)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    };
    const int sel = selectedPad.load();

    switch (c)
    {
        case Cmd::Play:
            engine.triggerFromUi (sel, 110, true);
            engine.triggerFromUi (sel, 0, false);
            break;
        case Cmd::Rec:
            if (engine.recState() == Engine::RecState::Idle) startRecording(); else stopRecording();
            break;
        case Cmd::Top:
            setParam (ParamID::ribbon, 0.5f);
            setParam (ParamID::ribbonHold, 0.0f);
            engine.stopPreview();
            break;
        case Cmd::Stop:
            engine.stopPreview();
            engine.panic();
            break;
        case Cmd::Exit:
            if (engine.recState() != Engine::RecState::Idle)
                stopRecording();
            break;
        case Cmd::Bank:
            selectedPad = ((sel / kPadsPerBank + 1) % kBanks) * kPadsPerBank + sel % kPadsPerBank;
            postMessage (juce::String ("BANCO ") + juce::String::charToString (juce::juce_wchar ('A' + selectedPad / kPadsPerBank)));
            break;
        case Cmd::PrevPad:     selectedPad = (sel + kNumPads - 1) % kNumPads; break;
        case Cmd::NextPad:     selectedPad = (sel + 1) % kNumPads; break;
        case Cmd::NextPage:    page = (page.load() + 1) % kPages; break;
        case Cmd::Scratch:     setParam (ParamID::ribbonMode, float (RibbonMode::Scratch)); break;
        case Cmd::Rev:         setParam (ParamID::ribbonMode, float (RibbonMode::Reverse)); break;
        case Cmd::NextRibbonMode:
            setParam (ParamID::ribbonMode, float ((juce::roundToInt (pMode->load()) + 1) % 5));
            break;
        case Cmd::Hold:        setParam (ParamID::ribbonHold, pHold->load() > 0.5f ? 0.0f : 1.0f); break;
        case Cmd::NextPadMode: setPadMode (PadMode ((int (padMode()) + 1) % 3)); break;
        case Cmd::FixedVel:
            fixedVelocity = ! fixedVelocity.load();
            postMessage (fixedVelocity.load() ? "FIXED VEL: tutti i colpi a 127" : "FIXED VEL spento");
            break;
        case Cmd::RepeatRate:
            repeatRate = (repeatRate.load() + 1) % kRepeatRates;
            postMessage (juce::String ("NOTE REPEAT: ") + kRepeatRateNames[repeatRate.load()]);
            break;
        case Cmd::Undo:        undo(); break;
        case Cmd::Redo:        redo(); break;
        case Cmd::SampleMinus: stepSampleInFolder (sel, -1); break;
        case Cmd::SamplePlus:  stepSampleInFolder (sel, +1); break;
        case Cmd::ClearSelected:
            padAction (sel, PadAction::Erase);
            break;
        case Cmd::CopyPad:
            clipboardPad = sel;
            postMessage ("COPY: " + padLabel (sel));
            break;
        case Cmd::PastePad:
            if (clipboardPad >= 0 && clipboardPad != sel)
            {
                copyPad (clipboardPad, sel);
                postMessage ("PASTE: " + padLabel (clipboardPad) + " su " + padLabel (sel));
            }
            break;
        case Cmd::ActionEnd:   duplicateSource = -1; break;
        case Cmd::Chop:
        {
            // ripremuto entro 4 s sul campione appena tagliato: tagli uguali; poi di nuovo transienti
            const double now = juce::Time::getMillisecondCounterHiRes();
            const bool again = lastChop.sample != nullptr && now - lastChop.at < 4000.0
                               && sel / kPadsPerBank == lastChop.pad / kPadsPerBank;
            const auto mode = again && lastChop.mode == ChopMode::Transients ? ChopMode::Equal : ChopMode::Transients;
            chopPad (again ? lastChop.pad : sel, mode);
            break;
        }
        case Cmd::Bwd: case Cmd::Fwd: case Cmd::Enter:
            break;   // servono il browser dell'editor
    }
}

// ---------------------------------------------------------------- CHOP

std::vector<double> RSU10AudioProcessor::findSlices (const juce::AudioBuffer<float>& audio, double rate,
                                                     int from, int to, int maxSlices)
{
    // Inviluppo a finestre da 5 ms; un colpo = salto d'energia sopra la media recente, abbastanza
    // forte rispetto al picco e ad almeno 60 ms dal precedente. Si tengono i colpi più forti.
    const int hop = juce::jmax (16, int (rate * 0.005));
    const int frames = (to - from) / hop;
    std::vector<double> starts { double (from) };
    if (frames < 4)
        return starts;

    std::vector<float> env (size_t (frames), 0.0f);
    float peak = 0.0f;
    for (int f = 0; f < frames; ++f)
    {
        float e = 0.0f;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            const float* x = audio.getReadPointer (ch, from + f * hop);
            for (int i = 0; i < hop; ++i)
                e += x[i] * x[i];
        }
        env[size_t (f)] = std::sqrt (e / float (hop * juce::jmax (1, audio.getNumChannels())));
        peak = juce::jmax (peak, env[size_t (f)]);
    }
    if (peak <= 1.0e-6f)
        return starts;

    struct Onset { int frame; float strength; };
    std::vector<Onset> onsets;
    const int minGap = juce::jmax (1, int (0.06 * rate / hop));
    float recent = env[0];
    int last = 0;
    for (int f = 1; f < frames; ++f)
    {
        const float rise = env[size_t (f)] - recent;
        if (rise > 0.12f * peak && env[size_t (f)] > 1.6f * recent && f - last >= minGap)
        {
            onsets.push_back ({ f, rise });
            last = f;
        }
        recent = recent * 0.8f + env[size_t (f)] * 0.2f;
    }
    // i più forti, poi in ordine di tempo (il primo pezzo parte sempre dall'inizio)
    std::sort (onsets.begin(), onsets.end(), [] (const Onset& a, const Onset& b) { return a.strength > b.strength; });
    if ((int) onsets.size() > maxSlices - 1)
        onsets.resize (size_t (maxSlices - 1));
    for (const auto& o : onsets)
        if (o.frame * hop > hop * minGap)   // non a ridosso dell'inizio
            starts.push_back (double (from + o.frame * hop));
    std::sort (starts.begin(), starts.end());
    return starts;
}

int RSU10AudioProcessor::chopPad (int pad, ChopMode mode)
{
    if (pad < 0 || pad >= kNumPads)
        return 0;
    // Se si richiama il chop appena fatto, si riparte dal campione e dalla regione originali
    const bool redo = lastChop.sample != nullptr && lastChop.pad == pad && samples.get (pad, 0) == lastChop.sample.get();
    Sample::Ptr s = redo ? lastChop.sample : Sample::Ptr (samples.get (pad, 0));
    if (s == nullptr || s->length() < 1000)
    {
        postMessage ("CHOP: il pad " + padLabel (pad) + " non ha un campione da tagliare");
        return 0;
    }
    const float regionStart = redo ? lastChop.start : pads.get (pad, PP::Start);
    const float regionEnd = redo ? lastChop.end : pads.get (pad, PP::End);
    const int len = s->length();
    const int a = juce::jlimit (0, len - 1, int (regionStart * len)), b = juce::jlimit (a + 1, len, int (regionEnd * len));

    std::vector<double> starts;
    if (mode == ChopMode::Transients)
        starts = findSlices (s->audio(), s->rate(), a, b, kPadsPerBank);
    if (starts.size() < 2)   // niente colpi chiari (pad, voce...): parti uguali
    {
        mode = ChopMode::Equal;
        starts.clear();
        for (int i = 0; i < kPadsPerBank; ++i)
            starts.push_back (a + (b - a) * double (i) / kPadsPerBank);
    }

    // Le fette vanno sui pad del banco, dal primo: ognuna eredita il suono del pad di partenza
    const int first = (pad / kPadsPerBank) * kPadsPerBank;
    std::array<float, kNumPP> base {};
    for (int i = 0; i < kNumPP; ++i)
        base[size_t (i)] = pads.get (pad, PP (i));
    engine.panic();
    const int n = (int) starts.size();
    const int group = ++undoGroupCounter;
    for (int i = 0; i < n; ++i)
    {
        const int target = first + i;
        pushUndo (target, group);
        samples.set (target, 0, s);
        for (int l = 1; l < kMaxLayers; ++l)
            samples.set (target, l, nullptr);
        for (int k = 0; k < kNumPP; ++k)
            pads.set (target, PP (k), base[size_t (k)]);
        const double end = i + 1 < n ? starts[size_t (i + 1)] : double (b);
        pads.set (target, PP::Start, float (starts[size_t (i)] / len));
        pads.set (target, PP::End, float (end / len));
        pads.set (target, PP::PlayMode, float (PlayMode::OneShot));
        pads.set (target, PP::Reverse, 0.0f);
        pads.set (target, PP::Stretch, float (Stretch::Off));
        pads.set (target, PP::FadeIn, 1.0f);    // niente click ai tagli
        pads.set (target, PP::FadeOut, 6.0f);
    }
    lastChop = { s, first, regionStart, regionEnd, juce::Time::getMillisecondCounterHiRes(), mode };
    selectedPad = first;
    postMessage ("CHOP " + juce::String (mode == ChopMode::Transients ? "sui colpi" : "in parti uguali") + ": "
                 + juce::String (n) + " fette su " + padLabel (first) + "-" + padLabel (first + n - 1)
                 + (mode == ChopMode::Transients ? "  (di nuovo SHIFT+pad 10 = parti uguali)" : ""));
    return n;
}

RSU10AudioProcessor::PadSnapshot RSU10AudioProcessor::snapshot (int pad) const
{
    PadSnapshot s;
    s.pad = pad;
    for (int i = 0; i < kNumPP; ++i)
        s.values[size_t (i)] = pads.get (pad, PP (i));
    for (int l = 0; l < kMaxLayers; ++l)
        s.layers[size_t (l)] = samples.get (pad, l);
    return s;
}

void RSU10AudioProcessor::restore (const PadSnapshot& s)
{
    for (int l = 0; l < kMaxLayers; ++l)
        samples.set (s.pad, l, s.layers[size_t (l)]);
    for (int i = 0; i < kNumPP; ++i)
        pads.set (s.pad, PP (i), s.values[size_t (i)]);
    selectedPad = s.pad;
}

void RSU10AudioProcessor::pushUndo (int pad, int group)
{
    if (pad < 0 || pad >= kNumPads)
        return;
    undoStack.push_back (snapshot (pad));
    undoStack.back().group = group;
    if (undoStack.size() > 128)   // abbastanza per qualche chop da 16 fette
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void RSU10AudioProcessor::undo()
{
    if (undoStack.empty())
    {
        postMessage ("Niente da annullare");
        return;
    }
    engine.panic();
    const int group = undoStack.back().group;
    int restored = 0, firstPad = 0;
    do
    {
        const auto s = undoStack.back();
        undoStack.pop_back();
        redoStack.push_back (snapshot (s.pad));
        redoStack.back().group = group;
        restore (s);
        firstPad = s.pad;
        ++restored;
    }
    while (group != 0 && ! undoStack.empty() && undoStack.back().group == group);
    postMessage ("UNDO: " + (restored > 1 ? juce::String (restored) + " pad" : padLabel (firstPad)));
}

void RSU10AudioProcessor::redo()
{
    if (redoStack.empty())
    {
        postMessage ("Niente da ripetere");
        return;
    }
    engine.panic();
    const int group = redoStack.back().group;
    int restored = 0, firstPad = 0;
    do
    {
        const auto s = redoStack.back();
        redoStack.pop_back();
        undoStack.push_back (snapshot (s.pad));
        undoStack.back().group = group;
        restore (s);
        firstPad = s.pad;
        ++restored;
    }
    while (group != 0 && ! redoStack.empty() && redoStack.back().group == group);
    postMessage ("REDO: " + (restored > 1 ? juce::String (restored) + " pad" : padLabel (firstPad)));
}

juce::AudioProcessorEditor* RSU10AudioProcessor::createEditor()
{
    return new RSU10Editor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RSU10AudioProcessor();
}
