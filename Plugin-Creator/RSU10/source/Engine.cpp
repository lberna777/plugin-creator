#include "Engine.h"

namespace rsu
{
namespace
{
constexpr int kControlInterval = 16;          // ricalcolo di filtri e altezza ogni N campioni
constexpr float kKillMs = 4.0f;               // choke / furto di voce
constexpr float kMidiRibbonHoldMs = 300.0f;

inline float dbToGain (float db) noexcept { return db <= -59.9f ? 0.0f : std::pow (10.0f, db / 20.0f); }

// Interpolazione cubica di Hermite, indici fuori campione bloccati ai bordi
inline float readHermite (const float* d, int len, double pos) noexcept
{
    const int i = (int) std::floor (pos);
    const float t = float (pos - i);
    auto at = [d, len] (int k) noexcept { return d[juce::jlimit (0, len - 1, k)]; };
    const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

double gradeRate (Grade g) noexcept
{
    switch (g)
    {
        case Grade::Standard:  return 32000.0;
        case Grade::Long:      return 22050.0;
        case Grade::ExtraLong: return 11640.0;   // la frequenza più bassa dell'SU10
        case Grade::High:      break;
    }
    return 0.0;
}

float gradeBits (Grade g) noexcept
{
    return g == Grade::ExtraLong ? 2048.0f : g == Grade::Long ? 8192.0f : 0.0f;   // 12 e 14 bit
}
} // namespace

// ---------------------------------------------------------------- Svf

void Svf::set (float cutoff, float q, double rate) noexcept
{
    const float fc = juce::jlimit (16.0f, float (rate * 0.45), cutoff);
    const float g = std::tan (juce::MathConstants<float>::pi * fc / float (rate));
    k = 1.0f / juce::jmax (0.5f, q);
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

float Svf::process (float x, FilterType type) noexcept
{
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    switch (type)
    {
        case FilterType::LowPass:  return v2;
        case FilterType::HighPass: return x - k * v1 - v2;
        case FilterType::BandPass: return v1 * k;   // guadagno unitario al centro
        case FilterType::Off:      break;
    }
    return x;
}

double stretchHeadRate (Stretch mode, double regionSeconds, double regionFrames, double hostBpm, double hostRate)
{
    if (mode == Stretch::Off || regionSeconds <= 0.0 || hostRate <= 0.0)
        return 0.0;
    static constexpr int beatsFor[] { 0, 0, 1, 2, 4, 8, 16 };
    const int beats = mode == Stretch::Auto ? Sample::guessBeats (regionSeconds) : beatsFor[int (mode)];
    const double bpm = hostBpm > 0.0 ? hostBpm : Sample::bpmFor (regionSeconds, beats);
    const double targetSeconds = beats * 60.0 / bpm;
    return regionFrames / (targetSeconds * hostRate);
}

// ---------------------------------------------------------------- Voice

struct Engine::Voice
{
    bool active = false;
    int pad = 0, note = -1, layer = 0;
    uint64_t order = 0;
    Sample::Ptr sample;

    float velocity = 1.0f, velGain = 1.0f, semitoneOffset = 0.0f;
    double pos = 0.0, travelled = 0.0;
    double regionStart = 0.0, regionEnd = 0.0, loopA = 0.0, loopB = 0.0;
    int dir = 1;
    PlayMode mode = PlayMode::OneShot;
    bool released = false;

    juce::ADSR env;
    float killGain = 1.0f, killStep = 0.0f;

    Svf filter[2], ribbonFilter[2];
    FilterType filterType = FilterType::Off;
    int controlCountdown = 0;
    double baseRate = 1.0, pitchRate = 1.0;

    // Grade (lo-fi): campiona e tiene
    double gradePhase = 1.0, gradeStep = 0.0;
    float gradeHeld[2] {};
    float gradeQuant = 0.0f;

    // Stretch granulare: testina di lettura + due grani sfasati di mezzo grano
    bool stretching = false;
    double headRate = 0.0;
    struct Grain { double pos = 0.0; int age = 0; } grain[2];
    int grainLen = 2048;

    float fadeInFrames = 0.0f, fadeOutFrames = 0.0f;

    void kill (double rate) noexcept
    {
        if (killStep <= 0.0f)
            killStep = 1.0f / juce::jmax (1.0f, kKillMs * 0.001f * float (rate));
    }

    void stop() noexcept
    {
        active = false;
        sample = nullptr;   // il campione resta vivo nel posto o nella lista dei tenuti: niente free qui
    }
};

struct Engine::PadStage
{
    juce::AudioBuffer<float> buffer;
    bool used = false;
    juce::SmoothedValue<float> gainL, gainR;
    float compEnv = 0.0f;
};

// ---------------------------------------------------------------- Engine

Engine::Engine (PadParamStore& p, SampleSlots& s) : params (p), slots (s)
{
    for (int i = 0; i < kMaxVoices; ++i)
        voices.push_back (std::make_unique<Voice>());
    for (int i = 0; i < kNumPads; ++i)
        stages.push_back (std::make_unique<PadStage>());
}

Engine::~Engine() = default;

void Engine::prepare (double sampleRate, int block)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, block);

