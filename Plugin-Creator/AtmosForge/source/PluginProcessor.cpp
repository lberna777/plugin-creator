#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
    // Le scene e i materiali si nominano una volta sola: UI e parametri leggono da qui.
    const StringArray kSceneNames   { "STRADA", "CAMINO", "PIOGGIA / NEVE", "PASSI" };
    const StringArray kSurfaceNames { "Legno", "Ghiaia", "Neve", "Cemento", "Metallo" };

    constexpr int kCtrlChunk = 32;   // ogni quanti campioni la scena rilegge i parametri
}

const StringArray& sceneNames()   { return kSceneNames; }
const StringArray& surfaceNames() { return kSurfaceNames; }

//==============================================================================
AudioProcessorValueTreeState::ParameterLayout AtmosForgeAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamID::scene, 1 }, "Scena", kSceneNames, 1));

    auto pct = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name,
            NormalisableRange<float> (0.0f, 1.0f, 0.001f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; }));
    };

    layout.add (pct (ParamID::density, "Densita", 0.5f));
    layout.add (pct (ParamID::motion,  "Movimento", 0.5f));
    layout.add (pct (ParamID::tone,    "Timbro", 0.5f));
    layout.add (pct (ParamID::depth,   "Distanza", 0.35f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::width, 1 }, "Ampiezza",
        NormalisableRange<float> (0.0f, 2.0f, 0.001f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::level, 1 }, "Livello",
        NormalisableRange<float> (-36.0f, 12.0f, 0.1f), -6.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
            [] (float v, int) { return String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamID::surface, 1 }, "Materiale", kSurfaceNames, 1));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { ParamID::seed, 1 }, "Seme", 0, 9999, 1234));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { ParamID::autoGen, 1 }, "Auto", true));

    return layout;
}

//==============================================================================
AtmosForgeAudioProcessor::AtmosForgeAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

void AtmosForgeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    engine.reset();

    scratch.setSize (2, jmax (samplesPerBlock, 512), false, true, true);   // niente alloc nel path audio

    const double rampSec = 0.05;
    for (auto* s : { &smDensity, &smMotion, &smTone, &smDepth, &smWidth, &smGain })
        s->reset (sampleRate, rampSec);

    smDensity.setCurrentAndTargetValue (apvts.getRawParameterValue (ParamID::density)->load());
    smMotion .setCurrentAndTargetValue (apvts.getRawParameterValue (ParamID::motion)->load());
    smTone   .setCurrentAndTargetValue (apvts.getRawParameterValue (ParamID::tone)->load());
    smDepth  .setCurrentAndTargetValue (apvts.getRawParameterValue (ParamID::depth)->load());
    smWidth  .setCurrentAndTargetValue (apvts.getRawParameterValue (ParamID::width)->load());
    smGain   .setCurrentAndTargetValue (af::dbToGain (apvts.getRawParameterValue (ParamID::level)->load()));

    peakLevel.store (0.0f);
}

bool AtmosForgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet().isDisabled()          // strumento: input opzionale
        || layouts.getMainInputChannelSet() == out;
}

af::SceneParams AtmosForgeAudioProcessor::currentParams() const
{
    af::SceneParams p;
    p.density = apvts.getRawParameterValue (ParamID::density)->load();
    p.motion  = apvts.getRawParameterValue (ParamID::motion)->load();
    p.tone    = apvts.getRawParameterValue (ParamID::tone)->load();
    p.depth   = apvts.getRawParameterValue (ParamID::depth)->load();
    p.width   = apvts.getRawParameterValue (ParamID::width)->load() * 0.5f;
    p.surface = (int) apvts.getRawParameterValue (ParamID::surface)->load();
    p.autoGen = apvts.getRawParameterValue (ParamID::autoGen)->load() > 0.5f;
    return p;
}

