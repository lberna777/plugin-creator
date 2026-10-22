#include "Machine.h"

namespace mp
{
void Machine::bind (juce::AudioProcessorValueTreeState& s)
{
    sWave.bind (s, pid::oscWave);        sTune.bind (s, pid::oscTune);
    sDetune.bind (s, pid::osc2Detune);   sGlide.bind (s, pid::glide);
    sSub.bind (s, pid::oscSub);          sNoise.bind (s, pid::oscNoise);
    sFold.bind (s, pid::fold);           sDrift.bind (s, pid::drift);
    sCutoff.bind (s, pid::cutoff);       sReso.bind (s, pid::reso);
    sFilterEnv.bind (s, pid::filterEnv); sDrive.bind (s, pid::filterDrive);
    sAttack.bind (s, pid::attack);       sDecay.bind (s, pid::decay);
    sSustain.bind (s, pid::sustain);     sDrone.bind (s, pid::drone);
    sLfoRate.bind (s, pid::lfoRate);     sLfoPitch.bind (s, pid::lfoPitch);
    sLfoCutoff.bind (s, pid::lfoCutoff); sLfoFold.bind (s, pid::lfoFold);
    sDelayTime.bind (s, pid::delayTime); sDelayFb.bind (s, pid::delayFb);
    sDelayMix.bind (s, pid::delayMix);   sSpaceSize.bind (s, pid::spaceSize);
    sSpaceMix.bind (s, pid::spaceMix);   sWidth.bind (s, pid::width);
    sVolume.bind (s, pid::volume);

    pLfoShape   = s.getRawParameterValue (pid::lfoShape);
    pChaos      = s.getRawParameterValue (pid::chaos);
    pSeqOn      = s.getRawParameterValue (pid::seqOn);
    pSeqRate    = s.getRawParameterValue (pid::seqRate);
    pSeqScale   = s.getRawParameterValue (pid::seqScale);
    pSeqLength  = s.getRawParameterValue (pid::seqLength);
    pSeqProb    = s.getRawParameterValue (pid::seqProb);
    pSeqMutate  = s.getRawParameterValue (pid::seqMutate);
    pSeqRatchet = s.getRawParameterValue (pid::seqRatchet);
    pSeqSwing   = s.getRawParameterValue (pid::seqSwing);
    for (int i = 0; i < pid::numSteps; ++i)
        pSteps[(size_t) i] = s.getRawParameterValue (pid::step (i));
}

void Machine::prepare (double sampleRate)
{
    fs = sampleRate;

    constexpr double ramp = 0.03;
    for (auto* p : { &sWave, &sTune, &sDetune, &sGlide, &sSub, &sNoise, &sFold, &sDrift, &sReso,
                     &sFilterEnv, &sDrive, &sSustain, &sLfoPitch, &sLfoCutoff, &sLfoFold,
                     &sDelayFb, &sDelayMix, &sSpaceSize, &sSpaceMix, &sWidth, &sVolume })
        p->prepare (fs, ramp);
    for (auto* p : { &sCutoff, &sAttack, &sDecay, &sLfoRate })
        p->prepare (fs, ramp);
    sDrone.prepare (fs, 0.08);          // crossfade VCA ↔ drone senza click
    sDelayTime.prepare (fs, 0.35);      // rampa lenta = pitch-shift "da nastro"

    delay.prepare (fs);
    space.prepare (fs);
    dcCoef = 1.0f - std::exp (-kTwoPi * 8.0f / (float) fs);
    drift1.prepare (fs, 0.41f, 0.35f);
    drift2.prepare (fs, 0.53f, 0.35f);
    drift3.prepare (fs, 0.77f, 0.60f);
    reset();
}

void Machine::reset()
{
    rng.seed (0x5EED1234u);
    osc1.reset (0.0f); osc2.reset (0.31f); sub.reset (0.0f);
    ladderL.reset(); ladderR.reset();
    env.reset(); lfo.reset();
    delay.reset(); space.reset();
    currentNote = targetNote = rootNote = 48.0f;
    accent = 1.0f; chaosCutoffOct = 0.0f; chaosDecayScale = 1.0f;
    pitchBend = modWheel = 0.0f;
    heldCount = 0;
    seqRunning = seqHostDriven = false;
    lastTick = noTick; freePpq = 0.0;
    gateTimer = ratchetLeft = ratchetTimer = ratchetInterval = 0;
    mutation.fill (0);
    dcL = dcR = 0.0f;
    peakHold = 0.0f;
    uiStep = -1; uiGate = 0.0f; uiPeak = 0.0f;
}

void Machine::readBlockParams() noexcept
{
    for (auto* p : { &sWave, &sTune, &sDetune, &sGlide, &sSub, &sNoise, &sFold, &sDrift, &sReso,
                     &sFilterEnv, &sDrive, &sSustain, &sDrone, &sLfoPitch, &sLfoCutoff, &sLfoFold,
                     &sDelayFb, &sDelayMix, &sSpaceSize, &sSpaceMix, &sWidth, &sVolume })
        p->beginBlock();
    for (auto* p : { &sCutoff, &sAttack, &sDecay, &sLfoRate, &sDelayTime })
        p->beginBlock();

    lfoShape     = (int) pLfoShape->load();
    chaos        = pChaos->load();
    seqOn        = pSeqOn->load() > 0.5f;
    seqRateIndex = juce::jlimit (0, (int) std::size (choices::seqRatePpq) - 1, (int) pSeqRate->load());
    seqScale     = (int) pSeqScale->load();
    seqLength    = juce::jlimit (1, pid::numSteps, (int) pSeqLength->load());
    seqProb      = pSeqProb->load();
    seqMutate    = pSeqMutate->load();
    seqRatchet   = pSeqRatchet->load();
    seqSwing     = pSeqSwing->load();

    if (seqMutate <= 0.0001f)
        mutation.fill (0);      // MUTATE a zero: la sequenza torna esattamente ai knob
}

void Machine::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, juce::AudioPlayHead* playHead)
{
    const int numSamples = buffer.getNumSamples();
    readBlockParams();

    hostPlaying = false;
    blockPpq = 0.0;
    if (playHead != nullptr)
        if (auto pos = playHead->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 400.0, *b);
            if (auto p = pos->getPpqPosition())
            {
                blockPpq    = *p;
                hostPlaying = pos->getIsPlaying();
            }
        }
    ppqPerSample = bpm / (60.0 * fs);

    int cursor = 0;
    for (const auto meta : midi)
    {
        const int pos = juce::jlimit (0, numSamples, meta.samplePosition);
        if (pos > cursor) { render (buffer, cursor, pos); cursor = pos; }
        handleMidi (meta.getMessage());
    }
    if (cursor < numSamples)
        render (buffer, cursor, numSamples);

    // Rete di sicurezza: uno stato non finito (non dovrebbe mai accadere) viene azzerato.
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            if (! std::isfinite (d[i]))
            {
                buffer.clear();
                ladderL.reset(); ladderR.reset(); delay.reset(); space.reset();
                return;
            }
    }
}

