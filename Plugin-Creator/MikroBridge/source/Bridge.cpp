#include "Bridge.h"

#include <hidapi.h>
#if JUCE_MAC
 #include <hidapi_darwin.h>
#endif

using namespace mikro;

namespace
{
constexpr double kReopenMs = 1000.0, kLedMs = 10.0, kDisplayMs = 40.0;
constexpr double kSettleMs = 1500.0;          // dopo l'apertura: solo lettura, la Mikro finisce di avviarsi
constexpr double kStableMs = 8000.0;          // aperta da almeno tanto = collegamento sano, pausa azzerata
constexpr double kMaxReopenMs = 10000.0;
constexpr int kReadTimeoutMs = 2;

constexpr int kShiftPadSemiDown = 12, kShiftPadSemiUp = 13, kShiftPadOctDown = 14, kShiftPadOctUp = 15;   // pad 13..16

void setRgb (LedState& l, int led, int r, int g, int b)
{
    l[size_t (led)] = uint8_t (juce::jlimit (0, 127, r));
    l[size_t (led + 1)] = uint8_t (juce::jlimit (0, 127, g));
    l[size_t (led + 2)] = uint8_t (juce::jlimit (0, 127, b));
}
} // namespace

Bridge::Bridge() : juce::Thread ("Mikro USB")
{
    std::fill (std::begin (sentNote), std::end (sentNote), -1);
    for (auto& c : sentChord)
        std::fill (std::begin (c), std::end (c), -1);
    for (int i = 0; i < kNumKnobFx; ++i)
        knobValues[i] = kKnobFx[i].initial;
    std::fill (std::begin (sentChannel), std::end (sentChannel), 1);

   #if ! JUCE_WINDOWS
    midiOut = juce::MidiOutput::createNewDevice (kPortName);
    midiIn = juce::MidiInput::createNewDevice (kPortName, this);
    if (midiIn != nullptr)
        midiIn->start();
   #endif

    splashStart = juce::Time::getMillisecondCounterHiRes();
    hid_init();
   #if JUCE_MAC
    // Non esclusivo: se l'agente NI tiene aperta la Mikro, la lettura funziona lo stesso
    hid_darwin_set_open_exclusive (0);
   #endif
    startThread (juce::Thread::Priority::highest);
}

Bridge::~Bridge()
{
    stopThread (2000);
    if (midiIn != nullptr)
        midiIn->stop();
    hid_exit();
}

int Bridge::ccForButton (int buttonIndex, bool shift) noexcept
{
    const auto id = kButtons[size_t (buttonIndex)].id;
    if (id == PadMode || id == Shift)
        return -1;
    return (shift ? kFirstShiftButtonCc : kFirstButtonCc) + buttonIndex;
}

int Bridge::noteForPad (int physicalPad) const noexcept
{
    if (settings.mode.load() == int (Mode::Keys))   // gradi consecutivi della scala: ogni pad è intonato
    {
        const int sc = settings.scale.load();
        const int semis = settings.harmony.load() ? scales::consonantDegreeToSemitones (sc, physicalPad)   // ARMONIA
                                                  : scales::degreeToSemitones (sc, physicalPad);
        return settings.keyBase.load() + settings.root.load() + semis;
    }
    if (chordMode())                                 // fondamentale dell'accordo del pad
        return settings.keyBase.load() + settings.root.load() + scales::chordForPad (settings.scale.load(), physicalPad).notes[0];
    return settings.drumBase.load() + physicalPad;
}

juce::String Bridge::chordLabel (int physicalPad) const
{
    const int sc = settings.scale.load(), root = settings.root.load();
    return juce::String (scales::chordName (sc, root, physicalPad)) + " "
         + juce::String (juce::CharPointer_UTF8 (scales::chordRoman (sc, physicalPad).c_str()));
}

int Bridge::octaveStep() const noexcept
{
    return usesKeyBase() ? 12 : 16;
}