    for (auto& st : stages)
    {
        st->buffer.setSize (2, maxBlock);
        st->gainL.reset (sr, 0.02);
        st->gainR.reset (sr, 0.02);
        st->compEnv = 0.0f;
    }
    for (auto& v : voices)
    {
        v->stop();
        v->env.setSampleRate (sr);
    }
    scratchBuf.assign (size_t (maxBlock), 1.0f);
    mixL.setSize (2, maxBlock);
    recBuffer.setSize (2, int (kMaxRecSeconds * sr));
    recBuffer.clear();
}

// ---- dalla UI

void Engine::triggerFromUi (int pad, int velocity, bool on)
{
    const auto scope = uiFifo.write (1);
    if (scope.blockSize1 > 0)
        uiEvents[size_t (scope.startIndex1)] = { pad, velocity, on };
    else if (scope.blockSize2 > 0)
        uiEvents[size_t (scope.startIndex2)] = { pad, velocity, on };
}

void Engine::panic()
{
    triggerFromUi (-1, 0, false);
}

void Engine::startPreview (Sample::Ptr s)
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (s == nullptr)
        return;
    previewKeep.push_back ({ s, juce::Time::getMillisecondCounterHiRes() });
    previewStop = false;
    previewRequest.store (s.get());
}

void Engine::stopPreview()
{
    previewStop = true;
}

void Engine::purgePreviews()
{
    JUCE_ASSERT_MESSAGE_THREAD
    const double now = juce::Time::getMillisecondCounterHiRes();
    // Tiene sempre l'ultima; le altre dopo 2 s e quando il thread audio le ha mollate
    for (size_t i = 0; i + 1 < previewKeep.size();)
    {
        auto& [s, t] = previewKeep[i];
        if (now - t > 2000.0 && s->getReferenceCount() == 1)
            previewKeep.erase (previewKeep.begin() + (long) i);
        else
            ++i;
    }
}

void Engine::armRecording (RecSource src, float threshold)
{
    recSource = src;
    recThreshold = threshold;
    recPos = 0;
    recStopRequest = false;
    recStateAtomic = int (RecState::Armed);
}

void Engine::stopRecording()
{
    recStopRequest = true;
}

bool Engine::takeRecording (juce::AudioBuffer<float>& dest, double& rate)
{
    if (recState() != RecState::Done)
        return false;
    const int n = recPos.load();
    if (n > 0)
    {
        dest.setSize (2, n);
        for (int ch = 0; ch < 2; ++ch)
            dest.copyFrom (ch, 0, recBuffer, ch, 0, n);
    }
    rate = sr;
    recStateAtomic = int (RecState::Idle);
    return n > 0;
}

// ---- voci

Engine::Voice* Engine::allocateVoice()
{
    Voice* oldest = nullptr;
    for (auto& v : voices)
    {
        if (! v->active)
            return v.get();
        if (oldest == nullptr || v->order < oldest->order)
            oldest = v.get();
    }
    // Tutte occupate: si ruba la più vecchia (con 48 voci succede solo in casi estremi)
    oldest->stop();
    return oldest;
}