void AtmosForgeAudioProcessor::triggerManualEvent (bool big) noexcept
{
    (big ? pendingBigEvents : pendingSmallEvents).fetch_add (1);
}

void AtmosForgeAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numOutCh   = buffer.getNumChannels();
    if (numSamples <= 0 || numOutCh <= 0) return;

    // --- stato non continuo: scena, seme, materiale
    engine.setScene ((af::SceneId) (int) apvts.getRawParameterValue (ParamID::scene)->load());

    const auto seed = (uint32_t) (int) apvts.getRawParameterValue (ParamID::seed)->load();
    if (seed != lastSeed) { lastSeed = seed; engine.setSeed (seed); }

    // --- parametri continui: target smussati (niente zipper su automazione)
    smDensity.setTargetValue (apvts.getRawParameterValue (ParamID::density)->load());
    smMotion .setTargetValue (apvts.getRawParameterValue (ParamID::motion)->load());
    smTone   .setTargetValue (apvts.getRawParameterValue (ParamID::tone)->load());
    smDepth  .setTargetValue (apvts.getRawParameterValue (ParamID::depth)->load());
    smWidth  .setTargetValue (apvts.getRawParameterValue (ParamID::width)->load());
    smGain   .setTargetValue (af::dbToGain (apvts.getRawParameterValue (ParamID::level)->load()));

    auto p = currentParams();

    // --- eventi chiesti dalla UI (consumo lock-free dei contatori)
    for (int n = pendingSmallEvents.exchange (0); n > 0; --n) engine.triggerNote (72, 0.9f);
    for (int n = pendingBigEvents.exchange (0);   n > 0; --n) engine.triggerNote (48, 1.0f);

    float* wl = scratch.getWritePointer (0);
    float* wr = scratch.getWritePointer (1);

    int pos = 0;
    auto it = midi.begin();

    while (pos < numSamples)
    {
        // 1) consuma tutte le note che cadono su questo campione
        while (it != midi.end())
        {
            const auto meta = *it;
            if (meta.samplePosition > pos) break;
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
                engine.triggerNote (msg.getNoteNumber(), msg.getFloatVelocity());
            ++it;
        }

        // 2) rendi fino alla prossima nota, a blocchetti di control-rate
        int until = numSamples;
        if (it != midi.end())
            until = jlimit (pos + 1, numSamples, (*it).samplePosition);

        const int chunk = jmin (kCtrlChunk, until - pos);

        p.density = smDensity.getNextValue();
        p.motion  = smMotion.getNextValue();
        p.tone    = smTone.getNextValue();
        p.depth   = smDepth.getNextValue();
        smDensity.skip (chunk - 1); smMotion.skip (chunk - 1);
        smTone.skip (chunk - 1);    smDepth.skip (chunk - 1);

        engine.render (wl + pos, wr + pos, chunk, p);
        pos += chunk;
    }

    // --- larghezza stereo + livello, campione per campione, poi soft clip
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float w = smWidth.getNextValue();
        const float g = smGain.getNextValue();
        const float m = (wl[i] + wr[i]) * 0.5f;
        const float s = (wl[i] - wr[i]) * 0.5f * w;
        const float l = af::softClip ((m + s) * g);
        const float r = af::softClip ((m - s) * g);
        wl[i] = l; wr[i] = r;
        peak = jmax (peak, jmax (std::abs (l), std::abs (r)));
    }
    peakLevel.store (peak);

    // --- uscita: stereo, oppure somma a mono su una traccia mono (mai silenzio)
    if (numOutCh == 1)
    {
        auto* dst = buffer.getWritePointer (0);
        for (int i = 0; i < numSamples; ++i)
            dst[i] = (wl[i] + wr[i]) * 0.5f;
    }
    else
    {
        buffer.copyFrom (0, 0, wl, numSamples);
        buffer.copyFrom (1, 0, wr, numSamples);
        for (int ch = 2; ch < numOutCh; ++ch)
            buffer.clear (ch, 0, numSamples);
    }
}