juce::String Bridge::noteName (int note)
{
    if (note < 0 || note > 127)
        return "--";
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[note % 12]) + juce::String (note / 12 - 2);   // nota 60 = C3 (Logic, Ableton)
}

// ---------------------------------------------------------------- thread USB

bool Bridge::openDevice()
{
    dev = hid_open (kVendorId, kProductId, nullptr);
    if (dev == nullptr)
    {
        // Collegata ma non apribile = la tiene un altro programma (agente NI) o mancano i permessi
        auto* found = hid_enumerate (kVendorId, kProductId);
        mirror.status = int (found != nullptr ? Status::Busy : Status::Searching);
        hid_free_enumeration (found);
        return false;
    }
    haveButtons = false;
    displayValid = false;
    settled = false;
    openedAt = juce::Time::getMillisecondCounterHiRes();
    splashStart = openedAt + kSettleMs;   // l'animazione parte quando si può scrivere
    for (auto& p : pads)
        p.reset();
    mirror.status = int (Status::Connected);
    return true;   // niente scritture qui: le fa run() dopo kSettleMs
}

void Bridge::closeDevice()
{
    // scollegata poco dopo l'apertura = probabile riavvio della Mikro: si aspetta di più a riaprire
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (dev != nullptr && now - openedAt < kStableMs)
        reopenDelayMs = juce::jmin (kMaxReopenMs, reopenDelayMs * 2.0);
    lastOpenAttempt = now;
    releaseAllNotes();
    if (dev != nullptr)
        hid_close (dev);
    dev = nullptr;
    mirror.status = int (Status::Searching);
}

void Bridge::run()
{
    uint8_t buf[512];

    while (! threadShouldExit())
    {
        if (dev == nullptr)
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (now - lastOpenAttempt >= reopenDelayMs)
            {
                lastOpenAttempt = now;
                if (openDevice())
                    continue;
            }
            updateDisplay();   // lo specchio nella finestra vive anche senza Mikro
            wait (20);
            continue;
        }

        // Svuota i report in coda, poi aggiorna LED e display
        for (int n = 0; n < 64; ++n)
        {
            const int len = hid_read_timeout (dev, buf, sizeof (buf), n == 0 ? kReadTimeoutMs : 0);
            if (len < 0)
            {
                closeDevice();
                break;
            }
            if (len == 0)
                break;
            handleReport (buf, len);
        }

        if (dev != nullptr)
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            tickArp (now);
            updateDisplay();
            if (! settled && now - openedAt >= kSettleMs)
            {
                settled = true;
                refreshOutputs (true);   // prima scrittura: la Mikro ha finito di avviarsi
            }
            else if (settled)
                refreshOutputs (false);
            if (settled && now - openedAt >= kStableMs)
                reopenDelayMs = kReopenMs;
        }
    }

    if (dev != nullptr)
    {
        // Spegne tutto all'uscita, così la Mikro non resta con le luci accese
        LedState off {};
        auto r = ledReport (off);
        hid_write (dev, r.data(), r.size());
        Display blank;
        for (int c = 0; c < 4; ++c)
        {
            auto d = displayReport (blank.data(), c);
            hid_write (dev, d.data(), d.size());
        }
        closeDevice();
    }
}

void Bridge::handleReport (const uint8_t* data, int len)
{
    if (ButtonState now; parseButtons (data, size_t (len), now))
    {
        handleButtons (now);
        return;
    }

    if (parsePads (data, size_t (len), rawPressure))
    {
        // le due pressioni più alte: a ogni pad serve la più alta tra gli ALTRI (anti-diafonia)
        int top1 = 0, top2 = 0, topIndex = -1;
        for (int i = 0; i < kNumPads; ++i)
        {
            const int p = rawPressure[size_t (i)];
            if (p > top1) { top2 = top1; top1 = p; topIndex = i; }
            else if (p > top2) top2 = p;
        }
        const double now = juce::Time::getMillisecondCounterHiRes();
        const bool flipped = settings.flippedPads.load();
        for (int i = 0; i < kNumPads; ++i)
            handlePad (physicalPad (i, flipped), rawPressure[size_t (i)], i == topIndex ? top2 : top1, now);
    }
}

