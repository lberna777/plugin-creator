#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MixCompassProcessor::MixCompassProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MixCompass", createLayout())
{
    listenRefParam  = apvts.getRawParameterValue (ParamIds::listenRef);
    levelMatchParam = apvts.getRawParameterValue (ParamIds::levelMatch);

    fifoL.assign ((size_t) fifoSize, 0.0f);
    fifoR.assign ((size_t) fifoSize, 0.0f);
    analyzer = std::make_unique<mc::MixAnalyzer>();
    analyzer->prepare (48000.0, 2);

    loader = std::make_unique<ReferenceLoader> (
        [this] (std::unique_ptr<ReferenceTrack> t) { installReference (std::move (t)); },
        [this] (const juce::File& f, const juce::String& why) { referenceFailed (f, why); });
}

MixCompassProcessor::~MixCompassProcessor()
{
    loader.reset();          // ferma il caricamento prima di toccare il riferimento
    worker.stopThread (2000);
    delete currentReference.exchange (nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout MixCompassProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::listenRef, 1 }, "A / B", false,
        juce::AudioParameterBoolAttributes().withStringFromValueFunction (
            [] (bool v, int) { return juce::String (v ? "Riferimento" : "Mix"); })));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::levelMatch, 1 }, "Level match", true));
    return layout;
}

bool MixCompassProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

//==============================================================================
void MixCompassProcessor::prepareToPlay (double sampleRate, int)
{
    worker.stopThread (2000);

    currentSampleRate = sampleRate;
    analysisChannels = getTotalNumInputChannels() >= 2 ? 2 : 1;
    analyzer->prepare (sampleRate, analysisChannels);
    fifo.reset();
    droppedSamples = 0;
    mixLoudnessForMatch = std::numeric_limits<float>::quiet_NaN();

    abAmount.reset (sampleRate, 0.02);        // crossfade A/B di 20 ms
    abAmount.setCurrentAndTargetValue (0.0f);
    refGain.reset (sampleRate, 0.05);
    refGain.setCurrentAndTargetValue (1.0f);
    referencePosition = 0;

    worker.startThread (juce::Thread::Priority::low);

    // il riferimento vive alla sample rate dell'host: se è cambiata (o se ne aspettiamo uno dallo
    // stato salvato) si ricarica, sul thread del loader
    juce::File file;
    {
        const juce::ScopedLock sl (statusLock);
        file = referenceFile;
    }
    const auto* ref = currentReference.load();
    if (file != juce::File() && (ref == nullptr || ! juce::exactlyEqual (ref->sampleRate, sampleRate)))
    {
        {
            const juce::ScopedLock sl (statusLock);
            status.state = RefState::loading;
            status.message = "Preparo il riferimento alla nuova sample rate…";
        }
        loader->request (file, sampleRate);
    }
}

void MixCompassProcessor::releaseResources()
{
    worker.stopThread (2000);
}

//==============================================================================
void MixCompassProcessor::pushToFifo (const juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    const auto* inL = buffer.getReadPointer (0);
    const auto* inR = buffer.getNumChannels() > 1 && analysisChannels == 2 ? buffer.getReadPointer (1) : inL;

    const auto scope = fifo.write (numSamples);
    if (scope.blockSize1 > 0)
    {
        std::copy (inL, inL + scope.blockSize1, fifoL.data() + scope.startIndex1);
        std::copy (inR, inR + scope.blockSize1, fifoR.data() + scope.startIndex1);
    }
    if (scope.blockSize2 > 0)
    {
        std::copy (inL + scope.blockSize1, inL + scope.blockSize1 + scope.blockSize2, fifoL.data() + scope.startIndex2);
        std::copy (inR + scope.blockSize1, inR + scope.blockSize1 + scope.blockSize2, fifoR.data() + scope.startIndex2);
    }
    const int lost = numSamples - scope.blockSize1 - scope.blockSize2;
    if (lost > 0) droppedSamples.fetch_add (lost);
}

void MixCompassProcessor::drainFifo() noexcept
{
    for (;;)
    {
        const int ready = fifo.getNumReady();
        if (ready <= 0) return;
        const auto scope = fifo.read (juce::jmin (ready, 4096));
        if (scope.blockSize1 > 0)
        {
            const float* d[2] = { fifoL.data() + scope.startIndex1, fifoR.data() + scope.startIndex1 };
            analyzer->process (d, scope.blockSize1);
        }
        if (scope.blockSize2 > 0)
        {
            const float* d[2] = { fifoL.data() + scope.startIndex2, fifoR.data() + scope.startIndex2 };
            analyzer->process (d, scope.blockSize2);
        }
    }
}

void MixCompassProcessor::AnalysisWorker::run()
{
    auto lastPublish = 0.0;
    while (! threadShouldExit())
    {
        bool forcePublish = false;
        if (owner.resetRequested.exchange (false))
        {
            owner.analyzer->reset();
            owner.mixLoudnessForMatch = std::numeric_limits<float>::quiet_NaN();
            forcePublish = true;
        }

        owner.drainFifo();

        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (forcePublish || now - lastPublish >= 100.0)
        {
            auto& s = owner.mixSnapshots.back();
            owner.analyzer->fill (s);
            owner.mixSnapshots.publish();
            lastPublish = now;

            // loudness del mix per il level match: integrated appena è affidabile, prima lo short-term
            float match = std::numeric_limits<float>::quiet_NaN();
            if (s.seconds >= 3.0 && s.integrated > mc::Snapshot::silence)  match = (float) s.integrated;
            else if (s.shortTerm > mc::Snapshot::silence)                  match = (float) s.shortTerm;
            owner.mixLoudnessForMatch = match;
        }
        wait (10);
    }
}