void Engine::noteOn (int pad, int velocity, float semitoneOffset, int note)
{
    if (pad < 0 || pad >= kNumPads)
        return;

    // Strato dalla velocity
    const int layers = slots.layerCount (pad);
    if (layers == 0)
        return;
    int layer = 0;
    const PP splits[] { PP::Split2, PP::Split3, PP::Split4 };
    for (int l = 1; l < layers; ++l)
        if (velocity >= params.getInt (pad, splits[l - 1]))
            layer = l;
    // Strato vuoto (es. solo 1 e 3 caricati): scende al primo pieno sotto
    while (layer > 0 && slots.get (pad, layer) == nullptr)
        --layer;
    Sample* s = slots.get (pad, layer);
    if (s == nullptr || s->length() < 4)
        return;

    // Choke: i pad dello stesso gruppo si zittiscono (charleston aperto/chiuso); MONO: anche se stesso
    const int choke = params.getInt (pad, PP::Choke);
    const bool mono = params.getInt (pad, PP::Mono) != 0;
    for (auto& v : voices)
    {
        if (! v->active)
            continue;
        if ((mono && v->pad == pad) || (choke > 0 && v->pad != pad && params.getInt (v->pad, PP::Choke) == choke))
            v->kill (sr);
    }

    auto* v = allocateVoice();
    if (v == nullptr)
        return;

    v->active = true;
    v->sample = s;
    v->pad = pad;
    v->note = note;
    v->layer = layer;
    v->order = ++voiceCounter;
    v->velocity = velocity / 127.0f;
    v->semitoneOffset = semitoneOffset;
    v->released = false;
    v->killGain = 1.0f;
    v->killStep = 0.0f;
    v->controlCountdown = 0;

    const double len = s->length();
    double a = params.get (pad, PP::Start) * len, b = params.get (pad, PP::End) * len;
    if (b < a)
        std::swap (a, b);
    if (b - a < 16.0)
        b = juce::jmin (len, a + 16.0);
    v->regionStart = a;
    v->regionEnd = b;

    v->dir = params.getInt (pad, PP::Reverse) != 0 ? -1 : 1;
    v->mode = PlayMode (params.getInt (pad, PP::PlayMode));
    const double loopFrac = params.get (pad, PP::LoopStart);
    if (v->dir > 0)
    {
        v->loopA = a + loopFrac * (b - a);
        v->loopB = b;
    }
    else
    {
        v->loopA = a;
        v->loopB = b - loopFrac * (b - a);
    }
    if (v->loopB - v->loopA < 16.0)
        v->loopA = v->loopB - 16.0;
    v->pos = v->dir > 0 ? a : b - 1.0;
    v->travelled = 0.0;

    const float vn = v->velocity;
    const float velAmt = params.get (pad, PP::VelToVolume);
    v->velGain = 1.0f - velAmt + velAmt * vn * vn;

    const double srcPerHost = s->rate() / sr;
    v->baseRate = srcPerHost;
    v->fadeInFrames = float (params.get (pad, PP::FadeIn) * 0.001 * s->rate());
    v->fadeOutFrames = float (params.get (pad, PP::FadeOut) * 0.001 * s->rate());

    juce::ADSR::Parameters ep;
    ep.attack  = params.get (pad, PP::Attack) * 0.001f;
    ep.decay   = params.get (pad, PP::Decay) * 0.001f;
    ep.sustain = params.get (pad, PP::Sustain);
    ep.release = params.get (pad, PP::Release) * 0.001f;
    v->env.setParameters (ep);
    v->env.reset();
    v->env.noteOn();

    for (auto& f : v->filter)       f.reset();
    for (auto& f : v->ribbonFilter) f.reset();

    const auto grade = Grade (params.getInt (pad, PP::Grade));
    const double gr = gradeRate (grade);
    v->gradeStep = gr > 0.0 ? juce::jmin (1.0, gr / sr) : 0.0;
    v->gradePhase = 1.0;
    v->gradeQuant = gradeBits (grade);

    const auto stretch = Stretch (params.getInt (pad, PP::Stretch));
    v->stretching = stretch != Stretch::Off;
    v->grainLen = juce::jmax (256, int (0.05 * sr));
    v->grain[0] = { v->pos, 0 };
    v->grain[1] = { v->pos, v->grainLen / 2 };

    padHit[pad] = vn;
    padLastLayer[pad] = layer;
}