void Bridge::handleButtons (const ButtonState& now)
{
    const bool shift = now.pressed (Shift);
    const int ch = juce::jlimit (1, 16, settings.ctrlChannel.load());

    if (haveButtons)
    {
        for (size_t i = 0; i < kButtons.size(); ++i)
        {
            const auto id = kButtons[i].id;
            const bool was = buttons.pressed (id), is = now.pressed (id);
            if (was == is)
                continue;

            if (id == Group)   // GROUP: effetto della manopola; SHIFT + GROUP: arpeggiatore
            {
                if (is)
                {
                    if (shift)
                    {
                        releaseAllNotes();
                        settings.arp = ! settings.arp.load();
                    }
                    else
                        settings.knobFx = (settings.knobFx.load() + 1) % kNumKnobFx;
                    const int fx = juce::jlimit (0, kNumKnobFx - 1, settings.knobFx.load());
                    mirror.knobValue = kKnobFx[fx].cc == kKnobFxArpRate ? settings.arpRate.load() : knobValues[fx];
                    mirror.knobTouchedMs = juce::Time::getMillisecondCounterHiRes();
                }
                continue;
            }
            if (id == EncoderPush && settings.knobFx.load() != 0)   // manopola premuta: valore iniziale
            {
                if (is)
                    resetKnob();
                continue;
            }

            if (id == PadMode)
            {
                if (is)
                {
                    releaseAllNotes();
                    if (shift)   // SHIFT + PAD MODE: ARMONIA accesa/spenta (tastiera)
                        settings.harmony = ! settings.harmony.load();
                    else
                        settings.mode = (settings.mode.load() + 1) % kNumModes;   // batteria > tastiera > accordi
                }
                continue;
            }

            if (const int cc = ccForButton (int (i), shift); cc >= 0)
                send (juce::MidiMessage::controllerEvent (ch, cc, is ? 127 : 0));
        }

        if (const int d = encoderDelta (buttons.encoder, now.encoder); d != 0)
        {
            if (settings.knobFx.load() != 0)
                turnKnob (d, shift);   // SHIFT = regolazione fine
            else
                send (juce::MidiMessage::controllerEvent (ch, shift ? kShiftEncoderCc : kEncoderCc, juce::jlimit (1, 127, 64 + d)));
        }
    }

    buttons = now;
    haveButtons = true;
}