void Machine::handleMidi (const juce::MidiMessage& m) noexcept
{
    if (m.isNoteOn())
    {
        const int note = m.getNoteNumber();
        const bool wasEmpty = heldCount == 0;
        if (heldCount < (int) held.size())
            held[(size_t) heldCount++] = note;

        if (seqOn)
        {
            rootNote = (float) note;              // la nota traspone la sequenza
            if (wasEmpty && ! hostPlaying) { freePpq = 0.0; lastTick = noTick; }
            return;
        }

        targetNote = (float) note;
        if (env.isIdle()) currentNote = targetNote;
        accent = 0.55f + 0.45f * m.getFloatVelocity();
        env.gateOn();
    }
    else if (m.isNoteOff())
    {
        const int note = m.getNoteNumber();
        for (int i = 0; i < heldCount; ++i)
            if (held[(size_t) i] == note)
            {
                for (int j = i; j < heldCount - 1; ++j) held[(size_t) j] = held[(size_t) j + 1];
                --heldCount;
                break;
            }

        if (seqOn)
        {
            if (heldCount > 0) rootNote = (float) held[(size_t) heldCount - 1];
            return;
        }
        if (heldCount > 0) targetNote = (float) held[(size_t) heldCount - 1];   // legato, niente retrigger
        else               env.gateOff();
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        heldCount = 0;
        env.gateOff();
    }
    else if (m.isPitchWheel())
    {
        pitchBend = ((float) m.getPitchWheelValue() - 8192.0f) / 8192.0f;
    }
    else if (m.isControllerOfType (1))
    {
        modWheel = (float) m.getControllerValue() / 127.0f;
    }
}

void Machine::stopSequencer() noexcept
{
    env.gateOff();
    gateTimer = ratchetLeft = 0;
    uiStep = -1;
}