void Engine::noteOff (int pad, int note)
{
    for (auto& v : voices)
        if (v->active && v->pad == pad && v->note == note && ! v->released)
            if (v->mode != PlayMode::OneShot)
            {
                v->released = true;
                v->env.noteOff();
            }
}

void Engine::handleMidi (const juce::MidiMessage& m, const EngineGlobals& g)
{
    if (m.isNoteOn())
    {
        const int n = m.getNoteNumber();
        if (g.scale)
            noteOn (juce::jlimit (0, kNumPads - 1, g.scalePad), m.getVelocity(), float (n - g.scaleRoot), n);
        else if (n >= kFirstNote && n < kFirstNote + kNumPads)
            noteOn (n - kFirstNote, m.getVelocity(), 0.0f, n);
    }
    else if (m.isNoteOff())
    {
        const int n = m.getNoteNumber();
        if (g.scale)
            noteOff (juce::jlimit (0, kNumPads - 1, g.scalePad), n);
        else if (n >= kFirstNote && n < kFirstNote + kNumPads)
            noteOff (n - kFirstNote, n);
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), val = m.getControllerValue();
        if (cc == 1)                    // rotella di modulazione: ribbon assoluto
        {
            midiRibbon = val / 127.0f;
            midiRibbonOn = val > 0;
            midiRibbonHold = 0;
        }
        else if (cc == 14)              // encoder della Mikro (Mikro Bridge, relativo)
        {
            midiRibbon = juce::jlimit (0.0f, 1.0f, midiRibbon + (val - 64) * 0.02f);
            midiRibbonHold = int (kMidiRibbonHoldMs * 0.001f * float (sr));
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (auto& v : voices)
                if (v->active)
                    v->kill (sr);
        }
    }
}

void Engine::updateRibbon (int n, const EngineGlobals& g)
{
    // Sorgente: il ribbon della UI/automazione ha la precedenza, poi il MIDI
    bool touch = g.ribbonTouch;
    float value = g.ribbon;
    if (! touch && (midiRibbonOn || midiRibbonHold > 0))
    {
        touch = true;
        value = midiRibbon;
    }
    midiRibbonHold = juce::jmax (0, midiRibbonHold - n);

    if (g.ribbonMode != lastMode)
    {
        ribbonLatched = false;
        lastMode = g.ribbonMode;
    }
    // HOLD: l'effetto resta dov'era quando si stacca il dito
    if (touch)
    {
        ribbonLatched = true;
        ribbonValue = value;
    }
    else if (! g.ribbonHold)
    {
        ribbonLatched = false;
    }
    const bool active = ribbonLatched;

    ribbonShown = active ? ribbonValue : value;
    ribbonTouchedShown = active;

    const float dt = float (n / sr);
    const float smooth = 1.0f - std::exp (-dt / 0.015f);

    // Scratch: dito fermo = disco fermo, movimento = velocità; senza dito gira normale
    float targetScratch = 1.0f;
    if (g.ribbonMode == RibbonMode::Scratch && active)
        targetScratch = touch ? juce::jlimit (-4.0f, 4.0f, (ribbonValue - ribbonPrev) / juce::jmax (1.0e-4f, dt) * 0.25f) : 0.0f;
    const float startScratch = scratchRate;
    for (int i = 0; i < n; ++i)
    {
        const float t = float (i + 1) / float (n);
        scratchBuf[size_t (i)] = startScratch + (targetScratch - startScratch) * smooth * t;
    }
    scratchRate = startScratch + (targetScratch - startScratch) * smooth;
    ribbonPrev = ribbonValue;

    const float targetPitch = (g.ribbonMode == RibbonMode::Pitch && active) ? (ribbonValue - 0.5f) * 2.0f * g.ribbonPitchRange : 0.0f;
    pitchSemis += (targetPitch - pitchSemis) * smooth;

    const float targetCut = (g.ribbonMode == RibbonMode::Filter && active) ? 80.0f * std::pow (250.0f, ribbonValue) : 20000.0f;
    ribbonCutoff += (targetCut - ribbonCutoff) * smooth;

    reverseNow = g.ribbonMode == RibbonMode::Reverse && active;
    scratchActive = (g.ribbonMode == RibbonMode::Scratch && active) || std::abs (scratchRate - 1.0f) > 1.0e-4f;

    const float targetXf = (g.ribbonMode == RibbonMode::Crossfade && active) ? ribbonValue : 0.0f;
    crossfade += (targetXf - crossfade) * smooth;
}