void Bridge::handlePad (int pad, int pressure, int othersMax, double nowMs)
{
    mirror.pressure[pad] = pressure;

    PadSettings ps;
    ps.onThreshold = juce::jlimit (60, 2000, settings.threshold.load());
    ps.curve = VelocityCurve (juce::jlimit (0, 3, settings.curve.load()));
    ps.aftertouch = settings.aftertouch.load();
    ps.crosstalk = float (juce::jlimit (0, 80, settings.crosstalk.load())) / 100.0f;

    const auto events = pads[pad].process (pressure, ps, nowMs, othersMax);

    for (int k = 0; k < events.count; ++k)
    {
        const auto& e = events.e[k];

        if (e.type == PadEvent::NoteOn)
        {
            // Con SHIFT i pad fanno le funzioni stampate sotto (semitono/ottava) o mandano un CC
            if (buttons.pressed (Shift))
            {
                shiftPadDown[pad] = true;
                localPadFunction (pad);
                continue;
            }
            const int note = noteForPad (pad);
            if (note < 0 || note > 127)
                continue;
            sentNote[pad] = note;
            sentChannel[pad] = juce::jlimit (1, 16, settings.padChannel.load());
            noteOn (sentChannel[pad], note, e.value);
            if (chordMode())   // le altre note dell'accordo, stessa velocity
            {
                const auto chord = scales::chordForPad (settings.scale.load(), pad);
                for (int c = 1; c < chord.count; ++c)
                {
                    const int n = note + chord.notes[c] - chord.notes[0];
                    sentChord[pad][c] = (n >= 0 && n <= 127) ? n : -1;
                    if (sentChord[pad][c] >= 0)
                        noteOn (sentChannel[pad], n, e.value);
                }
            }
            mirror.lastPad = pad;
            mirror.velocity[pad] = e.value;
            mirror.lastNote = note;
            mirror.lastVelocity = e.value;
        }
        else if (e.type == PadEvent::NoteOff)
        {
            if (shiftPadDown[pad])
            {
                shiftPadDown[pad] = false;
                if (pad < kShiftPadSemiDown)
                    send (juce::MidiMessage::controllerEvent (settings.ctrlChannel.load(), kFirstShiftPadCc + pad, 0));
                continue;
            }
            if (sentNote[pad] >= 0)
                noteOff (sentChannel[pad], sentNote[pad]);
            sentNote[pad] = -1;
            for (auto& n : sentChord[pad])
                if (const int old = std::exchange (n, -1); old >= 0)
                    noteOff (sentChannel[pad], old);
            mirror.velocity[pad] = 0;
        }
        else if (e.type == PadEvent::Pressure && sentNote[pad] >= 0)
        {
            send (juce::MidiMessage::aftertouchChange (sentChannel[pad], sentNote[pad], e.value));
            for (const int n : sentChord[pad])
                if (n >= 0)
                    send (juce::MidiMessage::aftertouchChange (sentChannel[pad], n, e.value));
        }
    }
}

void Bridge::localPadFunction (int pad)
{
    auto shiftBase = [this] (int delta)
    {
        auto& base = usesKeyBase() ? settings.keyBase : settings.drumBase;
        base = juce::jlimit (0, 127 - (kNumPads - 1), base.load() + delta);
    };

    // In Keys con una scala, il semitono sposta la tonalità (C -> C# ...) e la scala resta giusta
    auto shiftRoot = [this] (int delta) { settings.root = scales::mod (settings.root.load() + delta, 12); };
    const bool scaled = (settings.mode.load() == int (Mode::Keys) && settings.scale.load() != 0) || chordMode();

    switch (pad)
    {
        case kShiftPadSemiDown: if (scaled) shiftRoot (-1); else shiftBase (-1); break;
        case kShiftPadSemiUp:   if (scaled) shiftRoot (+1); else shiftBase (+1); break;
        case kShiftPadOctDown:  shiftBase (-octaveStep()); break;
        case kShiftPadOctUp:    shiftBase (+octaveStep()); break;
        default:   // SHIFT + pad 1..12 (UNDO, REDO, QUANTIZE...): CC da assegnare nel DAW
            send (juce::MidiMessage::controllerEvent (settings.ctrlChannel.load(), kFirstShiftPadCc + pad, 127));
            break;
    }
}

void Bridge::releaseAllNotes()
{
    if (const int n = arp.soundingNote(); n >= 0)
        send (juce::MidiMessage::noteOff (arpChannel, n));
    arp.clear();
    for (auto& ch : noteRefs)   // releaseAllNotes manda i noteOff diretti: i conteggi ripartono da zero
        std::fill (std::begin (ch), std::end (ch), uint8_t (0));
    for (int p = 0; p < kNumPads; ++p)
    {
        if (sentNote[p] >= 0)
            send (juce::MidiMessage::noteOff (sentChannel[p], sentNote[p]));
        sentNote[p] = -1;
        for (auto& n : sentChord[p])
        {
            if (n >= 0)
                send (juce::MidiMessage::noteOff (sentChannel[p], n));
            n = -1;
        }
        mirror.velocity[p] = 0;
        mirror.pressure[p] = 0;
        pads[p].reset();
    }
}

// ---------------------------------------------------------------- note, arpeggiatore, manopola

