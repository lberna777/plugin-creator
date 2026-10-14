#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params.h"

// Catena (come un vero registratore a tre testine, agganciato alla timeline del DAW):
//  REGISTRAZIONE: in → livello → codifica NR → pre-enfasi → HX Pro → [×4: isteresi] → sensibilità del nastro
//  NASTRO:        testina di cancellazione (in anticipo di ~60 ms) + testina di registrazione, a passate
//  MONITOR:       SOURCE = la registrazione dal vivo · TAPE = il nastro letto alla posizione del DAW (± pitch)
//  RIPRODUZIONE:  trasporto (wow/flutter, azimut) → fruscio → dropout → basse → acuti → bias → de-enfasi → decodifica NR
//  USCITA:        output → mix con il dry allineato in latenza

namespace
{
constexpr int osFactorLog2 = 2;
constexpr double emphasisBoostDb = 10.0;
constexpr double transportCenterSeconds = 0.005;
constexpr double hissMaxDb = -42.0, hissRangeDb = 40.0;
constexpr double biasWidthAmount = 0.12, biasShelfDb = 3.0, biasShelfHz = 8000.0;
constexpr double ageHfLoss = 0.35;
constexpr double calibLevel = 0.01;
constexpr double hxAmount = 2.0, hxCornerHz = 4000.0;

// testine: distanza cancellazione→registrazione e cancellazione incompleta
constexpr double eraseGapSeconds = 0.06;        // a 4,76 cm/s; metà a 9,5
constexpr float  eraseResidue[3] = { 0.02f, 0.03f, 0.06f };   // -34 / -30 / -24 dB: il metallo si cancella peggio
constexpr float  biasErase = 0.3f;              // il bias di registrazione cancella in parte il vecchio

// errori di una piastra non calibrata, per tipo di nastro: livello (dB), bias (frazione), azimut (µs)
struct DeckError { double levelDb, bias, azimuthUs; };
constexpr DeckError deckError[3] = { { 1.2, -0.12, 6.0 }, { -1.5, 0.18, 6.0 }, { 2.0, -0.20, 6.0 } };

constexpr int stateMagic = 0x43363053;          // "C60S"
constexpr int stateVersion = 1;

double speedFactorOf (int speedIdx) { return speedIdx == 1 ? 2.0 : 1.0; }
}

C60AudioProcessor::C60AudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pInput = raw (ParamID::input);     pTape = raw (ParamID::tape);         pSpeed = raw (ParamID::speed);
    pBias = raw (ParamID::bias);       pWow = raw (ParamID::wow);           pFlutter = raw (ParamID::flutter);
    pHiss = raw (ParamID::hiss);       pAge = raw (ParamID::age);           pOutput = raw (ParamID::output);
    pAutoGain = raw (ParamID::autoGain); pMix = raw (ParamID::mix);         pBypass = raw (ParamID::bypass);
    pMonitor = raw (ParamID::monitor); pRec = raw (ParamID::rec);           pAutoPunch = raw (ParamID::autoPunch);
    pRecMute = raw (ParamID::recMute); pFade = raw (ParamID::fade);         pPitch = raw (ParamID::pitch);
    pNr = raw (ParamID::nr);           pHx = raw (ParamID::hxPro);          pAzimuth = raw (ParamID::azimuth);
    pAutoAz = raw (ParamID::autoAz);
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

    osLatency = (int) std::lround (os->getLatencyInSamples());
    center = (int) std::ceil (transportCenterSeconds * fs);
    latency = osLatency + center;
    setLatencySamples (latency);

    const double fsOS = fs * (1 << osFactorLog2);
    for (int c = 0; c < maxCh; ++c)
    {
        hyst[c].prepare (fsOS);
        pre[c].reset();
        nrEnc[c].prepare (fs);
        hxSplit[c].setHighPass (fs, hxCornerHz); hxSplit[c].reset();
        hxRestore[c].setHighPass (fs, hxCornerHz); hxRestore[c].reset();
        hxEnv[c].prepare (fs, 1.0, 30.0);
        std::fill (std::begin (hxRing[c]), std::end (hxRing[c]), 1.0f);
        dryDelay[c].prepare (latency + chunk + 8);
    }
    hxW = 0;

    {
        std::lock_guard<std::mutex> lk (tape.lock());
        tape.allocate (fs, numCh);
    }
    applyPendingTape();

    if (std::abs (calibratedFs - fs) > 0.5) calibrateTables();

    play.prepare (fs, numCh, center);

    const float dbIn = pInput->load(), dbOut = pOutput->load();
    inGain.reset (fs, 0.05);  inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (dbIn));
    outGain.reset (fs, 0.05); outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (dbOut - (pAutoGain->load() > 0.5f ? dbIn : 0.f)));
    wet.reset (fs, 0.03);     wet.setCurrentAndTargetValue (pBypass->load() > 0.5f ? 0.f : pMix->load() * 0.01f);
    wowAmt.reset (fs, 0.1);   wowAmt.setCurrentAndTargetValue (pWow->load() * 0.01f);
    flutAmt.reset (fs, 0.1);  flutAmt.setCurrentAndTargetValue (pFlutter->load() * 0.01f);
    hissAmt.reset (fs, 0.05); hissAmt.setCurrentAndTargetValue (pHiss->load() * 0.01f);
    ageAmt.reset (fs, 0.1);   ageAmt.setCurrentAndTargetValue (pAge->load() * 0.01f);
    biasAmt.reset (fs, 0.05); biasAmt.setCurrentAndTargetValue (pBias->load() * 0.01f);
    rate.reset (fs, 0.2);     rate.setCurrentAndTargetValue (1.f + pPitch->load() * 0.01f);
    monitorTape.reset (fs, 0.02); monitorTape.setCurrentAndTargetValue (pMonitor->load() > 0.5f ? 1.f : 0.f);

    curType = curSpeed = -1;
    updateFormula ((int) pTape->load(), (int) pSpeed->load());
    for (int c = 0; c < maxCh; ++c) nrEnc[c].setMode ((int) pNr->load());
    play.setNoiseReduction ((int) pNr->load());

    gRec = gErase = 0; wasRecordingTarget = false; expectedDaw = -1; readPos = 0;
}