void Machine::clockSequencer (int sampleIndex) noexcept
{
    const bool running = seqOn && (hostPlaying || heldCount > 0);

    if (running != seqRunning || (running && hostPlaying != seqHostDriven))
    {
        if (! running && seqRunning) stopSequencer();
        if (running && ! hostPlaying) freePpq = 0.0;
        lastTick      = noTick;
        seqRunning    = running;
        seqHostDriven = hostPlaying;
    }
    if (! running)
        return;

    double ppq;
    if (hostPlaying) ppq = blockPpq + sampleIndex * ppqPerSample;
    else           { ppq = freePpq; freePpq += ppqPerSample; }

    const double stepPpq  = choices::seqRatePpq[seqRateIndex];
    const double pairLen  = stepPpq * 2.0;
    const double pairIdx  = std::floor (ppq / pairLen);
    const double inPair   = ppq - pairIdx * pairLen;
    const int64_t tick    = (int64_t) pairIdx * 2 + (inPair >= stepPpq * (1.0 + (double) seqSwing) ? 1 : 0);

    if (tick != lastTick)
    {
        lastTick = tick;
        triggerStep ((int) (((tick % seqLength) + seqLength) % seqLength));
    }

    if (gateTimer > 0 && --gateTimer == 0)
        env.gateOff();

    if (ratchetLeft > 0 && --ratchetTimer <= 0)
    {
        env.gateOn();
        gateTimer    = juce::jmax (1, ratchetInterval / 2);
        ratchetTimer = ratchetInterval;
        --ratchetLeft;
    }
}

void Machine::triggerStep (int step) noexcept
{
    uiStep = step;
    ratchetLeft = 0;

    if (rng.next01() >= seqProb)
    {
        env.gateOff();              // pausa
        gateTimer = 0;
        return;
    }

    // Turing machine: con probabilità MUTATE lo step cambia e la variazione resta "incisa".
    if (seqMutate > 0.0001f && rng.next01() < seqMutate * 0.6f)
        mutation[(size_t) step] = juce::roundToInt (rng.bipolar() * 12.0f * seqMutate);

    const float knob  = pSteps[(size_t) step]->load();
    const int   semis = quantizeToScale (knob + (float) mutation[(size_t) step], seqScale);
    targetNote = rootNote + (float) semis;
    if (env.isIdle()) currentNote = targetNote;

    chaosCutoffOct  = rng.bipolar() * chaos * 2.0f;
    chaosDecayScale = std::pow (2.0f, rng.bipolar() * chaos * 1.5f);
    accent          = 1.0f - chaos * 0.6f * rng.next01();

    const int stepSamples = juce::jmax (2, (int) (choices::seqRatePpq[seqRateIndex] / ppqPerSample));
    env.gateOn();

    if (rng.next01() < seqRatchet)
    {
        const int count  = rng.next01() < 0.5f ? 2 : 3;
        ratchetInterval  = juce::jmax (2, stepSamples / count);
        ratchetTimer     = ratchetInterval;
        ratchetLeft      = count - 1;
        gateTimer        = juce::jmax (1, ratchetInterval / 2);
    }
    else
    {
        gateTimer = juce::jmax (1, stepSamples / 2);
    }
}