void Bridge::noteOn (int channel, int note, int velocity)
{
    if (arpActive())
    {
        arpChannel = channel;
        arp.press (note, velocity);
        return;
    }
    auto& refs = noteRefs[juce::jlimit (1, 16, channel) - 1][note & 127];
    if (refs > 0)   // già suonata da un altro pad: si ripete il colpo senza staccare la nota
        send (juce::MidiMessage::noteOff (channel, note));
    refs = uint8_t (juce::jmin (255, refs + 1));
    send (juce::MidiMessage::noteOn (channel, note, (juce::uint8) velocity));
}

void Bridge::noteOff (int channel, int note)
{
    if (arpActive())
    {
        arp.release (note, settings.arpLatch.load());
        return;
    }
    auto& refs = noteRefs[juce::jlimit (1, 16, channel) - 1][note & 127];
    if (refs > 1)   // un altro pad la tiene ancora: resta accesa
    {
        --refs;
        return;
    }
    refs = 0;
    send (juce::MidiMessage::noteOff (channel, note));
}

void Bridge::tickArp (double nowMs)
{
    if (! arpActive())
    {
        if (arpWasActive)
        {
            if (const int n = arp.soundingNote(); n >= 0)
                send (juce::MidiMessage::noteOff (arpChannel, n));
            arp.clear();
        }
        arpWasActive = false;
        return;
    }
    arpWasActive = true;
    if (arpRestart.exchange (false))
        arp.restart();

    ArpSettings s;
    s.pattern = ArpPattern (juce::jlimit (0, kNumArpPatterns - 1, settings.arpPattern.load()));
    s.rate = juce::jlimit (0, kNumArpRates - 1, settings.arpRate.load());
    s.octaves = juce::jlimit (1, 3, settings.arpOctaves.load());
    s.gate = float (juce::jlimit (5, 98, settings.arpGate.load())) / 100.0f;
    s.latch = settings.arpLatch.load();
    const bool clocked = nowMs - clockSeenMs.load() < 500.0 && mirror.clockBpm.load() > 0.0;
    s.bpm = clocked ? mirror.clockBpm.load() : double (juce::jlimit (20, 400, settings.arpBpm.load()));

    std::array<ArpEvent, 2> ev;
    const int n = arp.tick (nowMs, s, ev);
    for (int i = 0; i < n; ++i)
    {
        if (ev[size_t (i)].type == ArpEvent::NoteOn)
        {
            send (juce::MidiMessage::noteOn (arpChannel, ev[size_t (i)].note, ev[size_t (i)].velocity));
            mirror.lastNote = ev[size_t (i)].note;
        }
        else
            send (juce::MidiMessage::noteOff (arpChannel, ev[size_t (i)].note));
    }
}

void Bridge::turnKnob (int delta, bool fine)
{
    const int fx = juce::jlimit (0, kNumKnobFx - 1, settings.knobFx.load());
    if (kKnobFx[fx].cc == kKnobFxArpRate)   // velocità dell'arpeggiatore: uno scatto = una suddivisione
        settings.arpRate = juce::jlimit (0, kNumArpRates - 1, settings.arpRate.load() + (delta > 0 ? 1 : -1));
    else
        knobValues[fx] = juce::jlimit (0, 127, knobValues[fx] + delta * (fine ? 1 : 3));
    sendKnob (fx);
}

void Bridge::resetKnob()
{
    const int fx = juce::jlimit (0, kNumKnobFx - 1, settings.knobFx.load());
    if (kKnobFx[fx].cc == kKnobFxArpRate)
        settings.arpRate = kKnobFx[fx].initial;
    else
        knobValues[fx] = kKnobFx[fx].initial;
    sendKnob (fx);
}