void C60AudioProcessor::calibrateTables()
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
            const double sp = speedFactorOf (s);
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

    curType = type; curSpeed = speedIdx;
    const auto& f = c60::tapeFormula (type);
    const double tauPole = f.emphasisTau / juce::Decibels::decibelsToGain (emphasisBoostDb);
    for (int c = 0; c < maxCh; ++c) pre[c].set (fs, f.emphasisTau, tauPole);
    hystDrive = f.drive;
    hissTypeGain = juce::Decibels::decibelsToGain (f.hissDb - (speedIdx == 1 ? 3.0 : 0.0)) * hissCal[type][speedIdx];
    play.setFormula (type, speedIdx, emphasisBoostDb);
}

double C60AudioProcessor::hissGainFor (double h) const
{
    if (h <= 0.0) return 0.0;
    return hissTypeGain * std::pow (10.0, (hissMaxDb - hissRangeDb * (1.0 - h)) / 20.0) * std::min (1.0, h * 20.0);
}

//==============================================================================
void C60AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int c = numIn; c < numOut; ++c) buffer.clear (c, 0, buffer.getNumSamples());
    const int numCh = juce::jmin (numOut, maxCh, buffer.getNumChannels(), (int) chunkBuf.getNumChannels());
    const int numSamples = buffer.getNumSamples();

    updateFormula ((int) pTape->load(), (int) pSpeed->load());
    const int nrMode = (int) pNr->load();
    for (int c = 0; c < maxCh; ++c) nrEnc[c].setMode (nrMode);
    play.setNoiseReduction (nrMode);

    const float dbIn = pInput->load();
    inGain.setTargetValue (juce::Decibels::decibelsToGain (dbIn));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load() - (pAutoGain->load() > 0.5f ? dbIn : 0.f)));
    wet.setTargetValue (pBypass->load() > 0.5f ? 0.f : pMix->load() * 0.01f);
    wowAmt.setTargetValue (pWow->load() * 0.01f);
    flutAmt.setTargetValue (pFlutter->load() * 0.01f);
    hissAmt.setTargetValue (pHiss->load() * 0.01f);
    ageAmt.setTargetValue (pAge->load() * 0.01f);
    biasAmt.setTargetValue (pBias->load() * 0.01f);
    rate.setTargetValue (1.f + pPitch->load() * 0.01f);
    monitorTape.setTargetValue (pMonitor->load() > 0.5f ? 1.f : 0.f);

    // posizione del DAW: il nastro la segue (salti, loop e ripartenze riallineano la lettura)
    bool playing = false;
    int64_t daw = 0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto t = pos->getTimeInSamples())
            {
                playing = pos->getIsPlaying();
                daw = *t;
            }
    if (playing)
    {
        if (daw != expectedDaw) readPos = (double) (daw - osLatency);
        expectedDaw = daw + numSamples;
    }
    else
        expectedDaw = -1;

    std::unique_lock<std::mutex> lk (tape.lock(), std::try_to_lock);
    const bool locked = lk.owns_lock() && tape.length() > 0;

    float* chans[maxCh] {};
    float peak[maxCh] {};
    for (int start = 0; start < numSamples; start += chunk)
    {
        const int n = juce::jmin (chunk, numSamples - start);
        for (int c = 0; c < numCh; ++c) chans[c] = buffer.getWritePointer (c, start);
        processChunk (chans, numCh, n, playing, daw + start, locked);
        for (int c = 0; c < numCh; ++c)
            for (int i = 0; i < n; ++i) peak[c] = juce::jmax (peak[c], std::abs (chans[c][i]));
    }

    tapePos.store (playing ? daw : tapePos.load());
    playingFlag.store (playing);
    recordingFlag.store (gRec > 0.f);
    outLevelL.store (peak[0]);
    outLevelR.store (numCh > 1 ? peak[1] : peak[0]);
}