void Machine::render (juce::AudioBuffer<float>& buffer, int start, int end) noexcept
{
    const int  numChannels = buffer.getNumChannels();
    auto*      outL = buffer.getWritePointer (0);
    auto*      outR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
    const float nyquistGuard = 0.42f * (float) fs;

    for (int i = start; i < end; ++i)
    {
        const float wave      = sWave.next();
        const float tune      = sTune.next();
        const float detune    = sDetune.next();
        const float glideMs   = sGlide.next();
        const float subLevel  = sSub.next();
        const float noise     = sNoise.next();
        const float foldAmt   = sFold.next();
        const float driftAmt  = sDrift.next();
        const float cutoff    = sCutoff.next();
        const float reso      = sReso.next();
        const float envAmt    = sFilterEnv.next();
        const float drive     = 1.0f + 5.0f * sDrive.next();
        const float attackMs  = sAttack.next();
        const float decayMs   = sDecay.next();
        const float sustain   = sSustain.next();
        const float droneMix  = sDrone.next();
        const float lfoRate   = sLfoRate.next();
        const float lfoPitch  = sLfoPitch.next();
        const float lfoCut    = sLfoCutoff.next();
        const float lfoFold   = sLfoFold.next();
        const float delayMs   = sDelayTime.next();
        const float delayFb   = sDelayFb.next();
        const float delayMix  = sDelayMix.next();
        const float spaceSize = sSpaceSize.next();
        const float spaceMix  = sSpaceMix.next();
        const float width     = sWidth.next();
        const float volumeDb  = sVolume.next();

        clockSequencer (i);

        // ---- modulazioni ----
        const float lfoOut = lfo.process (lfoRate, lfoShape, fs, rng);
        const float d1 = drift1.process (rng) * driftAmt;
        const float d2 = drift2.process (rng) * driftAmt;
        const float d3 = drift3.process (rng) * driftAmt;

        currentNote += (targetNote - currentNote) * onePoleCoef (glideMs * 0.001f, fs);

        const float pitch1 = currentNote + tune + pitchBend * 2.0f
                           + lfoPitch * lfoPitch * 12.0f * lfoOut + d1 * 0.3f;
        const float pitch2 = pitch1 + detune + (d2 - d1) * 0.3f;
        const float dt1 = juce::jmin (0.45f, semitonesToHz (pitch1) / (float) fs);
        const float dt2 = juce::jmin (0.45f, semitonesToHz (pitch2) / (float) fs);

        // ---- sorgenti ----
        const float o1 = osc1.process (dt1, wave);
        const float o2 = osc2.process (dt2, wave);
        const float so = sub.processSquare (dt1 * 0.5f) * subLevel * 0.5f;

        const float spread = 0.5f + 0.4f * width;
        float xL = (o1 * spread + o2 * (1.0f - spread)) * 0.55f + so + rng.bipolar() * noise * 0.5f;
        float xR = (o2 * spread + o1 * (1.0f - spread)) * 0.55f + so + rng.bipolar() * noise * 0.5f;

        const float foldTotal = juce::jlimit (0.0f, 1.0f, foldAmt + lfoFold * 0.5f * lfoOut + modWheel * 0.6f);
        xL = wavefold (xL, foldTotal);
        xR = wavefold (xR, foldTotal);

        // ---- inviluppo + filtro ----
        const float e = env.process (onePoleCoef (attackMs * 0.001f / 1.47f, fs),
                                     onePoleCoef (decayMs * chaosDecayScale * 0.001f / 6.9f, fs),
                                     sustain);

        const float octaves = envAmt * e * 6.0f + lfoCut * 3.0f * lfoOut + chaosCutoffOct
                            + d3 * 0.3f + (currentNote - 60.0f) * (0.4f / 12.0f);
        const float fc = cutoff * std::pow (2.0f, octaves);
        const float fcL = juce::jlimit (20.0f, nyquistGuard, fc * (1.0f - 0.02f * width));
        const float fcR = juce::jlimit (20.0f, nyquistGuard, fc * (1.0f + 0.02f * width));

        float l = ladderL.process (xL, fcL, reso, drive, fs);
        float r = ladderR.process (xR, fcR, reso, drive, fs);

        // ---- VCA: inviluppo con accento, oppure DRONE (aperto) ----
        const float vca = droneMix + (1.0f - droneMix) * e * accent;
        l *= vca;
        r *= vca;
        dcL += (l - dcL) * dcCoef;  l -= dcL;
        dcR += (r - dcR) * dcCoef;  r -= dcR;

        // ---- FX ----
        delay.process (l, r, delayMs, delayFb, delayMix);
        space.process (l, r, spaceSize, spaceMix);

        // ---- width (M/S) + volume ----
        const float mid  = 0.5f * (l + r);
        const float side = 0.5f * (l - r) * width * 1.6f;
        const float gain = volumeDb <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (volumeDb);
        l = (mid + side) * gain;
        r = (mid - side) * gain;

        if (outR != nullptr) { outL[i] = l; outR[i] = r; }
        else                 { outL[i] = 0.5f * (l + r); }

        // ---- UI ----
        const float a = std::abs (outL[i]);
        peakHold = a > peakHold ? a : peakHold * 0.99995f;
        if (++scopeDecimator >= 2)
        {
            scopeDecimator = 0;
            uiScope[(size_t) scopeWrite].store (outL[i], std::memory_order_relaxed);
            scopeWrite = (scopeWrite + 1) % scopeSize;
        }
    }

    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, start, end - start);

    uiScopeWrite.store (scopeWrite, std::memory_order_relaxed);
    uiPeak.store (peakHold, std::memory_order_relaxed);
    uiGate.store (env.level, std::memory_order_relaxed);
}
} // namespace mp
