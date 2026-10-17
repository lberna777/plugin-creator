// Ponte Mikro MK2 -> porta MIDI virtuale "Mikro MK2 Bridge".
// Un thread legge l'USB (hidapi), suona le note, accende LED e display; la porta MIDI in ingresso
// con lo stesso nome riceve dal DAW le note e i CC che accendono pad e pulsanti.
#pragma once

#include "DisplayRenderer.h"
#include "PadEngine.h"
#include "Arp.h"
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
        std::atomic<int> crosstalk { 35 };  // anti-fantasma: % della pressione del pad più premuto (0 = spento)
        std::atomic<bool> harmony { false };// modo Keys: solo le note della scala che stanno bene tutte insieme
        std::atomic<int> knobFx { 0 };      // effetto dell'encoder (mikro::kKnobFx), si cambia con GROUP
        std::atomic<bool> arp { false };    // arpeggiatore (tastiera e accordi), SHIFT + GROUP
        std::atomic<int> arpPattern { 0 }, arpRate { 3 }, arpOctaves { 1 }, arpGate { 60 };
        std::atomic<bool> arpLatch { false };
        std::atomic<int> arpBpm { 120 };    // se FL non manda il clock MIDI
        std::atomic<int> ledLevel { 50 };   // luminosità delle luci in %: meno corrente dall'USB
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
        std::atomic<int> knobValue { 0 };  // valore dell'effetto della manopola, 0..127
        std::atomic<double> knobTouchedMs { -1.0e9 };   // il display mostra l'effetto per un po'
        std::atomic<double> clockBpm { 0.0 };           // tempo dal clock MIDI di FL (0 = nessuno)
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

    // Ultimo frame del display (anche senza Mikro collegata): per lo specchio nella finestra
    void copyDisplay (mikro::Display& out) const;

private:
    void run() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    bool openDevice();
    void closeDevice();
    void handleReport (const uint8_t* data, int len);
    void handleButtons (const mikro::ButtonState& now);
    void handlePad (int pad, int pressure, int othersMax, double nowMs);
    void noteOn (int channel, int note, int velocity);   // passa dall'arpeggiatore se acceso
    void noteOff (int channel, int note);
    void turnKnob (int delta, bool fine);
    void resetKnob();
    void sendKnob (int fx);
    void tickArp (double nowMs);
    bool arpActive() const noexcept { return settings.arp.load() && usesKeyBase(); }
    void localPadFunction (int pad);
    void releaseAllNotes();
    void send (const juce::MidiMessage& m);
    void refreshOutputs (bool force);
    mikro::LedState computeLeds();
    mikro::DisplayState displayState() const noexcept;
    void updateDisplay();
    int octaveStep() const noexcept;

    std::unique_ptr<juce::MidiOutput> midiOut;
    std::unique_ptr<juce::MidiInput> midiIn;

    hid_device_* dev = nullptr;
    mikro::ButtonState buttons;
    bool haveButtons = false;
    mikro::PadEngine pads[mikro::kNumPads];
    int sentNote[mikro::kNumPads];        // nota mandata da ogni pad (per il NoteOff giusto)
    int sentChord[mikro::kNumPads][4];    // modo Accordi: le altre note dell'accordo (-1 = nessuna)
    mikro::Arpeggiator arp;
    // Quanti pad tengono ogni nota (per canale): due accordi con note in comune non si spengono a vicenda
    uint8_t noteRefs[16][128] {};
    int arpChannel = 1;
    int knobValues[mikro::kNumKnobFx] {};
    bool arpWasActive = false;
    std::atomic<bool> arpRestart { false };
    double lastClockMs = -1.0, clockInterval = 0.0;   // thread MIDI
    std::atomic<double> clockSeenMs { -1.0e9 };
    int sentChannel[mikro::kNumPads];
    bool shiftPadDown[mikro::kNumPads] {};
    std::array<int, mikro::kNumPads> rawPressure {};

    std::atomic<uint8_t> noteFeedback[128] {};   // dal DAW, canale pad
    std::atomic<bool> buttonFeedback[128] {};    // dal DAW, canale controlli, indice = CC

    mikro::LedState lastLeds {};
    mikro::DisplayRenderer renderer;
    mikro::DisplayState shownState;
    mikro::Display frame, lastDisplay, preview;   // frame = disegnato, lastDisplay = inviato alla Mikro
    mutable juce::SpinLock previewLock;
    double lastLedWrite = 0, lastDisplayRender = 0, lastOpenAttempt = -1.0e9;
    // Contro il loop di riavvii: dopo l'apertura la Mikro si sta ancora avviando e una scrittura
    // subito la manda in crash; se si scollega appena aperta, si aspetta sempre di più a riaprirla
    double openedAt = -1.0e9, reopenDelayMs = 1000.0;
    bool settled = false;
    bool write (const std::vector<uint8_t>& report);
    double splashStart = 0;                       // animazione della barca all'avvio e a ogni collegamento
    bool displayValid = false, frameValid = false, frameIsSplash = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Bridge)
};