void C60AudioProcessor::processChunk (float* const* ch, int numCh, int n, bool playing, int64_t dawStart, bool locked)
{
    float gIn[chunk], gOut[chunk], gWet[chunk], gWow[chunk], gFlut[chunk], gHiss[chunk], gRate[chunk], gMon[chunk];
    for (int i = 0; i < n; ++i)
    {
        gIn[i] = inGain.getNextValue();   gOut[i] = outGain.getNextValue(); gWet[i] = wet.getNextValue();
        gWow[i] = wowAmt.getNextValue();  gFlut[i] = flutAmt.getNextValue();
        gHiss[i] = (float) hissGainFor (hissAmt.getNextValue());
        gRate[i] = rate.getNextValue();   gMon[i] = monitorTape.getNextValue();
    }
    ageAmt.skip (n);
    biasAmt.skip (n);

    const int type = curType;
    const auto& f = c60::tapeFormula (type);
    const auto& err = deckError[type];
    const bool cal = calibrated[type].load (std::memory_order_relaxed);
    const bool autoAz = pAutoAz->load() > 0.5f;
    const double ageNow = ageAmt.getCurrentValue();
    const double biasNow = juce::jlimit (-1.0, 1.0, (double) biasAmt.getCurrentValue() + (cal ? 0.0 : err.bias));
    const double sens = cal ? 1.0 : juce::Decibels::decibelsToGain (err.levelDb);

    play.setChunk (ageNow, biasNow, ageHfLoss, biasShelfDb, biasShelfHz);
    for (int c = 0; c < maxCh; ++c) hyst[c].setParams (f.sat, f.width + biasWidthAmount * biasNow);

    // 1) elettronica di registrazione: livello, NR, pre-enfasi, HX Pro (alleggerisce gli acuti forti)
    const bool hx = pHx->load() > 0.5f;
    constexpr int ringMask = hxRingSize - 1;
    for (int c = 0; c < numCh; ++c)
    {
        float* x = ch[c];
        float* y = chunkBuf.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            dryDelay[c].push (x[i]);
            double v = pre[c].process (nrEnc[c].encode (x[i] * gIn[i]));
            const double band = hxSplit[c].process (v);
            hxEnv[c].process (band);
            float k = 1.0f;
            if (hx)
            {
                k = (float) (1.0 / (1.0 + hxAmount * hxEnv[c].e));
                v -= (1.0 - k) * band;
            }
            hxRing[c][(hxW + i) & ringMask] = k;
            y[i] = (float) v;
        }
    }

    // 2) il nastro: isteresi sovracampionata
    {
        juce::dsp::AudioBlock<float> block (chunkBuf.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);
        auto up = os->processSamplesUp (block);
        const double norm = [&] {
            const double pos = (biasNow + 1.0) * 0.5 * (biasSteps - 1);
            const int i0 = juce::jlimit (0, biasSteps - 2, (int) pos);
            const double t = pos - i0;
            return hystNorm[type][i0] * (1 - t) + hystNorm[type][i0 + 1] * t;
        }();
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            float* s = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                s[i] = (float) (hyst[c].process (hystDrive * s[i]) * norm);
        }
        os->processSamplesDown (block);
    }

    // 3) HX Pro: restituisce gli acuti tolti (guadagno ritardato della latenza del sovracampionamento)
    for (int c = 0; c < numCh; ++c)
    {
        float* y = chunkBuf.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            const float k = hxRing[c][(hxW + i - osLatency) & ringMask];
            const double band = hxRestore[c].process (y[i]);
            y[i] = (float) ((y[i] + band * (1.0 / k - 1.0)) * sens);
        }
    }
    hxW = (hxW + n) & ringMask;

    // 4) testine, nastro e riproduzione
    const bool recArm = pRec->load() > 0.5f, autoPunch = pAutoPunch->load() > 0.5f, mute = pRecMute->load() > 0.5f;
    const float recStep = 1.0f / (float) (std::max (3.0f, pFade->load()) * 1e-3 * fs);
    const float eraseStep = 1.0f / (float) (0.003 * fs);
    const int64_t gap = (int64_t) std::llround (eraseGapSeconds / speedFactorOf (curSpeed) * fs);
    const int64_t in = punchIn.load(), out = punchOut.load();

    c60::FrameControl k;
    k.age = ageNow;
    k.autoAzimuth = autoAz;
    k.azStaticSeconds = pAzimuth->load() * 1e-6 + ((cal || autoAz) ? 0.0 : err.azimuthUs * 1e-6);

    for (int i = 0; i < n; ++i)
    {
        const int64_t pos = dawStart + i - osLatency;
        const bool target = locked && playing && recArm && pos >= 0 && pos < tape.length()
                            && (! autoPunch || (pos >= in && pos < out));
        if (target && ! wasRecordingTarget && gRec <= 0.f && gErase <= 0.f) tape.beginPass();
        wasRecordingTarget = target;

        if (! playing) gRec = gErase = 0.f;
        else
        {
            gRec   = target ? std::min (1.f, gRec + recStep)   : std::max (0.f, gRec - recStep);
            gErase = target ? std::min (1.f, gErase + eraseStep) : std::max (0.f, gErase - eraseStep);
        }

        if (locked && playing && (gRec > 0.f || gErase > 0.f))
        {
            tape.erase (pos + gap, gErase, eraseResidue[type]);
            for (int c = 0; c < numCh; ++c)
                tape.record (pos, c, mute ? 0.f : chunkBuf.getSample (c, i), gRec, biasErase);
        }

        const double tp = readPos;
        if (playing) readPos += gRate[i];

        float frame[maxCh];
        for (int c = 0; c < numCh; ++c)
        {
            const float live = chunkBuf.getSample (c, i);
            const float fromTape = (locked && playing) ? tape.readFrac (c, tp) : 0.f;
            frame[c] = live + gMon[i] * (fromTape - live);
        }

        k.wow = gWow[i]; k.flutter = gFlut[i]; k.hissGain = gHiss[i];
        k.moving = playing || gMon[i] < 0.5f;     // in SOURCE il nastro "gira" sempre, in TAPE solo col DAW
        play.process (frame, k);

        for (int c = 0; c < numCh; ++c)
        {
            const float dry = dryDelay[c].readInt (latency + (n - 1 - i));
            ch[c][i] = dry + gWet[i] * (frame[c] * gOut[i] - dry);
        }
    }
}

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

