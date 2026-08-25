#include "ChainDsp.h"

namespace vf
{

static constexpr float kMinDb = -100.0f;
static constexpr int   kMaxChannels = 8;

static inline float gainToDb (float gain) noexcept
{
    return juce::Decibels::gainToDecibels (gain, kMinDb);
}

//==============================================================================
// Biquad (RBJ cookbook). I coefficienti si scrivono in float già allocati:
// niente ReferenceCountedObject, niente malloc sul thread audio.
//==============================================================================
void ChainDsp::Biquad::setPeak (double sampleRate, float freq, float q, float gainDb) noexcept
{
    const auto A = std::pow (10.0, gainDb / 40.0);
    const auto w0 = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sampleRate * 0.49, (double) freq) / sampleRate;
    const auto cosw = std::cos (w0);
    const auto alpha = std::sin (w0) / (2.0 * juce::jmax (0.05, (double) q));

    const auto a0 = 1.0 + alpha / A;
    b0 = static_cast<float> ((1.0 + alpha * A) / a0);
    b1 = static_cast<float> ((-2.0 * cosw) / a0);
    b2 = static_cast<float> ((1.0 - alpha * A) / a0);
    a1 = static_cast<float> ((-2.0 * cosw) / a0);
    a2 = static_cast<float> ((1.0 - alpha / A) / a0);
}

void ChainDsp::Biquad::setShelf (double sampleRate, float freq, float q, float gainDb, bool high) noexcept
{
    const auto A = std::pow (10.0, gainDb / 40.0);
    const auto w0 = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sampleRate * 0.49, (double) freq) / sampleRate;
    const auto cosw = std::cos (w0);
    const auto alpha = std::sin (w0) / (2.0 * juce::jmax (0.05, (double) q));
    const auto twoSqrtAAlpha = 2.0 * std::sqrt (A) * alpha;

    double a0;
    if (high)
    {
        a0 = (A + 1.0) - (A - 1.0) * cosw + twoSqrtAAlpha;
        b0 = static_cast<float> (A * ((A + 1.0) + (A - 1.0) * cosw + twoSqrtAAlpha) / a0);
        b1 = static_cast<float> (-2.0 * A * ((A - 1.0) + (A + 1.0) * cosw) / a0);
        b2 = static_cast<float> (A * ((A + 1.0) + (A - 1.0) * cosw - twoSqrtAAlpha) / a0);
        a1 = static_cast<float> (2.0 * ((A - 1.0) - (A + 1.0) * cosw) / a0);
        a2 = static_cast<float> (((A + 1.0) - (A - 1.0) * cosw - twoSqrtAAlpha) / a0);
    }
    else
    {
        a0 = (A + 1.0) + (A - 1.0) * cosw + twoSqrtAAlpha;
        b0 = static_cast<float> (A * ((A + 1.0) - (A - 1.0) * cosw + twoSqrtAAlpha) / a0);
        b1 = static_cast<float> (2.0 * A * ((A - 1.0) - (A + 1.0) * cosw) / a0);
        b2 = static_cast<float> (A * ((A + 1.0) - (A - 1.0) * cosw - twoSqrtAAlpha) / a0);
        a1 = static_cast<float> (-2.0 * ((A - 1.0) + (A + 1.0) * cosw) / a0);
        a2 = static_cast<float> (((A + 1.0) + (A - 1.0) * cosw - twoSqrtAAlpha) / a0);
    }
}

//==============================================================================
void ChainDsp::DeEsser::prepare (const juce::dsp::ProcessSpec& spec)
{
    splitter.prepare (spec);
    splitter.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);   // si usa la overload a due uscite
    reset();
}

void ChainDsp::DeEsser::reset()
{
    splitter.reset();
    env.reset();
    lastGrDb = 0.0f;
}

float ChainDsp::Limiter::process (float peak, float ceilingGain, float attackCoeff, float releaseCoeff) noexcept
{
    // riduzione necessaria perché il picco non superi il tetto, con rilascio morbido
    const auto required = peak > ceilingGain ? ceilingGain / peak : 1.0f;
    const auto coeff = (required < envelope) ? attackCoeff : releaseCoeff;
    envelope = required + coeff * (envelope - required);
    return juce::jmin (envelope, 1.0f);
}

float ChainDsp::Compressor::computeGain (float levelDb, float threshold, float ratio, float knee) noexcept
{
    const auto over = levelDb - threshold;
    if (knee > 0.0f && std::abs (over) <= knee * 0.5f)
    {
        const auto x = over + knee * 0.5f;
        return -( (1.0f - 1.0f / ratio) * x * x / (2.0f * knee) );
    }
    return over <= 0.0f ? 0.0f : -over * (1.0f - 1.0f / ratio);
}