// Manda allo strumento (canale dei pad) il valore dell'effetto e lo mostra sul display
void Bridge::sendKnob (int fx)
{
    const auto& k = kKnobFx[fx];
    const int ch = juce::jlimit (1, 16, settings.padChannel.load());
    int shown = knobValues[fx];
    if (k.cc == kKnobFxArpRate)
        shown = settings.arpRate.load();
    else if (k.cc == kKnobFxPitchBend)
        send (juce::MidiMessage::pitchWheel (ch, shown <= 64 ? shown * 8192 / 64 : 8192 + (shown - 64) * 8191 / 63));
    else if (k.cc >= 0)
        send (juce::MidiMessage::controllerEvent (ch, k.cc, shown));
    mirror.knobValue = shown;
    mirror.knobTouchedMs = juce::Time::getMillisecondCounterHiRes();
}

void Bridge::send (const juce::MidiMessage& m)
{
    if (midiOut != nullptr)
        midiOut->sendMessageNow (m);
}

// ---------------------------------------------------------------- LED e display

// Una scrittura che fallisce = la Mikro non risponde (si sta riavviando): si chiude e si riprova
// più tardi, invece di insistere e tenerla in crash
bool Bridge::write (const std::vector<uint8_t>& report)
{
    if (dev == nullptr)
        return false;
    if (hid_write (dev, report.data(), report.size()) < 0)
    {
        closeDevice();
        return false;
    }
    return true;
}

void Bridge::refreshOutputs (bool force)
{
    if (dev == nullptr)
        return;
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (force || now - lastLedWrite >= kLedMs)
    {
        const auto leds = computeLeds();
        if (force || leds != lastLeds)
        {
            // Luminosità: le luci accese consumano corrente; su un hub carico la Mikro si spegne e riparte
            auto dimmed = leds;
            const int level = juce::jlimit (5, 100, settings.ledLevel.load());
            for (auto& v : dimmed)
                v = uint8_t ((int (v) * level + 50) / 100);
            if (! write (ledReport (dimmed)))
                return;
            lastLeds = leds;
        }
        lastLedWrite = now;
    }

    if (frameValid && (force || ! displayValid || frame != lastDisplay))
    {
        for (int c = 0; c < 4; ++c)
            if (! write (displayReport (frame.data(), c)))
                return;
        lastDisplay = frame;
        displayValid = true;
    }
}

// Ridisegna solo se lo stato è cambiato (o durante l'animazione d'avvio), al massimo ogni kDisplayMs
void Bridge::updateDisplay()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (frameValid && now - lastDisplayRender < kDisplayMs)
        return;
    lastDisplayRender = now;

    const double splash = juce::jmax (0.0, now - splashStart);   // prima di kSettleMs resta al primo fotogramma
    if (splash < DisplayRenderer::kSplashMs)
    {
        renderer.renderSplash (splash, frame);
        frameIsSplash = true;
    }
    else if (const auto st = displayState(); ! frameValid || frameIsSplash || st != shownState)
    {
        renderer.render (st, frame);
        shownState = st;
        frameIsSplash = false;
    }
    else
        return;

    frameValid = true;
    const juce::SpinLock::ScopedLockType sl (previewLock);
    preview = frame;
}

void Bridge::copyDisplay (Display& out) const
{
    const juce::SpinLock::ScopedLockType sl (previewLock);
    out = preview;
}