//==============================================================================
bool C60AudioProcessor::undoLastPass()
{
    std::lock_guard<std::mutex> lk (tape.lock());
    return tape.undoLastPass();
}

void C60AudioProcessor::eraseTape()
{
    std::lock_guard<std::mutex> lk (tape.lock());
    tape.eraseAll();
}

void C60AudioProcessor::calibrate()
{
    calibrated[juce::jlimit (0, 2, (int) pTape->load())].store (true);
}

C60AudioProcessor::Deck C60AudioProcessor::getDeckState() const
{
    Deck d;
    d.position = tapePos.load(); d.in = punchIn.load(); d.out = punchOut.load(); d.memo = memoPos.load();
    d.used = tape.used(); d.length = tape.length(); d.sampleRate = tape.sampleRate();
    d.playing = playingFlag.load(); d.recording = recordingFlag.load(); d.canUndo = tape.canUndo();
    d.calibrated = calibrated[juce::jlimit (0, 2, (int) pTape->load())].load();
    d.currentPass = tape.currentPass();
    return d;
}

// Esporta il nastro così come si sente in TAPE (riproduzione completa), WAV 24 bit.
bool C60AudioProcessor::exportTape (const juce::File& file, juce::String& error)
{
    const int64_t used = tape.used();
    if (used <= 0) { error = "Il nastro è vuoto."; return false; }

    const double sr = tape.sampleRate();
    const int nch = tape.channels();
    const int type = juce::jlimit (0, 2, (int) pTape->load());
    const bool cal = calibrated[type].load();
    const bool autoAz = pAutoAz->load() > 0.5f;
    const int ctr = (int) std::ceil (transportCenterSeconds * sr);

    c60::PlaybackChain chain;
    chain.prepare (sr, nch, ctr);
    chain.setFormula (type, (int) pSpeed->load(), emphasisBoostDb);
    chain.setNoiseReduction ((int) pNr->load());
    const double bias = juce::jlimit (-1.0, 1.0, pBias->load() * 0.01 + (cal ? 0.0 : deckError[type].bias));
    chain.setChunk (pAge->load() * 0.01, bias, ageHfLoss, biasShelfDb, biasShelfHz);

    c60::FrameControl k;
    k.wow = pWow->load() * 0.01; k.flutter = pFlutter->load() * 0.01; k.age = pAge->load() * 0.01;
    k.hissGain = std::abs (sr - fs) < 0.5 ? hissGainFor (pHiss->load() * 0.01) : 0.0;
    k.autoAzimuth = autoAz;
    k.azStaticSeconds = pAzimuth->load() * 1e-6 + ((cal || autoAz) ? 0.0 : deckError[type].azimuthUs * 1e-6);
    const float gain = juce::Decibels::decibelsToGain (pOutput->load() - (pAutoGain->load() > 0.5f ? pInput->load() : 0.f));

    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr) { error = "Non riesco a scrivere " + file.getFullPathName(); return false; }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sr, (unsigned) nch, 24, {}, 0));
    if (writer == nullptr) { error = "Impossibile creare il WAV."; return false; }
    stream.release();   // ora appartiene al writer

    constexpr int block = 4096;
    juce::AudioBuffer<float> buf (nch, block);
    int64_t skip = ctr;                                   // allinea: il trasporto ritarda di ctr campioni
    for (int64_t start = 0; start < used + ctr; start += block)
    {
        const int n = (int) std::min ((int64_t) block, used + ctr - start);
        buf.clear();
        {
            std::lock_guard<std::mutex> lk (tape.lock());
            const int64_t avail = std::max ((int64_t) 0, std::min ((int64_t) n, used - start));
            if (avail > 0) tape.copyOut (start, avail, buf.getArrayOfWritePointers(), nch);
        }
        float frame[maxCh];
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < nch; ++c) frame[c] = buf.getSample (c, i);
            chain.process (frame, k);
            for (int c = 0; c < nch; ++c) buf.setSample (c, i, frame[c] * gain);
        }
        const int from = (int) std::min ((int64_t) n, skip);
        skip -= from;
        if (n - from > 0) writer->writeFromAudioSampleBuffer (buf, from, n - from);
    }
    return true;
}