//==============================================================================
void ChainDsp::prepare (double newSampleRate, int maximumBlockSize, int channels)
{
    sampleRate  = newSampleRate;
    numChannels = juce::jmax (1, channels);

    const juce::dsp::ProcessSpec spec { sampleRate,
                                        static_cast<juce::uint32> (maximumBlockSize),
                                        static_cast<juce::uint32> (numChannels) };

    for (auto* f : { &hpf1, &hpf2, &keyHp, &keyLp, &revHp, &revLp, &dlyHp, &dlyLp, &fxHp, &fxLp })
    {
        f->prepare (spec);
        f->reset();
    }
    hpf1.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    hpf2.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    keyHp.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    keyLp.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    revHp.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    revLp.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    dlyHp.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    dlyLp.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    fxHp.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    fxLp.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    for (int band = 0; band < 3; ++band)
    {
        roomDetector[band].prepare (spec);
        roomDetector[band].setType (juce::dsp::StateVariableTPTFilterType::bandpass);
        roomDetector[band].reset();
        for (int ch = 0; ch < kChannels; ++ch)
        {
            roomFilters[band][ch].reset();
            subFilters[band][ch].reset();
            toneFilters[band][ch].reset();
        }
    }
    for (int ch = 0; ch < kChannels; ++ch)
    {
        airFilter[ch].reset();
        tiltLow[ch].reset();
        tiltHigh[ch].reset();
    }

    deEsser1.prepare (spec);
    deEsser2.prepare (spec);
    limiter.reset();

    reverb.prepare (spec);
    delayLine.prepare (spec);
    delayLine.setMaximumDelayInSamples (static_cast<int> (sampleRate * 2.0) + maximumBlockSize);
    delayLine.reset();

    doublerLine.prepare (spec);
    doublerLine.setMaximumDelayInSamples (static_cast<int> (sampleRate * 0.1) + maximumBlockSize);
    doublerLine.reset();
    doublerPhase = 0.0f;

    revPredelayLine.prepare (spec);
    revPredelayLine.setMaximumDelayInSamples (static_cast<int> (sampleRate * 0.12) + maximumBlockSize);
    revPredelayLine.reset();

    loudnessFollower.prepare (spec);
    loudnessFollower.setAttackTime (400.0f);
    loudnessFollower.setReleaseTime (400.0f);

    // scratch preallocati: in processBlock non si alloca
    dryScratch.setSize (numChannels, maximumBlockSize, false, false, true);
    revScratch.setSize (numChannels, maximumBlockSize, false, false, true);
    duckScratch.setSize (1, maximumBlockSize, false, false, true);

    for (auto* s : { &trimGain, &outputGain, &mixAmount, &driveAmount, &revSendGain, &dlySendGain,
                     &fxSendGain, &comp1Makeup, &comp2Makeup })
        s->reset (sampleRate, 0.02);
    delayTimeSamples.reset (sampleRate, 0.15);            // il tempo del delay si interpola: niente click (R7)
    for (auto& depth : roomDepthSmoothed) depth.reset (sampleRate, 0.03);
    roomCoeffCountdown = 0;
    gateOpen = false;

    comp1.reset();
    comp2.reset();
    gateEnv.reset();
    duckEnv.reset();
    gateGainDb = 0.0f;
    delayFeedbackState[0] = delayFeedbackState[1] = 0.0f;
    prepared = true;
    updateCoefficients();
    snapSmoothedOnNextUpdate = true;
}

void ChainDsp::reset()
{
    if (! prepared) return;
    for (auto* f : { &hpf1, &hpf2, &keyHp, &keyLp, &revHp, &revLp, &dlyHp, &dlyLp, &fxHp, &fxLp }) f->reset();
    for (int band = 0; band < 3; ++band)
    {
        roomDetector[band].reset();
        roomEnv[band].reset();
        for (int ch = 0; ch < kChannels; ++ch)
        {
            roomFilters[band][ch].reset();
            subFilters[band][ch].reset();
            toneFilters[band][ch].reset();
        }
    }
    for (int ch = 0; ch < kChannels; ++ch)
    {
        airFilter[ch].reset();
        tiltLow[ch].reset();
        tiltHigh[ch].reset();
    }
    deEsser1.reset();
    deEsser2.reset();
    limiter.reset();
    reverb.reset();
    delayLine.reset();
    doublerLine.reset();
    revPredelayLine.reset();
    comp1.reset();
    comp2.reset();
    gateEnv.reset();
    duckEnv.reset();
    delayFeedbackState[0] = delayFeedbackState[1] = 0.0f;
    if (prepared) updateCoefficients();
    snapSmoothedOnNextUpdate = true;
}

void ChainDsp::setSettings (const ChainSettings& newSettings)
{
    settings = newSettings;
    if (prepared) updateCoefficients();
}