void Engine::render (int start, int num, const EngineGlobals& g)
{
    if (num <= 0)
        return;

    for (auto& vp : voices)
    {
        auto& v = *vp;
        if (! v.active)
            continue;

        const auto& audio = v.sample->audio();
        const int len = audio.getNumSamples();
        const float* src[2] { audio.getReadPointer (0), audio.getReadPointer (audio.getNumChannels() > 1 ? 1 : 0) };
        const bool stereo = audio.getNumChannels() > 1;

        auto& stage = *stages[size_t (v.pad)];
        if (! stage.used)
        {
            stage.buffer.clear (0, blockSize);
            stage.used = true;
        }
        float* outL = stage.buffer.getWritePointer (0, start);
        float* outR = stage.buffer.getWritePointer (1, start);

        const bool looping = v.mode == PlayMode::Loop;
        const int pad = v.pad;

        for (int i = 0; i < num; ++i)
        {
            // Parametri lenti: altezza, filtro, stretch
            if (--v.controlCountdown <= 0)
            {
                v.controlCountdown = kControlInterval;
                const float semis = params.get (pad, PP::Tune) + params.get (pad, PP::Fine) * 0.01f + v.semitoneOffset + pitchSemis;
                v.pitchRate = v.baseRate * std::pow (2.0, semis / 12.0);

                v.filterType = FilterType (params.getInt (pad, PP::FilterType));
                if (v.filterType != FilterType::Off)
                {
                    const float velMod = params.get (pad, PP::VelToFilter) * (v.velocity - 1.0f) * 5.0f;   // fino a 5 ottave giù
                    const float cutoff = params.get (pad, PP::Cutoff) * std::pow (2.0f, velMod);
                    const float q = 0.5f + params.get (pad, PP::Resonance) * 11.5f;
                    for (auto& f : v.filter)
                        f.set (cutoff, q, sr);
                }
                if (ribbonCutoff < 19000.0f)
                    for (auto& f : v.ribbonFilter)
                        f.set (ribbonCutoff, 0.9f, sr);

                if (v.stretching)
                {
                    const double regionSecs = (v.regionEnd - v.regionStart) / v.sample->rate();
                    v.headRate = stretchHeadRate (Stretch (params.getInt (pad, PP::Stretch)), regionSecs,
                                                  v.regionEnd - v.regionStart, g.hostBpm, sr);
                }
            }

            const float scratch = scratchBuf[size_t (start + i)];
            const int dirNow = reverseNow ? -v.dir : v.dir;
            const bool scratching = scratchActive;

            // ---- lettura
            float l = 0.0f, r = 0.0f;
            double step;
            if (v.stretching && v.headRate > 0.0)
            {
                step = v.headRate * dirNow * scratch;
                for (auto& gr : v.grain)
                {
                    const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * float (gr.age) / float (v.grainLen));
                    l += w * readHermite (src[0], len, gr.pos);
                    r += w * (stereo ? readHermite (src[1], len, gr.pos) : 0.0f);
                    gr.pos += v.pitchRate * dirNow * (scratching ? scratch : 1.0f);
                    if (looping)
                    {
                        if (gr.pos >= v.loopB) gr.pos -= v.loopB - v.loopA;
                        if (gr.pos < v.loopA && dirNow < 0) gr.pos += v.loopB - v.loopA;
                    }
                    if (++gr.age >= v.grainLen)
                    {
                        gr.age = 0;
                        gr.pos = v.pos;   // il grano riparte dalla testina
                    }
                }
                if (! stereo)
                    r = l;
            }
            else
            {
                step = v.pitchRate * dirNow * scratch;
                l = readHermite (src[0], len, v.pos);
                r = stereo ? readHermite (src[1], len, v.pos) : l;
            }

            // ---- fade ai bordi della regione
            float fade = 1.0f;
            if (v.fadeInFrames > 0.0f && v.travelled < v.fadeInFrames)
                fade *= float (v.travelled) / v.fadeInFrames;
            if (! looping && v.fadeOutFrames > 0.0f)
            {
                const double remaining = dirNow > 0 ? v.regionEnd - v.pos : v.pos - v.regionStart;
                if (remaining < v.fadeOutFrames)
                    fade *= float (juce::jmax (0.0, remaining)) / v.fadeOutFrames;
            }

            // ---- avanzamento e bordi
            v.pos += step;
            v.travelled += std::abs (step);
            bool ended = false;
            if (looping)
            {
                const double span = v.loopB - v.loopA;
                if (v.pos >= v.loopB) v.pos -= span;
                else if (v.pos < v.loopA && (v.dir < 0 || dirNow < 0)) v.pos += span;
            }
            else if (scratching)
            {
                v.pos = juce::jlimit (v.regionStart, v.regionEnd - 1.0, v.pos);   // lo scratch non chiude la voce
            }
            else if ((dirNow > 0 && v.pos >= v.regionEnd) || (dirNow < 0 && v.pos < v.regionStart))
            {
                ended = true;
            }

            // ---- grade: campiona e tiene + quantizzazione
            if (v.gradeStep > 0.0)
            {
                v.gradePhase += v.gradeStep;
                if (v.gradePhase >= 1.0)
                {
                    v.gradePhase -= 1.0;
                    v.gradeHeld[0] = l;
                    v.gradeHeld[1] = r;
                    if (v.gradeQuant > 0.0f)
                    {
                        v.gradeHeld[0] = std::round (l * v.gradeQuant) / v.gradeQuant;
                        v.gradeHeld[1] = std::round (r * v.gradeQuant) / v.gradeQuant;
                    }
                }
                l = v.gradeHeld[0];
                r = v.gradeHeld[1];
            }

            // ---- filtri
            if (v.filterType != FilterType::Off)
            {
                l = v.filter[0].process (l, v.filterType);
                r = v.filter[1].process (r, v.filterType);
            }
            if (ribbonCutoff < 19000.0f)
            {
                l = v.ribbonFilter[0].process (l, FilterType::LowPass);
                r = v.ribbonFilter[1].process (r, FilterType::LowPass);
            }

            // ---- inviluppo
            const float e = v.env.getNextSample();
            if (v.killStep > 0.0f)
            {
                v.killGain -= v.killStep;
                if (v.killGain <= 0.0f)
                    ended = true;
            }
            // Disco fermo sotto il dito = silenzio, non un valore costante
            const float scratchGain = scratching ? juce::jmin (1.0f, std::abs (scratch) * 4.0f) : 1.0f;
            const float gain = e * v.velGain * fade * scratchGain * juce::jmax (0.0f, v.killGain);
            outL[i] += l * gain;
            outR[i] += r * gain;

            if (! v.env.isActive())
                ended = true;
            if (ended)
            {
                v.stop();
                break;
            }
        }

        if (v.active)
            padPosition[v.pad] = float (v.pos / juce::jmax (1, len));
    }
}