//==============================================================================
// Stato: [magic][versione][XML parametri + piastra][campioni usati][fs][canali][mappa collage][FLAC 24 bit]
void C60AudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    juce::ValueTree deck ("DECK");
    deck.setProperty ("in", (juce::int64) punchIn.load(), nullptr);
    deck.setProperty ("out", (juce::int64) punchOut.load(), nullptr);
    deck.setProperty ("memo", (juce::int64) memoPos.load(), nullptr);
    for (int t = 0; t < 3; ++t) deck.setProperty ("cal" + juce::String (t), calibrated[t].load(), nullptr);
    state.appendChild (deck, nullptr);

    juce::MemoryOutputStream mos (dest, false);
    mos.writeInt (stateMagic);
    mos.writeInt (stateVersion);
    mos.writeString (state.toXmlString());

    const int64_t used = tape.used();
    mos.writeInt64 (used);
    if (used <= 0) return;

    const double sr = tape.sampleRate();
    const int nch = tape.channels();
    mos.writeDouble (sr);
    mos.writeInt (nch);
    mos.writeInt (tape.cells());
    for (int i = 0; i < tape.cells(); ++i) mos.writeShort ((short) tape.passAt (i));

    juce::MemoryBlock flacData;
    {
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (new juce::MemoryOutputStream (flacData, false),
                                                                          sr, (unsigned) nch, 24, {}, 0));
        if (w != nullptr)
        {
            const int block = (int) sr;   // 1 s per volta: il lock dura poco
            juce::AudioBuffer<float> buf (nch, block);
            for (int64_t start = 0; start < used; start += block)
            {
                const int n = (int) std::min ((int64_t) block, used - start);
                {
                    std::lock_guard<std::mutex> lk (tape.lock());
                    tape.copyOut (start, n, buf.getArrayOfWritePointers(), nch);
                }
                buf.applyGain (0, n, 1.0f / c60::TapeMedium::fullScale);
                w->writeFromAudioSampleBuffer (buf, 0, n);
            }
        }
    }
    mos.writeInt64 ((juce::int64) flacData.getSize());
    mos.write (flacData.getData(), flacData.getSize());
}