//==============================================================================
void ChainDsp::updateCoefficients()
{
    const auto nyquist = static_cast<float> (sampleRate * 0.5);
    auto safeFreq = [nyquist] (float f) { return juce::jlimit (20.0f, nyquist * 0.95f, f); };

    hpf1.setCutoffFrequency (safeFreq (settings.hpfFreq));
    hpf2.setCutoffFrequency (safeFreq (settings.hpfFreq));
    keyHp.setCutoffFrequency (safeFreq (settings.gateKeyLo));
    keyLp.setCutoffFrequency (safeFreq (settings.gateKeyHi));

    for (int band = 0; band < 3; ++band)
        for (int ch = 0; ch < kChannels; ++ch)
        {
            subFilters[band][ch].setPeak (sampleRate, safeFreq (settings.subFreq[band]),
                                          juce::jlimit (0.2f, 12.0f, settings.subQ[band]), settings.subGain[band]);
            toneFilters[band][ch].setPeak (sampleRate, safeFreq (settings.toneFreq[band]),
                                           juce::jlimit (0.2f, 12.0f, settings.toneQ[band]), settings.toneGain[band]);
        }

    for (int band = 0; band < 3; ++band)
    {
        roomDetector[band].setCutoffFrequency (safeFreq (settings.roomFreq[band]));
        roomDetector[band].setResonance (juce::jlimit (0.5f, 12.0f, settings.roomQ[band]));
    }

    for (int ch = 0; ch < kChannels; ++ch)
    {
        airFilter[ch].setShelf (sampleRate, safeFreq (12000.0f), 0.707f,
                                settings.airOn ? settings.airGain : 0.0f, true);
        // tilt vero: due shelf speculari attorno a 700 Hz (B11)
        tiltLow[ch].setShelf (sampleRate, 700.0f, 0.707f, -settings.satTilt, false);
        tiltHigh[ch].setShelf (sampleRate, 700.0f, 0.707f, settings.satTilt, true);
    }

    deEsser1.setFrequency (safeFreq (settings.ds1Freq));
    deEsser2.setFrequency (safeFreq (settings.ds2Freq));

    revHp.setCutoffFrequency (safeFreq (settings.revHpf));
    revLp.setCutoffFrequency (safeFreq (settings.revLpf));
    dlyHp.setCutoffFrequency (safeFreq (settings.dlyHpf));
    dlyLp.setCutoffFrequency (safeFreq (settings.dlyLpf));
    fxHp.setCutoffFrequency (safeFreq (settings.fxHpf));
    fxLp.setCutoffFrequency (safeFreq (settings.fxLpf));

    juce::dsp::Reverb::Parameters reverbParams;
    reverbParams.roomSize   = juce::jlimit (0.0f, 1.0f, settings.revSize * 0.01f * 0.6f
                                                        + juce::jlimit (0.0f, 1.0f, settings.revDecay / 4.0f) * 0.4f);
    reverbParams.damping    = juce::jlimit (0.0f, 1.0f, 1.0f - settings.revLpf / 12000.0f);
    reverbParams.width      = juce::jlimit (0.0f, 1.0f, settings.revWidth * 0.01f);
    reverbParams.wetLevel   = 1.0f;      // il livello vero lo fa revSend
    reverbParams.dryLevel   = 0.0f;
    reverbParams.freezeMode = 0.0f;
    reverb.setParameters (reverbParams);

    trimGain.setTargetValue (juce::Decibels::decibelsToGain (settings.inTrim) * (settings.polarity ? -1.0f : 1.0f));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (settings.outGain));
    mixAmount.setTargetValue (juce::jlimit (0.0f, 1.0f, settings.mix * 0.01f));
    driveAmount.setTargetValue (settings.satOn ? juce::jlimit (0.0f, 1.0f, settings.satDrive * 0.01f) : 0.0f);
    revSendGain.setTargetValue (settings.revOn ? juce::Decibels::decibelsToGain (settings.revSend) : 0.0f);
    dlySendGain.setTargetValue (settings.dlyOn ? juce::Decibels::decibelsToGain (settings.dlySend) : 0.0f);
    fxSendGain.setTargetValue (settings.fxOn ? juce::Decibels::decibelsToGain (settings.fxSend) : 0.0f);
    comp1Makeup.setTargetValue (juce::Decibels::decibelsToGain (settings.c1Makeup));
    comp2Makeup.setTargetValue (juce::Decibels::decibelsToGain (settings.c2Makeup));

    if (snapSmoothedOnNextUpdate)
    {
        for (auto* smoothed : { &trimGain, &outputGain, &mixAmount, &driveAmount, &revSendGain,
                                &dlySendGain, &fxSendGain, &comp1Makeup, &comp2Makeup })
            smoothed->setCurrentAndTargetValue (smoothed->getTargetValue());
        for (auto& depth : roomDepthSmoothed) depth.setCurrentAndTargetValue (0.0f);
        snapSmoothedOnNextUpdate = false;
    }
}

