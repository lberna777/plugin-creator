#include "Bridge.h"

#include <hidapi.h>
#if JUCE_MAC
 #include <hidapi_darwin.h>
#endif

using namespace mikro;

namespace
{
constexpr double kReopenMs = 1000.0, kLedMs = 10.0, kDisplayMs = 40.0;
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
        return settings.keyBase.load() + settings.root.load() + scales::degreeToSemitones (settings.scale.load(), physicalPad);
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
    splashStart = juce::Time::getMillisecondCounterHiRes();
    for (auto& p : pads)
        p.reset();
    mirror.status = int (Status::Connected);
    refreshOutputs (true);
    return true;
}

void Bridge::closeDevice()
{
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
            if (now - lastOpenAttempt >= kReopenMs)
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
            updateDisplay();
            refreshOutputs (false);
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
        const bool flipped = settings.flippedPads.load();
        for (int i = 0; i < kNumPads; ++i)
            handlePad (physicalPad (i, flipped), rawPressure[size_t (i)]);
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

            if (id == PadMode)
            {
                if (is)
                {
                    releaseAllNotes();
                    settings.mode = (settings.mode.load() + 1) % kNumModes;   // batteria > tastiera > accordi
                }
                continue;
            }

            if (const int cc = ccForButton (int (i), shift); cc >= 0)
                send (juce::MidiMessage::controllerEvent (ch, cc, is ? 127 : 0));
        }

        if (const int d = encoderDelta (buttons.encoder, now.encoder); d != 0)
            send (juce::MidiMessage::controllerEvent (ch, shift ? kShiftEncoderCc : kEncoderCc, juce::jlimit (1, 127, 64 + d)));
    }

    buttons = now;
    haveButtons = true;
}

void Bridge::handlePad (int pad, int pressure)
{
    mirror.pressure[pad] = pressure;

    PadSettings ps;
    ps.onThreshold = juce::jlimit (60, 2000, settings.threshold.load());
    ps.curve = VelocityCurve (juce::jlimit (0, 3, settings.curve.load()));
    ps.aftertouch = settings.aftertouch.load();

    const auto events = pads[pad].process (pressure, ps);

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
            send (juce::MidiMessage::noteOn (sentChannel[pad], note, e.value));
            if (chordMode())   // le altre note dell'accordo, stessa velocity
            {
                const auto chord = scales::chordForPad (settings.scale.load(), pad);
                for (int c = 1; c < chord.count; ++c)
                {
                    const int n = note + chord.notes[c] - chord.notes[0];
                    sentChord[pad][c] = (n >= 0 && n <= 127) ? n : -1;
                    if (sentChord[pad][c] >= 0)
                        send (juce::MidiMessage::noteOn (sentChannel[pad], n, e.value));
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
                send (juce::MidiMessage::noteOff (sentChannel[pad], sentNote[pad]));
            sentNote[pad] = -1;
            for (auto& n : sentChord[pad])
                if (const int old = std::exchange (n, -1); old >= 0)
                    send (juce::MidiMessage::noteOff (sentChannel[pad], old));
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

void Bridge::send (const juce::MidiMessage& m)
{
    if (midiOut != nullptr)
        midiOut->sendMessageNow (m);
}

// ---------------------------------------------------------------- LED e display

void Bridge::refreshOutputs (bool force)
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (force || now - lastLedWrite >= kLedMs)
    {
        const auto leds = computeLeds();
        if (force || leds != lastLeds)
        {
            const auto r = ledReport (leds);
            hid_write (dev, r.data(), r.size());
            lastLeds = leds;
        }
        lastLedWrite = now;
    }

    if (frameValid && (force || ! displayValid || frame != lastDisplay))
    {
        for (int c = 0; c < 4; ++c)
        {
            const auto r = displayReport (frame.data(), c);
            hid_write (dev, r.data(), r.size());
        }
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

    const double splash = now - splashStart;
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