void C60AudioProcessor::setStateInformation (const void* data, int size)
{
    juce::MemoryInputStream mis (data, (size_t) size, false);
    if (size < 8 || mis.readInt() != stateMagic)
    {
        // formato 0.1 (solo parametri)
        if (auto xml = getXmlFromBinary (data, size))
            if (xml->hasTagName (apvts.state.getType()))
                apvts.replaceState (juce::ValueTree::fromXml (*xml));
        return;
    }
    mis.readInt();   // versione
    auto state = juce::ValueTree::fromXml (mis.readString());
    if (! state.isValid()) return;

    auto deck = state.getChildWithName ("DECK");
    if (deck.isValid())
    {
        punchIn.store ((int64_t) (juce::int64) deck.getProperty ("in", 0));
        punchOut.store ((int64_t) (juce::int64) deck.getProperty ("out", 0));
        memoPos.store ((int64_t) (juce::int64) deck.getProperty ("memo", 0));
        for (int t = 0; t < 3; ++t) calibrated[t].store ((bool) deck.getProperty ("cal" + juce::String (t), false));
        state.removeChild (deck, nullptr);
    }
    apvts.replaceState (state);

    const int64_t used = mis.readInt64();
    if (used <= 0) { eraseTape(); return; }

    const double sr = mis.readDouble();
    const int nch = juce::jlimit (1, 2, mis.readInt());
    std::vector<uint16_t> cells ((size_t) juce::jmax (0, mis.readInt()));
    for (auto& c : cells) c = (uint16_t) mis.readShort();

    juce::MemoryBlock flacData;
    mis.readIntoMemoryBlock (flacData, (ssize_t) mis.readInt64());

    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> r (flac.createReaderFor (new juce::MemoryInputStream (flacData, false), true));
    if (r == nullptr) return;

    juce::AudioBuffer<float> audio (nch, (int) used);
    r->read (&audio, 0, (int) used, 0, true, nch > 1);
    audio.applyGain (c60::TapeMedium::fullScale);

    pendingTape = std::move (audio);
    pendingCells = std::move (cells);
    pendingFs = sr;
    hasPending.store (true);
    if (tape.length() > 0) applyPendingTape();   // già in esecuzione: carica subito
}

// Carica il nastro letto dal progetto, ricampionandolo al sample rate corrente se serve.
void C60AudioProcessor::applyPendingTape()
{
    if (! hasPending.load()) return;
    std::lock_guard<std::mutex> lk (tape.lock());
    const double hostFs = tape.length() > 0 ? tape.sampleRate() : pendingFs;
    const int nch = tape.length() > 0 ? tape.channels() : pendingTape.getNumChannels();
    tape.allocate (pendingFs, nch);
    tape.load (pendingTape.getArrayOfReadPointers(), pendingTape.getNumChannels(), pendingTape.getNumSamples(),
               pendingCells.data(), (int) pendingCells.size());
    tape.allocate (hostFs, nch);
    pendingTape.setSize (0, 0);
    pendingCells.clear();
    hasPending.store (false);
}

const juce::String C60AudioProcessor::getName() const { return JucePlugin_Name; }

juce::AudioProcessorEditor* C60AudioProcessor::createEditor() { return new C60AudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new C60AudioProcessor(); }