//==============================================================================
void ChainDsp::processGate (juce::AudioBuffer<float>& buffer, int numSamples)
{
    const auto attack  = msToCoeff (settings.gateAtk, sampleRate);
    const auto release = msToCoeff (settings.gateRel, sampleRate);
    const auto openDb  = settings.gateThresh;
    const auto closeDb = settings.gateThresh - 6.0f;          // hysteresis: obbligatoria
    const auto rangeDb = settings.gateRange;
    const auto channels = buffer.getNumChannels();
    float grPeak = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // detector key-filtered: il rumore fuori dalla banda di voce non apre il gate
        float key = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            key += buffer.getSample (ch, sample);
        key /= static_cast<float> (channels);
        key = keyLp.processSample (0, keyHp.processSample (0, key));

        const auto level = gateEnv.process (std::abs (key), attack, release);
        const auto levelDb = gainToDb (level);
        // isteresi vera: sopra la soglia apre, sotto la soglia bassa chiude, in mezzo RESTA come sta
        // (aperto o chiuso), invece di congelare il guadagno a metà corsa
        if (levelDb > openDb)       gateOpen = true;
        else if (levelDb < closeDb) gateOpen = false;
        const auto targetDb = gateOpen ? 0.0f : -rangeDb;
        const auto coeff = (targetDb > gateGainDb) ? attack : release;
        gateGainDb = targetDb + coeff * (gateGainDb - targetDb);

        const auto gain = juce::Decibels::decibelsToGain (gateGainDb);
        for (int ch = 0; ch < channels; ++ch)
            buffer.setSample (ch, sample, buffer.getSample (ch, sample) * gain);

        grPeak = juce::jmax (grPeak, -gateGainDb);
    }
    meters.gateGr.store (grPeak);
}

void ChainDsp::processRoomTamer (juce::AudioBuffer<float>& buffer, int numSamples)
{
    /*  Notch DINAMICI, causali.

        Prima il detector percorreva tutto il blocco e poi si filtrava: il notch applicato ai primi
        campioni dipendeva da energia che arrivava DOPO, quindi il risultato cambiava con la dimensione
        del buffer (ascolto != bounce). Ora detector e filtro camminano insieme, la profondità è smussata
        e i coefficienti si aggiornano a intervalli regolari indipendenti dal blocco. Il detector legge
        l'INGRESSO dello stadio, uguale per tutte e tre le bande: prima la banda 2 leggeva il segnale
        già filtrato dalla banda 1.
    */
    const auto attack  = msToCoeff (10.0f, sampleRate);
    const auto release = msToCoeff (120.0f, sampleRate);
    const auto channels = juce::jmin (kMaxChannels, buffer.getNumChannels());
    constexpr int coefficientInterval = 16;          // control rate: aggiornare per campione costerebbe troppo

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < channels; ++ch) mono += buffer.getSample (ch, sample);
        mono /= static_cast<float> (channels);

        for (int band = 0; band < 3; ++band)
        {
            const auto qNorm = 1.0f / juce::jmax (0.5f, settings.roomQ[band]);
            const auto detected = roomDetector[band].processSample (0, mono) * qNorm;
            const auto level = roomEnv[band].process (std::abs (detected), attack, release);
            const auto overDb = gainToDb (level) - settings.roomThresh;
            const auto amount = juce::jlimit (0.0f, 1.0f, overDb / 12.0f);
            roomDepthSmoothed[band].setTargetValue (settings.roomDepth[band] * amount);
        }

        if (roomCoeffCountdown <= 0)
        {
            for (int band = 0; band < 3; ++band)
            {
                const auto depth = roomDepthSmoothed[band].getCurrentValue();
                for (int ch = 0; ch < channels; ++ch)
                    roomFilters[band][ch].setPeak (sampleRate,
                        juce::jlimit (20.0f, static_cast<float> (sampleRate * 0.45), settings.roomFreq[band]),
                        juce::jlimit (0.5f, 12.0f, settings.roomQ[band]), depth);
            }
            roomCoeffCountdown = coefficientInterval;
        }
        --roomCoeffCountdown;
        for (int band = 0; band < 3; ++band) roomDepthSmoothed[band].skip (1);

        for (int ch = 0; ch < channels; ++ch)
        {
            auto value = buffer.getSample (ch, sample);
            for (int band = 0; band < 3; ++band) value = roomFilters[band][ch].process (value);
            buffer.setSample (ch, sample, value);
        }
    }
}

