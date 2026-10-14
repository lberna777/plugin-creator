#include "PluginProcessor.h"
#include "Params.h"

// Catena (come un vero registratore):
//  in → livello di registrazione → pre-enfasi → [×4: isteresi del nastro] → trasporto (wow/flutter, azimut)
//     → + fruscio → dropout → head bump / perdite basse → perdite di testina (acuti) → bias → de-enfasi
//     → output → mix con il dry allineato in latenza

namespace
{
constexpr int osFactorLog2 = 2;                  // sovracampionamento ×4 per l'isteresi
constexpr double emphasisBoostDb = 10.0;         // quanto la pre-enfasi alza gli acuti prima del nastro
constexpr double transportCenterSeconds = 0.005; // ritardo centrale del trasporto (> escursione wow massima)
constexpr double azimuthMaxSeconds = 30e-6;      // errore di azimut a usura massima
constexpr double hissMaxDb = -42.0, hissRangeDb = 40.0;
constexpr double biasWidthAmount = 0.12, biasShelfDb = 3.0, biasShelfHz = 8000.0;
constexpr double ageHfLoss = 0.35;               // perdita relativa della frequenza di taglio a usura massima
constexpr double calibLevel = 0.01;              // -40 dBFS: livello di riferimento "piccolo segnale"

double speedFactor (int speedIdx) { return speedIdx == 1 ? 2.0 : 1.0; }
}

C60AudioProcessor::C60AudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    pInput    = apvts.getRawParameterValue (ParamID::input);
    pTape     = apvts.getRawParameterValue (ParamID::tape);
    pSpeed    = apvts.getRawParameterValue (ParamID::speed);
    pBias     = apvts.getRawParameterValue (ParamID::bias);
    pWow      = apvts.getRawParameterValue (ParamID::wow);
    pFlutter  = apvts.getRawParameterValue (ParamID::flutter);
    pHiss     = apvts.getRawParameterValue (ParamID::hiss);
    pAge      = apvts.getRawParameterValue (ParamID::age);
    pOutput   = apvts.getRawParameterValue (ParamID::output);
    pAutoGain = apvts.getRawParameterValue (ParamID::autoGain);
    pMix      = apvts.getRawParameterValue (ParamID::mix);
    pBypass   = apvts.getRawParameterValue (ParamID::bypass);
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamID::bypass));
}

bool C60AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
void C60AudioProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    const int numCh = juce::jlimit (1, maxCh, getTotalNumOutputChannels());

    os = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) numCh, osFactorLog2,
             juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    os->initProcessing ((size_t) chunk);
    chunkBuf.setSize (numCh, chunk, false, true, false);

    const int osLatency = (int) std::lround (os->getLatencyInSamples());
    const int center = (int) std::ceil (transportCenterSeconds * fs);
    latency = osLatency + center;
    wowCenter = (float) center;
    setLatencySamples (latency);

    const double fsOS = fs * (1 << osFactorLog2);
    for (int c = 0; c < maxCh; ++c)
    {
        hyst[c].prepare (fsOS);
        pre[c].reset(); de[c].reset();
        bump[c].reset(); dip[c].reset(); hp[c].reset(); lp[c].reset(); biasShelf[c].reset();
        dropLpState[c] = 0;
        transport[c].prepare (2 * center + 8);
        dryDelay[c].prepare (latency + chunk + 8);
    }
    dropLpCoeff = 1.0 - std::exp (-2 * c60::pi * 2500.0 / fs);
    drops.prepare (fs);
    azimuthDrift.prepare (fs, 0.15, 0xBEEFu);

    if (std::abs (calibratedFs - fs) > 0.5) calibrate();

    const float dbIn = pInput->load(), dbOut = pOutput->load();
    inGain.reset (fs, 0.05);  inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (dbIn));
    outGain.reset (fs, 0.05); outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (dbOut - (pAutoGain->load() > 0.5f ? dbIn : 0.f)));
    const float wetNow = pBypass->load() > 0.5f ? 0.f : pMix->load() * 0.01f;
    wet.reset (fs, 0.03);     wet.setCurrentAndTargetValue (wetNow);
    wowAmt.reset (fs, 0.1);   wowAmt.setCurrentAndTargetValue (pWow->load() * 0.01f);
    flutAmt.reset (fs, 0.1);  flutAmt.setCurrentAndTargetValue (pFlutter->load() * 0.01f);
    hissAmt.reset (fs, 0.05); hissAmt.setCurrentAndTargetValue (pHiss->load() * 0.01f);
    ageAmt.reset (fs, 0.1);   ageAmt.setCurrentAndTargetValue (pAge->load() * 0.01f);
    biasAmt.reset (fs, 0.05); biasAmt.setCurrentAndTargetValue (pBias->load() * 0.01f);

    curType = curSpeed = -1;
    updateFormula ((int) pTape->load(), (int) pSpeed->load());
    wf.prepare (fs, speedFactor (curSpeed));
    updateChunkCoefficients (ageAmt.getCurrentValue(), biasAmt.getCurrentValue());
}