//==============================================================================
ReferenceTrack* MixCompassProcessor::acquireReference() noexcept
{
    // hazard pointer: annuncia quale riferimento stai usando, poi verifica che sia ancora quello
    // corrente. Chi lo sostituisce aspetta che l'annuncio cambi prima di cancellarlo.
    ReferenceTrack* p;
    do
    {
        p = currentReference.load();
        referenceInUse.store (p);
    } while (currentReference.load() != p);
    return p;
}

void MixCompassProcessor::installReference (std::unique_ptr<ReferenceTrack> track)
{
    auto analysis = std::make_shared<const mc::Snapshot> (track->analysis);
    const auto file = track->file;

    auto* old = currentReference.exchange (track.release());
    while (old != nullptr && referenceInUse.load() == old)
        juce::Thread::sleep (1);
    delete old;

    const juce::ScopedLock sl (statusLock);
    status.state = RefState::ready;
    status.name = file.getFileName();
    status.message = {};
    status.analysis = std::move (analysis);
    referenceFile = file;
}

void MixCompassProcessor::referenceFailed (const juce::File& file, const juce::String& why)
{
    const juce::ScopedLock sl (statusLock);
    status.state = RefState::error;
    status.message = "Non riesco a usare " + file.getFileName() + ": " + why;
    // resta valido il riferimento precedente, se c'era
    if (const auto* ref = currentReference.load())
        referenceFile = ref->file;
    else
        referenceFile = juce::File();
}

void MixCompassProcessor::loadReference (const juce::File& file)
{
    double rate;
    {
        const juce::ScopedLock sl (statusLock);
        referenceFile = file;
        status.state = RefState::loading;
        status.name = file.getFileName();
        status.message = "Analizzo il riferimento…";
        rate = currentSampleRate;
    }
    if (rate > 0)
        loader->request (file, rate);
    else
    {
        const juce::ScopedLock sl (statusLock);
        status.message = "Il riferimento si carica appena parte l'audio.";
    }
}

MixCompassProcessor::RefStatus MixCompassProcessor::getReferenceStatus() const
{
    const juce::ScopedLock sl (statusLock);
    return status;
}

//==============================================================================
void MixCompassProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);
    if (numSamples == 0 || numIn == 0) return;

    // l'analisi ascolta SEMPRE il tuo mix, anche mentre senti il riferimento
    pushToFifo (buffer, numSamples);

    auto* ref = acquireReference();
    const bool usable = ref != nullptr && juce::exactlyEqual (ref->sampleRate, currentSampleRate) && ref->audio.getNumSamples() > 0;
    abAmount.setTargetValue (usable && listenRefParam->load() > 0.5f ? 1.0f : 0.0f);

    float gainDb = 0.0f;
    if (usable && levelMatchParam->load() > 0.5f)
    {
        const float mixLufs = mixLoudnessForMatch.load();
        if (! std::isnan (mixLufs))
            gainDb = juce::jlimit (-24.0f, 12.0f, mixLufs - (float) ref->analysis.integrated);
    }
    refGain.setTargetValue (juce::Decibels::decibelsToGain (gainDb));

    // modalità A a regime: l'uscita È l'ingresso, nemmeno moltiplicata per 1 (null-test esatto)
    if (! abAmount.isSmoothing() && juce::exactlyEqual (abAmount.getCurrentValue(), 0.0f))
    {
        refGain.skip (numSamples);
        releaseReference();
        return;
    }

    const int refLength = usable ? ref->audio.getNumSamples() : 0;
    const float* refL = usable ? ref->audio.getReadPointer (0) : nullptr;
    const float* refR = usable ? ref->audio.getReadPointer (1) : nullptr;
    if (referencePosition >= refLength) referencePosition = 0;

    auto* out0 = buffer.getWritePointer (0);
    auto* out1 = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    for (int i = 0; i < numSamples; ++i)
    {
        const float x = abAmount.getNextValue();
        const float g = refGain.getNextValue() * x;
        float l = 0.0f, r = 0.0f;
        if (usable)
        {
            l = refL[referencePosition];
            r = refR[referencePosition];
            if (++referencePosition >= refLength) referencePosition = 0;
        }
        if (out1 != nullptr)
        {
            out0[i] = out0[i] * (1.0f - x) + l * g;
            out1[i] = out1[i] * (1.0f - x) + r * g;
        }
        else
        {
            out0[i] = out0[i] * (1.0f - x) + 0.5f * (l + r) * g;
        }
    }
    releaseReference();
}

void MixCompassProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // bypass: l'audio passa intatto; alla ripresa si riparte da A, senza salti
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
    abAmount.setCurrentAndTargetValue (0.0f);
}

//==============================================================================
void MixCompassProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    {
        const juce::ScopedLock sl (statusLock);
        state.setProperty ("referencePath", referenceFile.getFullPathName(), nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void MixCompassProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    const auto path = state.getProperty ("referencePath").toString();
    state.removeProperty ("referencePath", nullptr);
    apvts.replaceState (state);

    if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
    {
        const juce::File file (path);
        if (file.existsAsFile())
            loadReference (file);
        else
        {
            const juce::ScopedLock sl (statusLock);
            status.state = RefState::error;
            status.message = "Il riferimento salvato non si trova più: " + path;
        }
    }
}

juce::AudioProcessorEditor* MixCompassProcessor::createEditor()
{
    return new MixCompassEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MixCompassProcessor();
}