void ChainDsp::processDeEsser (juce::AudioBuffer<float>& buffer, int numSamples, DeEsser& deEsser,
                               float threshDb, float rangeDb, bool split, std::atomic<float>& meter)
{
    // UNA sola passata del crossover per campione e per canale, con entrambe le uscite:
    // il detector guarda la banda alta vera, non il segnale intero (B2).
    const auto attack  = msToCoeff (0.5f, sampleRate);        // le sibilanti sono veloci
    const auto release = msToCoeff (40.0f, sampleRate);
    const auto channels = buffer.getNumChannels();
    float grPeak = 0.0f;
    float low[2] { 0.0f, 0.0f }, high[2] { 0.0f, 0.0f };

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float detector = 0.0f;
        const auto used = juce::jmin (2, channels);
        for (int ch = 0; ch < used; ++ch)
        {
            deEsser.splitter.processSample (ch, buffer.getSample (ch, sample), low[ch], high[ch]);
            detector += std::abs (high[ch]);
        }
        detector /= static_cast<float> (used);

        const auto level = deEsser.env.process (detector, attack, release);
        const auto overDb = gainToDb (level) - threshDb;
        const auto grDb = overDb > 0.0f ? -juce::jmin (rangeDb, overDb * 0.7f) : 0.0f;
        const auto gain = juce::Decibels::decibelsToGain (grDb);
        grPeak = juce::jmax (grPeak, -grDb);

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto index = juce::jmin (ch, used - 1);
            // split: si abbassa solo la banda alta · wide: si abbassa tutto
            buffer.setSample (ch, sample, split ? low[index] + high[index] * gain
                                                : (low[index] + high[index]) * gain);
        }
    }
    deEsser.lastGrDb = grPeak;
    meter.store (grPeak);
}

void ChainDsp::processCompressor (juce::AudioBuffer<float>& buffer, int numSamples, Compressor& compressor,
                                  float thresh, float ratio, float atkMs, float relMs,
                                  float knee, juce::SmoothedValue<float>& makeupSmoothed,
                                  std::atomic<float>& meter)
{
    const auto attack  = msToCoeff (atkMs, sampleRate);
    const auto release = msToCoeff (relMs, sampleRate);
    const auto channels = buffer.getNumChannels();
    float grPeak = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float detector = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            detector = juce::jmax (detector, std::abs (buffer.getSample (ch, sample)));

        const auto level  = compressor.env.process (detector, attack, release);
        const auto grDb   = compressor.computeGain (gainToDb (level), thresh,
                                                    juce::jmax (1.0f, ratio), knee);
        // il makeup e' smussato: un preset nuovo non deve far saltare il livello di colpo (R4)
        const auto gain   = juce::Decibels::decibelsToGain (grDb) * makeupSmoothed.getNextValue();
        grPeak = juce::jmax (grPeak, -grDb);

        for (int ch = 0; ch < channels; ++ch)
            buffer.setSample (ch, sample, buffer.getSample (ch, sample) * gain);
    }
    compressor.lastGrDb = grPeak;
    meter.store (grPeak);
}

void ChainDsp::processSaturation (juce::AudioBuffer<float>& buffer, int numSamples)
{
    // Il drive si legge UNA volta per campione, fuori dal ciclo dei canali:
    // altrimenti L e R ricevono punti diversi della rampa (B8).
    const auto channels = buffer.getNumChannels();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto drive = driveAmount.getNextValue();
        const auto amount = 1.0f + drive * 8.0f;

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto in = buffer.getSample (ch, sample);
            const auto x = in * amount;
            float shaped = 0.0f;

            switch (settings.satType)
            {
                case 1:  shaped = std::tanh (x) + 0.05f * x * x * std::exp (-std::abs (x)); break;  // tube: armonica pari
                case 2:  shaped = juce::jlimit (-1.0f, 1.0f, x * 1.2f - 0.1f * x * x * x);  break;  // transistor: più duro
                default: shaped = std::tanh (x * 0.8f);                                     break;  // tape: morbido
            }

            const auto wet = shaped / juce::jmax (1.0f, amount * 0.8f);
            buffer.setSample (ch, sample, juce::jmap (drive, in, wet));
        }
    }
}

void ChainDsp::processTilt (juce::AudioBuffer<float>& buffer, int numSamples)
{
    // Inclinazione spettrale: due shelf speculari a 700 Hz. Vive fuori dalla saturazione,
    // così la manopola fa qualcosa anche con SAT spento (B11).
    for (int ch = 0; ch < juce::jmin (kMaxChannels, buffer.getNumChannels()); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int sample = 0; sample < numSamples; ++sample)
            data[sample] = tiltHigh[ch].process (tiltLow[ch].process (data[sample]));
    }
}