LedState Bridge::computeLeds()
{
    LedState l {};
    const bool keys = usesKeyBase();   // tastiera o accordi: PAD MODE acceso
    const bool shift = buttons.pressed (Shift);

    for (size_t i = 0; i < kButtons.size(); ++i)
    {
        const auto& b = kButtons[i];
        if (b.led < 0 || b.led == kLedGroup)
            continue;
        const int cc = ccForButton (int (i), false);
        const bool lit = buttons.pressed (b.id) || (cc >= 0 && buttonFeedback[cc].load())
                         || (b.id == PadMode && keys);
        l[size_t (b.led)] = lit ? 0xFF : 0x00;
    }

    if (buttons.pressed (Group))
        setRgb (l, kLedGroup, 127, 127, 127);
    else if (chordMode())
        setRgb (l, kLedGroup, 20, 127, 40);
    else if (keys)
        setRgb (l, kLedGroup, 0, 40, 127);
    else
        setRgb (l, kLedGroup, 127, 40, 0);

    for (int p = 0; p < kNumPads; ++p)
    {
        const int led = kPadLed[size_t (p)];

        if (shift)
        {
            // Con SHIFT si vedono le funzioni: 13-16 trasposizione, gli altri CC
            if (p >= kShiftPadSemiDown)
                setRgb (l, led, 0, 90, 30);
            else
                setRgb (l, led, 20, 20, 20);
            continue;
        }

        const int note = noteForPad (p);
        const int vel = mirror.velocity[p].load();
        const int fb = (note >= 0 && note < 128) ? noteFeedback[note].load() : 0;
        mirror.feedback[p] = fb;

        if (vel > 0)
        {
            const int k = 30 + vel * 97 / 127;
            setRgb (l, led, k, k, k);
        }
        else if (fb > 0)
        {
            const int k = 20 + fb * 107 / 127;
            setRgb (l, led, keys ? 0 : k, k, keys ? k : 0);
        }
        else if (keys)
        {
            if (note % 12 == 0)
                setRgb (l, led, 0, 14, 40);              // le C
            else if (juce::MidiMessage::isMidiNoteBlack (note))
                setRgb (l, led, 1, 2, 5);
            else
                setRgb (l, led, 6, 8, 12);
        }
        else
        {
            setRgb (l, led, 30, 9, 0);
        }
    }
    return l;
}

DisplayState Bridge::displayState() const noexcept
{
    DisplayState s;
    s.mode = settings.mode.load();
    s.lastNote = mirror.lastNote.load();
    s.lastVelocity = mirror.lastVelocity.load();
    s.lastPad = mirror.lastPad.load();
    s.root = settings.root.load();
    s.scale = settings.scale.load();
    s.channel = settings.padChannel.load();
    s.curve = settings.curve.load();
    s.aftertouch = settings.aftertouch.load();
    s.harmony = settings.harmony.load() && settings.mode.load() == int (Mode::Keys);
    s.arp = arpActive();
    s.knobFx = settings.knobFx.load();
    s.knobValue = mirror.knobValue.load();
    s.knobShown = juce::Time::getMillisecondCounterHiRes() - mirror.knobTouchedMs.load() < 1500.0;
    s.lowNote = noteForPad (0);
    s.highNote = noteForPad (kNumPads - 1);
    for (int p = 0; p < kNumPads; ++p)
        if (mirror.velocity[p].load() > 0)
            s.held = uint16_t (s.held | (1u << p));
    return s;
}

// ---------------------------------------------------------------- feedback dal DAW

void Bridge::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    // Clock MIDI di FL (24 impulsi a nota da 1/4): l'arpeggiatore va a tempo col progetto
    if (m.isMidiClock())
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (lastClockMs > 0.0 && now - lastClockMs < 250.0)
        {
            const double dt = now - lastClockMs;
            clockInterval = clockInterval > 0.0 ? clockInterval * 0.9 + dt * 0.1 : dt;
            mirror.clockBpm = 60000.0 / (clockInterval * 24.0);
        }
        lastClockMs = now;
        clockSeenMs = now;
        return;
    }
    if (m.isMidiStart())
    {
        arpRestart = true;
        return;
    }

    if (m.getChannel() == settings.padChannel.load())
    {
        if (m.isNoteOn())
            noteFeedback[m.getNoteNumber()] = uint8_t (m.getVelocity());
        else if (m.isNoteOff())
            noteFeedback[m.getNoteNumber()] = 0;
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            for (auto& n : noteFeedback)
                n = 0;
    }

    if (m.getChannel() == settings.ctrlChannel.load() && m.isController())
        buttonFeedback[m.getControllerNumber()] = m.getControllerValue() > 0;
}