void Engine::process (const OutputPointers& out, const float* const* input, int numInputChannels,
                      int numSamples, const juce::MidiBuffer& midi, const EngineGlobals& g)
{
    jassert (numSamples <= maxBlock);
    numSamples = juce::jmin (numSamples, maxBlock);

    // Eventi della UI (clic sui pad, panico)
    {
        const auto scope = uiFifo.read (uiFifo.getNumReady());
        auto apply = [this] (const UiEvent& e)
        {
            if (e.pad < 0)
            {
                for (auto& v : voices)
                    if (v->active)
                        v->kill (sr);
                return;
            }
            const int note = kFirstNote + e.pad;   // dalla UI il pad suona sé stesso, anche in modo scala
            if (e.on)
                noteOn (e.pad, e.velocity, 0.0f, note);
            else
                noteOff (e.pad, note);
        };
        for (int i = 0; i < scope.blockSize1; ++i) apply (uiEvents[size_t (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i) apply (uiEvents[size_t (scope.startIndex2 + i)]);
    }

    blockSize = numSamples;
    updateRibbon (numSamples, g);

    for (auto& st : stages)
        st->used = false;

    // Render a pezzi tra un evento MIDI e l'altro (precisione al campione)
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, numSamples, meta.samplePosition);
        render (pos, at - pos, g);
        pos = at;
        handleMidi (meta.getMessage(), g);
    }
    render (pos, numSamples - pos, g);

    // Pulisce le uscite
    for (int o = 0; o < kNumOutputs; ++o)
    {
        if (out.left[o] != nullptr)  juce::FloatVectorOperations::clear (out.left[o], numSamples);
        if (out.right[o] != nullptr) juce::FloatVectorOperations::clear (out.right[o], numSamples);
    }

    // ---- stadio di ogni pad: saturazione, compressore, volume, pan, uscita
    for (int p = 0; p < kNumPads; ++p)
    {
        auto& st = *stages[size_t (p)];
        const float volume = dbToGain (params.get (p, PP::Volume));
        const float pan = params.get (p, PP::Pan);
        const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        st.gainL.setTargetValue (volume * std::cos (angle) * juce::MathConstants<float>::sqrt2);
        st.gainR.setTargetValue (volume * std::sin (angle) * juce::MathConstants<float>::sqrt2);

        if (! st.used)
        {
            st.gainL.setCurrentAndTargetValue (st.gainL.getTargetValue());
            st.gainR.setCurrentAndTargetValue (st.gainR.getTargetValue());
            st.compEnv *= 0.5f;
            continue;
        }

        float* l = st.buffer.getWritePointer (0);
        float* r = st.buffer.getWritePointer (1);

        const float drive = params.get (p, PP::Drive);
        if (drive > 0.001f)
        {
            const float k = 1.0f + drive * 9.0f;
            const float norm = 1.0f / std::tanh (k);
            for (int i = 0; i < numSamples; ++i)
            {
                l[i] = std::tanh (k * l[i]) * norm * (1.0f - 0.35f * drive);
                r[i] = std::tanh (k * r[i]) * norm * (1.0f - 0.35f * drive);
            }
        }

        const float comp = params.get (p, PP::Comp);
        if (comp > 0.001f)
        {
            // Compressore a una manopola: più giri, più soglia bassa e rapporto alto
            const float thresholdDb = -6.0f - comp * 30.0f;
            const float ratio = 1.0f + comp * 7.0f;
            const float makeup = dbToGain (-thresholdDb * (1.0f - 1.0f / ratio) * 0.5f);
            const float att = 1.0f - std::exp (-1.0f / (0.003f * float (sr)));
            const float rel = 1.0f - std::exp (-1.0f / (0.08f * float (sr)));
            for (int i = 0; i < numSamples; ++i)
            {
                const float peak = juce::jmax (std::abs (l[i]), std::abs (r[i]));
                st.compEnv += (peak - st.compEnv) * (peak > st.compEnv ? att : rel);
                const float levelDb = juce::Decibels::gainToDecibels (st.compEnv, -100.0f);
                const float over = levelDb - thresholdDb;
                const float gr = over > 0.0f ? over * (1.0f - 1.0f / ratio) : 0.0f;
                const float gain = dbToGain (-gr) * makeup;
                l[i] *= gain;
                r[i] *= gain;
            }
        }

        int o = params.getInt (p, PP::Output);
        if (o < 0 || o >= kNumOutputs || out.left[o] == nullptr)
            o = 0;
        float* dl = out.left[o];
        float* dr = out.right[o] != nullptr ? out.right[o] : nullptr;
        for (int i = 0; i < numSamples; ++i)
        {
            const float gl = st.gainL.getNextValue(), grr = st.gainR.getNextValue();
            if (dl != nullptr) dl[i] += l[i] * gl;
            if (dr != nullptr) dr[i] += r[i] * grr;
        }
    }

    // ---- anteprima del browser (sull'uscita principale)
    if (previewStop.exchange (false))
    {
        previewNow = nullptr;
        previewRequest = nullptr;
    }
    if (auto* req = previewRequest.exchange (nullptr))
    {
        previewNow = req;
        previewPos = 0.0;
    }
    if (previewNow != nullptr && out.left[0] != nullptr)
    {
        const auto& a = previewNow->audio();
        const double rate = previewNow->rate() / sr;
        const int len = a.getNumSamples();
        for (int i = 0; i < numSamples && previewPos < len - 1; ++i)
        {
            const float pl = readHermite (a.getReadPointer (0), len, previewPos);
            const float pr = a.getNumChannels() > 1 ? readHermite (a.getReadPointer (1), len, previewPos) : pl;
            out.left[0][i] += pl * 0.8f;
            if (out.right[0] != nullptr)
                out.right[0][i] += pr * 0.8f;
            previewPos += rate;
        }
        if (previewPos >= len - 1)
            previewNow = nullptr;
    }
    previewPlaying = previewNow != nullptr;

    // ---- crossfade del ribbon: campioni <-> ingresso, sull'uscita principale
    if (crossfade > 0.001f && numInputChannels > 0 && input != nullptr && out.left[0] != nullptr)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = input[0][i], inR = numInputChannels > 1 ? input[1][i] : inL;
            out.left[0][i] = out.left[0][i] * (1.0f - crossfade) + inL * crossfade;
            if (out.right[0] != nullptr)
                out.right[0][i] = out.right[0][i] * (1.0f - crossfade) + inR * crossfade;
        }
    }

    // ---- volume generale
    float peak = 0.0f;
    for (int o = 0; o < kNumOutputs; ++o)
    {
        if (out.left[o] != nullptr)  juce::FloatVectorOperations::multiply (out.left[o], g.masterGain, numSamples);
        if (out.right[o] != nullptr) juce::FloatVectorOperations::multiply (out.right[o], g.masterGain, numSamples);
    }
    if (out.left[0] != nullptr)
    {
        const auto range = juce::FloatVectorOperations::findMinAndMax (out.left[0], numSamples);
        peak = juce::jmax (-range.getStart(), range.getEnd());
    }
    outPeak = juce::jmax (outPeak.load() * 0.9f, std::abs (peak));

    // ---- campionamento
    const auto rs = RecState (recStateAtomic.load());
    if (rs == RecState::Armed || rs == RecState::Recording)
    {
        const float* srcL = nullptr;
        const float* srcR = nullptr;
        if (recSource == RecSource::Input && numInputChannels > 0 && input != nullptr)
        {
            srcL = input[0];
            srcR = numInputChannels > 1 ? input[1] : input[0];
        }
        else if (recSource == RecSource::Output && out.left[0] != nullptr)
        {
            srcL = out.left[0];
            srcR = out.right[0] != nullptr ? out.right[0] : out.left[0];
        }

        int i = 0;
        if (rs == RecState::Armed && srcL != nullptr)
        {
            // Parte quando il segnale supera la soglia (come il trigger dell'SU10)
            for (; i < numSamples; ++i)
                if (recThreshold <= 0.0f || std::abs (srcL[i]) >= recThreshold || std::abs (srcR[i]) >= recThreshold)
                {
                    recStateAtomic = int (RecState::Recording);
                    break;
                }
        }
        if (RecState (recStateAtomic.load()) == RecState::Recording)
        {
            int p = recPos.load();
            const int cap = recBuffer.getNumSamples();
            const int n = srcL != nullptr ? juce::jmin (numSamples - i, cap - p) : 0;
            if (n > 0)
            {
                recBuffer.copyFrom (0, p, srcL + i, n);
                recBuffer.copyFrom (1, p, srcR + i, n);
                p += n;
                recPos = p;
            }
            if (p >= cap || recStopRequest.load())
                recStateAtomic = int (RecState::Done);
        }
        else if (recStopRequest.load())
        {
            recStateAtomic = int (RecState::Idle);   // fermato prima di partire
        }
    }

    // ---- stato per la UI
    int counts[kNumPads] {};
    for (auto& v : voices)
        if (v->active)
            ++counts[v->pad];
    for (int p = 0; p < kNumPads; ++p)
        padVoices[p] = counts[p];
}
} // namespace rsu