void ChainDsp::processLimiter (juce::AudioBuffer<float>& buffer, int numSamples)
{
    const auto ceilingGain = juce::Decibels::decibelsToGain (settings.limCeiling);
    const auto attack  = msToCoeff (0.05f, sampleRate);
    const auto release = msToCoeff (juce::jlimit (1.0f, 1000.0f, settings.limRelease), sampleRate);
    const auto channels = buffer.getNumChannels();
    float grPeak = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            peak = juce::jmax (peak, std::abs (buffer.getSample (ch, sample)));

        const auto gain = limiter.process (peak, ceilingGain, attack, release);
        grPeak = juce::jmax (grPeak, -gainToDb (gain));

        for (int ch = 0; ch < channels; ++ch)
        {
            // il ceiling è un tetto: dopo la riduzione, un clamp di sicurezza sul campione
            const auto limited = buffer.getSample (ch, sample) * gain;
            buffer.setSample (ch, sample, juce::jlimit (-ceilingGain, ceilingGain, limited));
        }
    }
    meters.limGr.store (grPeak);
}

//==============================================================================
void ChainDsp::processSends (const juce::AudioBuffer<float>& source, juce::AudioBuffer<float>& destination,
                             int numSamples, double bpm)
{
    const auto channels = destination.getNumChannels();
    const auto sourceChannels = source.getNumChannels();

    // Un solo detector di ducking, calcolato per campione e condiviso dai tre bus:
    // prima delay e doubler leggevano un inviluppo che nessuno aggiornava (B5).
    const auto duckAttack  = msToCoeff (5.0f, sampleRate);
    const auto duckRelease = msToCoeff (180.0f, sampleRate);
    auto* duck = duckScratch.getWritePointer (0);
    for (int sample = 0; sample < numSamples; ++sample)
    {
        float level = 0.0f;
        for (int ch = 0; ch < sourceChannels; ++ch)
            level = juce::jmax (level, std::abs (source.getSample (ch, sample)));
        duck[sample] = juce::jlimit (0.0f, 1.0f, duckEnv.process (level, duckAttack, duckRelease) * 4.0f);
    }

    // ---- bus riverbero
    const auto mixScale = juce::jlimit (0.0f, 1.0f, settings.mix * 0.01f);   // il MIX scala anche le mandate

    const bool reverbActive = settings.revOn || revSendGain.getCurrentValue() > 1.0e-5f;
    if (reverbActive)
    {
        const auto predelaySamples = juce::jlimit (0.0f, static_cast<float> (sampleRate * 0.1),
                                                   settings.revPredelay * 0.001f * static_cast<float> (sampleRate));
        revPredelayLine.setDelay (predelaySamples);

        for (int ch = 0; ch < channels; ++ch)
        {
            auto* data = revScratch.getWritePointer (ch);
            for (int sample = 0; sample < numSamples; ++sample)
            {
                const auto in = source.getSample (juce::jmin (ch, sourceChannels - 1), sample);
                revPredelayLine.pushSample (ch, in);
                const auto delayed = revPredelayLine.popSample (ch, -1.0f, true);
                data[sample] = revLp.processSample (ch, revHp.processSample (ch, delayed));
            }
        }

        juce::dsp::AudioBlock<float> reverbBlock (revScratch.getArrayOfWritePointers(),
                                                  static_cast<size_t> (channels),
                                                  0, static_cast<size_t> (numSamples));
        juce::dsp::ProcessContextReplacing<float> reverbContext (reverbBlock);
        reverb.process (reverbContext);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto gain = revSendGain.getNextValue() * mixScale
                              * juce::Decibels::decibelsToGain (-settings.revDuck * duck[sample]);
            for (int ch = 0; ch < channels; ++ch)
                destination.addSample (ch, sample, revScratch.getSample (ch, sample) * gain);
        }
    }

    // ---- bus delay
    if (settings.dlyOn || dlySendGain.getCurrentValue() > 1.0e-5f)
    {
        auto delayMs = settings.dlyTime;
        if (settings.dlySync && bpm > 0.0)
            delayMs = static_cast<float> (60000.0 / bpm * settings.dlyDivisionBeats);   // la divisione del preset (B9)
        const auto delaySamples = juce::jlimit (1.0f, static_cast<float> (sampleRate * 2.0),
                                                delayMs * 0.001f * static_cast<float> (sampleRate));
        delayTimeSamples.setTargetValue (delaySamples);
        const auto feedback = juce::jlimit (0.0f, 0.85f, settings.dlyFeedback * 0.01f);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto gain = dlySendGain.getNextValue() * mixScale
                              * juce::Decibels::decibelsToGain (-settings.dlyDuck * duck[sample]);
            delayLine.setDelay (delayTimeSamples.getNextValue());   // interpolato: niente salto di intonazione

            for (int ch = 0; ch < channels; ++ch)
            {
                const auto input = source.getSample (juce::jmin (ch, sourceChannels - 1), sample);
                const auto delayed = delayLine.popSample (ch, -1.0f, true);
                const auto filtered = dlyLp.processSample (ch, dlyHp.processSample (ch, delayed));
                delayLine.pushSample (ch, input + filtered * feedback);
                destination.addSample (ch, sample, filtered * gain);
            }
        }
    }

    // ---- bus doubler: due ritardi corti diversi L/R, stonati di pochi cent da un LFO lento.
    // Allarga la voce ai lati senza toccare il centro, dove restano main e 808.
    if ((settings.fxOn || fxSendGain.getCurrentValue() > 1.0e-5f) && channels >= 1)
    {
        const auto lfoStep = static_cast<float> (juce::MathConstants<double>::twoPi * 0.7 / sampleRate);
        const auto detuneSamples = settings.fxDetune / 100.0f * 0.004f * static_cast<float> (sampleRate);
        const auto width = juce::jlimit (0.0f, 1.0f, settings.fxWidth * 0.01f);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            doublerPhase += lfoStep;
            if (doublerPhase > juce::MathConstants<float>::twoPi) doublerPhase -= juce::MathConstants<float>::twoPi;

            const auto gain = fxSendGain.getNextValue() * mixScale
                              * juce::Decibels::decibelsToGain (-settings.fxDuck * duck[sample]);

            float copies[2] { 0.0f, 0.0f };
            for (int ch = 0; ch < juce::jmin (kMaxChannels, channels); ++ch)
            {
                const auto input = source.getSample (juce::jmin (ch, sourceChannels - 1), sample);
                doublerLine.pushSample (ch, input);

                const auto baseMs  = (ch == 0) ? settings.fxTimeL : settings.fxTimeR;
                const auto wobble  = std::sin (doublerPhase + (ch == 0 ? 0.0f : juce::MathConstants<float>::pi));
                const auto delayed = doublerLine.popSample (ch,
                    juce::jlimit (1.0f, static_cast<float> (sampleRate * 0.09),
                                  baseMs * 0.001f * static_cast<float> (sampleRate) + wobble * detuneSamples), true);
                copies[ch] = fxLp.processSample (ch, fxHp.processSample (ch, delayed));
            }

            if (channels >= 2)
            {
                // width: quanto le due copie si allontanano dal centro. A 0 collassano in mono.
                const auto mid  = (copies[0] + copies[1]) * 0.5f;
                const auto side = (copies[0] - copies[1]) * 0.5f * width;
                destination.addSample (0, sample, (mid + side) * gain);
                destination.addSample (1, sample, (mid - side) * gain);
            }
            else
            {
                destination.addSample (0, sample, copies[0] * gain);
            }
        }
    }
}

