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
    std::fill (std::begin (sentChannel), std::end (sentChannel), 1);

   #if ! JUCE_WINDOWS
    midiOut = juce::MidiOutput::createNewDevice (kPortName);
    midiIn = juce::MidiInput::createNewDevice (kPortName, this);
    if (midiIn != nullptr)
        midiIn->start();
   #endif

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
    return settings.drumBase.load() + physicalPad;
}

int Bridge::octaveStep() const noexcept
{
    return settings.mode.load() == int (Mode::Keys) ? 12 : 16;
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
            wait (50);
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
            refreshOutputs (false);
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
                    settings.mode = settings.mode.load() == int (Mode::Drums) ? int (Mode::Keys) : int (Mode::Drums);
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
            mirror.velocity[pad] = 0;
        }
        else if (e.type == PadEvent::Pressure && sentNote[pad] >= 0)
        {
            send (juce::MidiMessage::aftertouchChange (sentChannel[pad], sentNote[pad], e.value));
        }
    }
}

void Bridge::localPadFunction (int pad)
{
    auto shiftBase = [this] (int delta)
    {
        auto& base = settings.mode.load() == int (Mode::Keys) ? settings.keyBase : settings.drumBase;
        base = juce::jlimit (0, 127 - (kNumPads - 1), base.load() + delta);
    };

    // In Keys con una scala, il semitono sposta la tonalità (C -> C# ...) e la scala resta giusta
    auto shiftRoot = [this] (int delta) { settings.root = scales::mod (settings.root.load() + delta, 12); };
    const bool scaled = settings.mode.load() == int (Mode::Keys) && settings.scale.load() != 0;

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

    if (force || now - lastDisplayWrite >= kDisplayMs)
    {
        Display d;
        drawDisplay (d);
        if (force || ! displayValid || d != lastDisplay)
        {
            for (int c = 0; c < 4; ++c)
            {
                const auto r = displayReport (d.data(), c);
                hid_write (dev, r.data(), r.size());
            }
            lastDisplay = d;
            displayValid = true;
        }
        lastDisplayWrite = now;
    }
}

LedState Bridge::computeLeds()
{
    LedState l {};
    const bool keys = settings.mode.load() == int (Mode::Keys);
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

void Bridge::drawDisplay (Display& d) const
{
    const bool keys = settings.mode.load() == int (Mode::Keys);
    const int ch = settings.padChannel.load();

    // Barra del titolo in negativo
    d.fillRect (0, 0, kDisplayW, 9, true);
    d.text (2, 2, "MIKRO BRIDGE", 1, false);
    const char* mode = keys ? "KEYS" : "DRUMS";
    d.text (kDisplayW - 2 - Display::textWidth (mode), 2, mode, 1, false);

    // Ultima nota, grande
    const int last = mirror.lastNote.load();
    juce::String big = noteName (last);
    d.text (2, 14, big.toRawUTF8(), 3);
    if (last >= 0)
    {
        const juce::String v ("VEL " + juce::String (mirror.lastVelocity.load()));
        d.text (kDisplayW - 2 - Display::textWidth (v.toRawUTF8()), 16, v.toRawUTF8());
    }
    const juce::String chs ("CH " + juce::String (ch));
    d.text (kDisplayW - 2 - Display::textWidth (chs.toRawUTF8()), 25, chs.toRawUTF8());

    // Estensione dei pad
    const juce::String range ("PAD 1-16  " + noteName (noteForPad (0)) + " - " + noteName (noteForPad (15)));
    d.text (2, 36, range.toRawUTF8());

    const char* curves[] { "SOFT", "LIN", "HARD", "FIXED" };
    const juce::String info = keys ? juce::String ("SCALA ") + scales::kRootNames[scales::mod (settings.root.load(), 12)] + " "
                                         + scales::get (settings.scale.load()).shortName
                                   : juce::String ("VEL ") + curves[juce::jlimit (0, 3, settings.curve.load())]
                                         + (settings.aftertouch.load() ? "   AT ON" : "   AT OFF");
    d.text (2, 45, info.toRawUTF8());

    d.fillRect (0, 54, kDisplayW, 1, true);
    d.text (2, 57, "SHIFT + 13-16: TRASPONI");
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