// Guadagno a piccolo segnale dell'isteresi (per ogni tipo e bias) e livello del fruscio dopo i filtri:
// calcolati qui, una volta per sample rate, così il thread audio legge solo tabelle.
void C60AudioProcessor::calibrate()
{
    const double fsOS = fs * (1 << osFactorLog2);
    const int settle = (int) (0.01 * fsOS), measure = (int) (0.01 * fsOS);

    for (int t = 0; t < 3; ++t)
    {
        const auto& f = c60::tapeFormula (t);
        for (int b = 0; b < biasSteps; ++b)
        {
            const double bias = -1.0 + 2.0 * b / (biasSteps - 1);
            c60::Hysteresis h;
            h.prepare (fsOS);
            h.setParams (f.sat, f.width + biasWidthAmount * bias);
            double re = 0, im = 0;
            for (int n = 0; n < settle + measure; ++n)
            {
                const double ph = 2 * c60::pi * 1000.0 * n / fsOS;
                const double y = h.process (f.drive * calibLevel * std::sin (ph));
                if (n >= settle) { re += y * std::cos (ph); im += y * std::sin (ph); }
            }
            const double amp = 2 * std::sqrt (re * re + im * im) / measure;
            hystNorm[t][b] = amp > 0 ? calibLevel / amp : 1.0;
        }

        for (int s = 0; s < 2; ++s)
        {
            const double sp = speedFactor (s);
            c60::FirstOrderShelf d; d.set (fs, f.emphasisTau / juce::Decibels::decibelsToGain (emphasisBoostDb), f.emphasisTau);
            c60::Biquad bq[4];
            c60::setLowEnd (bq[0], bq[1], bq[2], fs, sp);
            bq[3].setLowPass (fs, std::min (f.hfCorner * (sp > 1 ? 1.35 : 1.0), 0.45 * fs), 0.8);
            c60::Rng r (0x1357u);
            const int len = (int) (0.5 * fs);
            double acc = 0;
            for (int n = 0; n < len; ++n)
            {
                double v = r.gauss();
                for (auto& q : bq) v = q.process (v);
                v = d.process (v);
                if (n > len / 5) acc += v * v;
            }
            const double rms = std::sqrt (acc / (len - len / 5 - 1));
            hissCal[t][s] = rms > 0 ? 1.0 / rms : 0.0;
        }
    }
    calibratedFs = fs;
}