//==============================================================================
void ChainDsp::processChunk (juce::AudioBuffer<float>& full, int startSample, int numSamples, double bpm)
{
    juce::ScopedNoDenormals noDenormals;
    const auto channels = juce::jmin (kMaxChannels, full.getNumChannels());
    if (numSamples <= 0 || channels == 0) return;

    // vista sul tratto: nessuna copia, nessuna allocazione
    float* channelPointers[kMaxChannels];
    for (int ch = 0; ch < channels; ++ch)
        channelPointers[ch] = full.getWritePointer (ch) + startSample;
    juce::AudioBuffer<float> buffer (channelPointers, channels, numSamples);

    // 1) trim + polarità
    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto gain = trimGain.getNextValue();
        for (int ch = 0; ch < channels; ++ch)
            buffer.setSample (ch, sample, buffer.getSample (ch, sample) * gain);
    }

    // il dry del MIX si copia DOPO trim e polarità: altrimenti invertire la polarità
    // metterebbe wet e dry in opposizione di fase (B6)
    for (int ch = 0; ch < juce::jmin (channels, dryScratch.getNumChannels()); ++ch)
        dryScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    // 2) gate key-filtered
    if (settings.gateOn) processGate (buffer, numSamples); else meters.gateGr.store (0.0f);

    // 3) HPF (12 o 24 dB/oct)
    if (settings.hpfOn)
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int sample = 0; sample < numSamples; ++sample)
            {
                auto x = hpf1.processSample (ch, data[sample]);
                if (settings.hpfSlope >= 24) x = hpf2.processSample (ch, x);
                data[sample] = x;
            }
        }

    // 4) room tamer dinamico
    if (settings.roomOn) processRoomTamer (buffer, numSamples);

    // 5) EQ sottrattiva
    if (settings.eqSubOn)
        for (int band = 0; band < 3; ++band)
            for (int ch = 0; ch < juce::jmin (kMaxChannels, channels); ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                for (int sample = 0; sample < numSamples; ++sample)
                    data[sample] = subFilters[band][ch].process (data[sample]);
            }

    // 6) de-esser 1 (protegge i detector dei compressori)
    if (settings.ds1On) processDeEsser (buffer, numSamples, deEsser1, settings.ds1Thresh,
                                        settings.ds1Range, settings.ds1Split, meters.ds1Gr);
    else meters.ds1Gr.store (0.0f);

    // 7) compressore veloce
    if (settings.c1On) processCompressor (buffer, numSamples, comp1, settings.c1Thresh, settings.c1Ratio,
                                          settings.c1Atk, settings.c1Rel, settings.c1Knee,
                                          comp1Makeup, meters.comp1Gr);
    else meters.comp1Gr.store (0.0f);

    // 8) compressore lento (glue)
    if (settings.c2On) processCompressor (buffer, numSamples, comp2, settings.c2Thresh, settings.c2Ratio,
                                          settings.c2Atk, settings.c2Rel, settings.c2Knee,
                                          comp2Makeup, meters.comp2Gr);
    else meters.comp2Gr.store (0.0f);

    // 9) saturazione (+ inclinazione, che vale anche a SAT spento)
    if (settings.satOn) processSaturation (buffer, numSamples);
    if (settings.satTilt != 0.0f) processTilt (buffer, numSamples);

    // 10) EQ tonale + aria
    if (settings.eqToneOn)
    {
        for (int band = 0; band < 3; ++band)
            for (int ch = 0; ch < juce::jmin (kMaxChannels, channels); ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                for (int sample = 0; sample < numSamples; ++sample)
                    data[sample] = toneFilters[band][ch].process (data[sample]);
            }

        if (settings.airOn)
            for (int ch = 0; ch < juce::jmin (kMaxChannels, channels); ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                for (int sample = 0; sample < numSamples; ++sample)
                    data[sample] = airFilter[ch].process (data[sample]);
            }
    }

    // 11) de-esser 2 (dopo saturazione e boost)
    if (settings.ds2On) processDeEsser (buffer, numSamples, deEsser2, settings.ds2Thresh,
                                        settings.ds2Range, settings.ds2Split, meters.ds2Gr);
    else meters.ds2Gr.store (0.0f);

    // 12) MIX (dry parallelo della catena)
    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto wetAmount = mixAmount.getNextValue();
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto wet = buffer.getSample (ch, sample);
            const auto dry = dryScratch.getSample (ch, sample);
            buffer.setSample (ch, sample, wet * wetAmount + dry * (1.0f - wetAmount));
        }
    }

    // 13) mandate: bus PARALLELI, alimentati dall'uscita della catena, sommate PRIMA del limiter
    if (settings.revOn || settings.dlyOn || settings.fxOn
        || revSendGain.getCurrentValue() > 1.0e-5f || dlySendGain.getCurrentValue() > 1.0e-5f
        || fxSendGain.getCurrentValue() > 1.0e-5f)
    {
        for (int ch = 0; ch < juce::jmin (channels, dryScratch.getNumChannels()); ++ch)
            dryScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);
        processSends (dryScratch, buffer, numSamples, bpm);
    }

    // 14) output gain, poi il limiter come ULTIMO stadio: così il ceiling è davvero un tetto (B4)
    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto gain = outputGain.getNextValue();
        for (int ch = 0; ch < channels; ++ch)
            buffer.setSample (ch, sample, buffer.getSample (ch, sample) * gain);
    }

    if (settings.limOn) processLimiter (buffer, numSamples);
    else meters.limGr.store (0.0f);
}