void AtmosForgeAudioProcessor::processBlockBypassed (AudioBuffer<float>& buffer, MidiBuffer&)
{
    // generatore: in bypass non c'è nulla da far passare, si tace
    buffer.clear();
    peakLevel.store (0.0f);
}

//==============================================================================
bool AtmosForgeAudioProcessor::renderToFile (const File& destination, double seconds, double sampleRate)
{
    jassert (MessageManager::getInstance()->isThisTheMessageThread());   // mai dal path audio

    const int total = (int) std::ceil (seconds * sampleRate);
    if (total <= 0) return false;

    // motore dedicato: il render offline non tocca lo stato di ciò che sta suonando
    af::Engine offline;
    const int block = 512;
    offline.prepare (sampleRate, block);
    offline.setScene ((af::SceneId) (int) apvts.getRawParameterValue (ParamID::scene)->load());
    offline.setSeed ((uint32_t) (int) apvts.getRawParameterValue (ParamID::seed)->load());
    offline.reset();

    auto p = currentParams();
    p.autoGen = true;    // in export gli eventi li genera sempre il motore

    const float width = apvts.getRawParameterValue (ParamID::width)->load();
    const float gain  = af::dbToGain (apvts.getRawParameterValue (ParamID::level)->load());

    AudioBuffer<float> out (2, total);
    out.clear();

    // un secondo di pre-roll scartato: il file non parte mai "da fermo"
    AudioBuffer<float> tmp (2, block);
    for (int done = 0; done < (int) sampleRate; done += block)
        offline.render (tmp.getWritePointer (0), tmp.getWritePointer (1),
                        jmin (block, (int) sampleRate - done), p);

    for (int done = 0; done < total; done += block)
    {
        const int count = jmin (block, total - done);
        auto* l = tmp.getWritePointer (0);
        auto* r = tmp.getWritePointer (1);
        offline.render (l, r, count, p);

        for (int i = 0; i < count; ++i)
        {
            const float m = (l[i] + r[i]) * 0.5f;
            const float s = (l[i] - r[i]) * 0.5f * width;
            l[i] = af::softClip ((m + s) * gain);
            r[i] = af::softClip ((m - s) * gain);
        }
        out.copyFrom (0, done, l, count);
        out.copyFrom (1, done, r, count);
    }

    // fade in/out di 20 ms: nessun click ai bordi del sample
    const int fade = jmin ((int) (0.02 * sampleRate), total / 4);
    if (fade > 1)
    {
        out.applyGainRamp (0, fade, 0.0f, 1.0f);
        out.applyGainRamp (total - fade, fade, 1.0f, 0.0f);
    }

    destination.deleteFile();
    std::unique_ptr<FileOutputStream> stream (destination.createOutputStream());
    if (stream == nullptr) return false;

    WavAudioFormat wav;
    std::unique_ptr<AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sampleRate, 2, 24, {}, 0));
    if (writer == nullptr) return false;
    stream.release();   // il writer possiede lo stream

    return writer->writeFromAudioSampleBuffer (out, 0, total);
}

String AtmosForgeAudioProcessor::suggestedFileName (double seconds) const
{
    const int sceneIdx = (int) apvts.getRawParameterValue (ParamID::scene)->load();
    auto name = kSceneNames[jlimit (0, kSceneNames.size() - 1, sceneIdx)]
                    .replace (" / ", "-").replace (" ", "");
    const int seed = (int) apvts.getRawParameterValue (ParamID::seed)->load();
    return "ATMOS_" + name + "_s" + String (seed) + "_" + String (roundToInt (seconds)) + "s.wav";
}

//==============================================================================
void AtmosForgeAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AtmosForgeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessorEditor* AtmosForgeAudioProcessor::createEditor()
{
    return new AtmosForgeAudioProcessorEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AtmosForgeAudioProcessor();
}
