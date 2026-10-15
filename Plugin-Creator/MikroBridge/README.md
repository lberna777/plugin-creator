# Mikro Bridge

La **Maschine Mikro MK2** senza il software Maschine: un'app che legge la Mikro via USB-HID
e la espone al DAW come porta MIDI virtuale **"Mikro MK2 Bridge"**. Pad con velocity e
aftertouch, tasti ed encoder come CC, display e LED pilotati dall'app (e dal DAW, in ritorno).

Istruzioni d'uso e installazione: [`dist/LEGGIMI.txt`](dist/LEGGIMI.txt).

## Perché un'app e non un plugin

Un plugin (AU/VST3) non può scrivere nella registrazione MIDI della traccia: Logic registra
solo ciò che arriva dagli ingressi MIDI. Facendo della Mikro un ingresso MIDI vero, si registra
come qualunque tastiera e suona qualunque strumento (Drum Machine Designer, Drum Rack, Battery…).

## Struttura

```
source/MikroProtocol.h   report USB: pulsanti 0x01, pad 0x20, LED 0x80, display 0xE0 (senza JUCE)
source/PadEngine.h       pressione 12 bit -> NoteOn/NoteOff/aftertouch (soglia, isteresi, picco)
source/Display.h         framebuffer 128x64 a pagine + font 5 px (da maschine-rs, MIT)
source/Bridge.*          thread USB (hidapi), porte MIDI virtuali, LED, display, SHIFT
source/MainComponent.*   finestra: specchio dei pad e impostazioni
tests/core_tests.cpp     test del protocollo con report sintetici
```

Protocollo ricavato da [maschine-rs](https://crates.io/crates/maschine) (MIT) e
[open-maschine](https://github.com/hansfbaier/open-maschine).

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target MikroBridge MikroCoreTests
./build/MikroCoreTests
```

macOS: universal arm64 + x86_64, costruita da `.github/workflows/mikro-macos.yml` a ogni push
(artefatto `Mikro-Bridge-macOS.zip`). Linux: funziona anche lì (ALSA + hidraw), serve una
regola udev per i permessi:
`SUBSYSTEM=="hidraw", ATTRS{idVendor}=="17cc", ATTRS{idProduct}=="1200", MODE="0666"`.

## Da verificare sull'hardware (non testabile senza la Mikro)

- ordine dei pad (c'è l'opzione "Pad numerati al contrario");
- soglia e fondo scala della velocity (`PadSettings::fullScale`);
- luminosità dei LED dei tasti (0xFF) e dei pad (0..127).