double ChainDsp::getTailSeconds() const noexcept
{
    // riverbero: decay dichiarato + predelay. delay: quante ripetizioni servono a scendere di 60 dB.
    const auto reverbTail = settings.revOn ? settings.revDecay + settings.revPredelay * 0.001f : 0.0f;

    auto delayTail = 0.0f;
    if (settings.dlyOn)
    {
        const auto feedback = juce::jlimit (0.0f, 0.85f, settings.dlyFeedback * 0.01f);
        const auto repeats = feedback > 0.01f
                             ? juce::jlimit (1.0f, 40.0f, -60.0f / juce::Decibels::gainToDecibels (feedback))
                             : 1.0f;
        delayTail = settings.dlyTime * 0.001f * repeats;
    }
    return juce::jlimit (0.5, 20.0, static_cast<double> (juce::jmax (reverbTail, delayTail)) + 0.5);
}

//==============================================================================
void ChainDsp::process (juce::AudioBuffer<float>& buffer, double bpm)
{
    const auto total = buffer.getNumSamples();
    const auto channels = buffer.getNumChannels();
    if (total <= 0 || channels == 0) return;

    meters.inPeak.store (buffer.getMagnitude (0, total));

    // Un blocco più lungo di quello dichiarato in prepare non può essere ignorato né troncato:
    // si spezza in tratti della dimensione preparata, così esce processato tutto (B1).
    const auto maxChunk = juce::jmax (1, dryScratch.getNumSamples());
    for (int start = 0; start < total; start += maxChunk)
        processChunk (buffer, start, juce::jmin (maxChunk, total - start), bpm);

    meters.outPeak.store (buffer.getMagnitude (0, total));
}

} // namespace vf