void C60AudioProcessor::updateFormula (int type, int speedIdx)
{
    type = juce::jlimit (0, 2, type);
    speedIdx = juce::jlimit (0, 1, speedIdx);
    if (type == curType && speedIdx == curSpeed) return;

    const bool speedChanged = speedIdx != curSpeed;
    curType = type; curSpeed = speedIdx;
    const auto& f = c60::tapeFormula (type);
    const double sp = speedFactor (speedIdx);
    const double tauPole = f.emphasisTau / juce::Decibels::decibelsToGain (emphasisBoostDb);

    for (int c = 0; c < maxCh; ++c)
    {
        pre[c].set (fs, f.emphasisTau, tauPole);   // record: acuti su
        de[c].set (fs, tauPole, f.emphasisTau);    // playback: esatto reciproco
        c60::setLowEnd (hp[c], bump[c], dip[c], fs, sp);
    }
    hystDrive = f.drive;
    hissTypeGain = juce::Decibels::decibelsToGain (f.hissDb - (sp > 1 ? 3.0 : 0.0)) * hissCal[type][speedIdx];
    if (speedChanged) wf.prepare (fs, sp);
}

void C60AudioProcessor::updateChunkCoefficients (double ageNow, double biasNow)
{
    const auto& f = c60::tapeFormula (curType);
    const double sp = speedFactor (curSpeed);
    const double fc = std::min (f.hfCorner * (sp > 1 ? 1.35 : 1.0) * (1.0 - ageHfLoss * ageNow), 0.45 * fs);
    for (int c = 0; c < maxCh; ++c)
    {
        lp[c].setLowPass (fs, fc, 0.8);
        biasShelf[c].setHighShelf (fs, std::min (biasShelfHz, 0.4 * fs), -biasShelfDb * biasNow);
        hyst[c].setParams (f.sat, f.width + biasWidthAmount * biasNow);
    }
}

//==============================================================================
void C60AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int c = numIn; c < numOut; ++c) buffer.clear (c, 0, buffer.getNumSamples());
    const int numCh = juce::jmin (numOut, maxCh, buffer.getNumChannels(), (int) chunkBuf.getNumChannels());

    updateFormula ((int) pTape->load(), (int) pSpeed->load());

    const float dbIn = pInput->load();
    inGain.setTargetValue (juce::Decibels::decibelsToGain (dbIn));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load() - (pAutoGain->load() > 0.5f ? dbIn : 0.f)));
    wet.setTargetValue (pBypass->load() > 0.5f ? 0.f : pMix->load() * 0.01f);
    wowAmt.setTargetValue (pWow->load() * 0.01f);
    flutAmt.setTargetValue (pFlutter->load() * 0.01f);
    hissAmt.setTargetValue (pHiss->load() * 0.01f);
    ageAmt.setTargetValue (pAge->load() * 0.01f);
    biasAmt.setTargetValue (pBias->load() * 0.01f);

    float* chans[maxCh] {};
    float peak[maxCh] {};
    for (int start = 0; start < buffer.getNumSamples(); start += chunk)
    {
        const int n = juce::jmin (chunk, buffer.getNumSamples() - start);
        for (int c = 0; c < numCh; ++c) chans[c] = buffer.getWritePointer (c, start);
        processChunk (chans, numCh, n);
        for (int c = 0; c < numCh; ++c)
            for (int i = 0; i < n; ++i) peak[c] = juce::jmax (peak[c], std::abs (chans[c][i]));
    }
    outLevelL.store (peak[0]);
    outLevelR.store (numCh > 1 ? peak[1] : peak[0]);
}

