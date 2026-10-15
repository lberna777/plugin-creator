// Ponte Mikro MK2 -> porta MIDI virtuale "Mikro MK2 Bridge".
// Un thread legge l'USB (hidapi), suona le note, accende LED e display; la porta MIDI in ingresso
// con lo stesso nome riceve dal DAW le note e i CC che accendono pad e pulsanti.
#pragma once

#include "Display.h"
#include "PadEngine.h"
#include "Scales.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>

struct hid_device_;

class Bridge final : private juce::Thread, private juce::MidiInputCallback
{
public:
    static constexpr const char* kPortName = "Mikro MK2 Bridge";

    enum class Mode { Drums = 0, Keys = 1, Chords = 2 };   // PAD MODE li gira in quest'ordine
    static constexpr int kNumModes = 3;
    enum class Status { Searching, Connected, Busy };

    // Impostazioni: scritte dalla UI, lette dal thread USB
    struct Settings
    {
        std::atomic<int> mode { int (Mode::Drums) };
        std::atomic<int> drumBase { 36 };   // C1: pad 1 = C1 ... pad 16 = D#2 (Drum Machine Designer, Battery, GM)
        std::atomic<int> keyBase { 48 };    // C2
        std::atomic<int> scale { 0 };       // modo Keys: indice in scales::kScales (0 = cromatica)
        std::atomic<int> root { 0 };        // tonica 0..11 (C..B): i pad suonano solo note della scala
        std::atomic<int> padChannel { 1 };
        std::atomic<int> ctrlChannel { 16 };
        std::atomic<int> curve { int (mikro::VelocityCurve::Linear) };
        std::atomic<int> threshold { 250 };
        std::atomic<bool> aftertouch { false };
        std::atomic<bool> flippedPads { false };
    };

    // Specchio per la UI (pad fisici 0..15)
    struct Mirror
    {
        std::atomic<int> pressure[mikro::kNumPads] {};
        std::atomic<int> velocity[mikro::kNumPads] {};   // >0 mentre il pad suona
        std::atomic<int> feedback[mikro::kNumPads] {};   // velocity ricevuta dal DAW
        std::atomic<int> lastNote { -1 }, lastVelocity { 0 };
        std::atomic<int> lastPad { -1 };   // per mostrare l'ultimo accordo
        std::atomic<int> status { int (Status::Searching) };
    };

    Bridge();
    ~Bridge() override;

    Settings settings;
    Mirror mirror;

    bool hasVirtualPorts() const noexcept { return midiOut != nullptr; }
    int noteForPad (int physicalPad) const noexcept;               // in Accordi: la fondamentale
    bool usesKeyBase() const noexcept { return settings.mode.load() != int (Mode::Drums); }
    bool chordMode() const noexcept   { return settings.mode.load() == int (Mode::Chords); }
    juce::String chordLabel (int physicalPad) const;               // "Am" + numero romano
    static juce::String noteName (int note);

    // Tasti che il ponte usa da sé (non mandati al DAW): elenco per la legenda
    static constexpr int kFirstButtonCc = 20, kFirstShiftButtonCc = 70, kFirstShiftPadCc = 102;
    static constexpr int kEncoderCc = 14, kShiftEncoderCc = 15;
    static int ccForButton (int buttonIndex, bool shift) noexcept;   // -1 = tasto locale

private:
    void run() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    bool openDevice();
    void closeDevice();
    void handleReport (const uint8_t* data, int len);
    void handleButtons (const mikro::ButtonState& now);
    void handlePad (int pad, int pressure);
    void localPadFunction (int pad);
    void releaseAllNotes();
    void send (const juce::MidiMessage& m);
    void refreshOutputs (bool force);
    mikro::LedState computeLeds();
    void drawDisplay (mikro::Display& d) const;
    int octaveStep() const noexcept;

    std::unique_ptr<juce::MidiOutput> midiOut;
    std::unique_ptr<juce::MidiInput> midiIn;

    hid_device_* dev = nullptr;
    mikro::ButtonState buttons;
    bool haveButtons = false;
    mikro::PadEngine pads[mikro::kNumPads];
    int sentNote[mikro::kNumPads];        // nota mandata da ogni pad (per il NoteOff giusto)
    int sentChord[mikro::kNumPads][4];    // modo Accordi: le altre note dell'accordo (-1 = nessuna)
    int sentChannel[mikro::kNumPads];
    bool shiftPadDown[mikro::kNumPads] {};
    std::array<int, mikro::kNumPads> rawPressure {};

    std::atomic<uint8_t> noteFeedback[128] {};   // dal DAW, canale pad
    std::atomic<bool> buttonFeedback[128] {};    // dal DAW, canale controlli, indice = CC

    mikro::LedState lastLeds {};
    mikro::Display lastDisplay;
    double lastLedWrite = 0, lastDisplayWrite = 0, lastOpenAttempt = -1.0e9;
    bool displayValid = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Bridge)
};