void C60AudioProcessor::processChunk (float* const* ch, int numCh, int n)
{
    // valori smussati per-campione, condivisi tra i canali
    float gIn[chunk], gOut[chunk], gWet[chunk], gWow[chunk], gFlut[chunk], gHiss[chunk];
    for (int i = 0; i < n; ++i)
    {
        gIn[i] = inGain.getNextValue();   gOut[i] = outGain.getNextValue(); gWet[i] = wet.getNextValue();
        gWow[i] = wowAmt.getNextValue();  gFlut[i] = flutAmt.getNextValue();
        const float h = hissAmt.getNextValue();
        gHiss[i] = h > 0.f ? (float) (hissTypeGain * std::pow (10.0, (hissMaxDb - hissRangeDb * (1.0 - h)) / 20.0)) * juce::jmin (1.f, h * 20.f)
                           : 0.f;
    }
    ageAmt.skip (n);
    biasAmt.skip (n);
    const double ageNow = ageAmt.getCurrentValue(), biasNow = biasAmt.getCurrentValue();
    updateChunkCoefficients (ageNow, biasNow);

    // 1) registrazione: livello + pre-enfasi; il dry va nella sua linea di allineamento
    for (int c = 0; c < numCh; ++c)
    {
        float* x = ch[c];
        float* y = chunkBuf.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            dryDelay[c].push (x[i]);
            y[i] = (float) pre[c].process (x[i] * gIn[i]);
        }
    }

    // 2) nastro: isteresi sovracampionata
    {
        juce::dsp::AudioBlock<float> block (chunkBuf.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);
        auto up = os->processSamplesUp (block);
        const double norm = [&] {
            const double pos = (biasNow + 1.0) * 0.5 * (biasSteps - 1);
            const int i0 = juce::jlimit (0, biasSteps - 2, (int) pos);
            const double t = pos - i0;
            return hystNorm[curType][i0] * (1 - t) + hystNorm[curType][i0 + 1] * t;
        }();
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            float* s = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                s[i] = (float) (hyst[c].process (hystDrive * s[i]) * norm);
        }
        os->processSamplesDown (block);
    }

    // 3) trasporto + riproduzione
    const double maxMod = wowCenter - 4.0;
    for (int i = 0; i < n; ++i)
    {
        const double mod = juce::jlimit (-maxMod, maxMod, wf.next (gWow[i], gFlut[i]));
        const double drop = drops.next (ageNow);
        const double az = numCh > 1 ? ageNow * azimuthMaxSeconds * fs * (0.7 + 0.3 * azimuthDrift.next()) : 0.0;

        for (int c = 0; c < numCh; ++c)
        {
            transport[c].push (chunkBuf.getSample (c, i));
            double v = transport[c].read (wowCenter + mod + (c == 0 ? 0.5 : -0.5) * az);

            v += gHiss[i] * hissRng[c].gauss();

            const double d = drop * drops.channelSkew (c);
            dropLpState[c] += dropLpCoeff * (v - dropLpState[c]);
            v = ((1.0 - d) * v + d * dropLpState[c]) * (1.0 - 0.5 * d);

            v = hp[c].process (v);
            v = bump[c].process (v);
            v = dip[c].process (v);
            v = lp[c].process (v);
            v = biasShelf[c].process (v);
            v = de[c].process (v) * gOut[i];

            const float dry = dryDelay[c].readInt (latency + (n - 1 - i));
            ch[c][i] = dry + gWet[i] * ((float) v - dry);
        }
    }
}

// Bypass dell'host: passa il dry con la stessa latenza dichiarata (niente salti di allineamento).
void C60AudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processDry (buffer);
}

void C60AudioProcessor::processDry (juce::AudioBuffer<float>& buffer)
{
    const int numCh = juce::jmin (getTotalNumOutputChannels(), maxCh, buffer.getNumChannels());
    for (int c = 0; c < numCh; ++c)
    {
        float* x = buffer.getWritePointer (c);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            dryDelay[c].push (x[i]);
            x[i] = dryDelay[c].readInt (latency);
        }
    }
}

const juce::String C60AudioProcessor::getName() const { return JucePlugin_Name; }

//==============================================================================
juce::AudioProcessorEditor* C60AudioProcessor::createEditor()
{
    // UI provvisoria: quella fotorealistica arriva dopo la validazione del DSP (CLAUDE.md, loop di lavoro)
    return new juce::GenericAudioProcessorEditor (*this);
}

void C60AudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void C60AudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new C60AudioProcessor(); }
