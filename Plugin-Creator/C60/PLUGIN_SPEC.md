# PLUGIN_SPEC — C-60 (registratore a cassetta per il master bus)

> Contratto del plugin. Identità e ID dei parametri si congelano prima del primo uso in un progetto.

## 1. Identità — PROPOSTA, da confermare
- Nome prodotto: **C-60**
- PLUGIN_CODE: **Cs60** · PLUGIN_MANUFACTURER_CODE: **Mypl** · COMPANY_NAME: **MyPlugins**
- BUNDLE_ID: **com.myplugins.c60**
- Versione: **0.1.0** (CMake `project(VERSION)` = unica fonte)

## 2. Cosa fa
Messo sul master, il mix "passa" su una cassetta: registrazione, nastro, trasporto, riproduzione.

Catena (nell'ordine fisico di un registratore):

| # | Stadio | Cosa emula | Dove |
|---|--------|-----------|------|
| 1 | Record Level | quanto "caldo" entra sul nastro | `processChunk` §1 |
| 2 | Pre-enfasi | EQ di registrazione IEC (120 µs ferro, 70 µs cromo/metallo), +10 dB sugli acuti | `FirstOrderShelf` |
| 3 | Isteresi ×4 | magnetizzazione del nastro, modello Jiles-Atherton (RK2), sovracampionato ×4 FIR | `Hysteresis` |
| 4 | Trasporto | wow (0,55 Hz + deriva casuale) e flutter (capstan 6,3 Hz, pinch roller 11,7 Hz + casuale), uguale su L e R | `WowFlutter` + `FracDelay` |
| 5 | Azimut | testina non perfettamente allineata: micro-ritardo tra L e R (cresce con Age) | `processChunk` §3 |
| 6 | Fruscio | rumore del nastro, scorrelato L/R, sagomato dalla catena di riproduzione | `hissRng` |
| 7 | Dropout | cali brevi di livello e di acuti (polvere/usura), frequenza ∝ Age³ | `Dropouts` |
| 8 | Basse | head bump (62 Hz a 4,76 cm/s, 124 Hz a 9,5) + perdita sotto il bump | `setLowEnd` |
| 9 | Acuti | perdite di testina: -3 dB a 12,5 / 15 / 16,5 kHz (I/II/IV), più alte a 9,5 cm/s, scendono con Age | `lp` |
| 10 | Bias | sotto-bias = più distorsione e più acuti; sovra-bias = più pulito e più scuro | `biasShelf` + isteresi |
| 11 | De-enfasi | esatto reciproco della 2 → a basso livello piatto, ad alto livello acuti compressi | `FirstOrderShelf` |
| 12 | Output / Mix | con Auto Gain l'uscita compensa il Record Level; dry allineato alla latenza | — |

## 2b. La piastra (decisa il 2026-10-14)
- **Nastro da 10 minuti agganciato alla timeline del DAW**: posizione del nastro = tempo del progetto.
- **REC** armato + DAW in play → registra. **AUTO PUNCH** limita la registrazione tra **IN** e **OUT**
  (fissati coi tasti PUNCH IN / PUNCH OUT alla posizione corrente).
- **Collage**: ogni registrazione è una passata che sostituisce il nastro solo dove passa;
  la linea del nastro mostra le passate a colori. **UNDO** annulla l'ultima passata (fino a 120 s).
- **Segni del montaggio realistici**: testina di cancellazione ~60 ms davanti a quella di registrazione
  (sovrapposizione dopo il punch-in, buco dopo il punch-out), cancellazione incompleta
  (-34 / -30 / -24 dB per I / II / IV), **FADE** = rampa ai punti di punch.
- **MONITOR**: SOURCE = il mix passa dal vivo nel nastro (effetto); TAPE = senti il nastro registrato.
- **PITCH** ±12 % in riproduzione (varispeed: il nastro scivola rispetto al DAW, come su una piastra vera).
- **NR** B / C / S / DBX: codifica in registrazione, decodifica in riproduzione. Se la piastra non è
  calibrata il decoder insegue male (respiro sugli acuti), come nella realtà.
- **HX Pro**: alleggerisce dinamicamente gli acuti forti in registrazione → più margine sugli acuti.
- **CAL** (stile Nakamichi ABLE / Revox): calibra livello, bias e azimut per il tipo di nastro corrente;
  una piastra nuova non è calibrata. **AZIMUTH** trimmer, **AUTO AZ** (stile NAAC del Dragon).
- **REC MUTE** registra silenzio · **MEMO** azzera il contagiri · **ERASE** cancella tutto · **EXPORT** WAV 24 bit.
- Il nastro si salva **dentro il progetto** (FLAC 24 bit, solo la parte usata).

## 3. Parametri (ID congelati)
| id | nome | tipo | range | default | smussato |
|----|------|------|-------|---------|----------|
| input    | Record Level | float | -18…+18 dB | 0 | sì |
| tape     | Tape     | choice | Type I / Type II / Type IV | Type II | – |
| speed    | Speed    | choice | 4.76 / 9.5 cm/s | 4.76 | – |
| bias     | Bias     | float | -100…+100 % | 0 | sì (coeff. ogni 32 campioni) |
| wow      | Wow      | float | 0…100 % | 20 | sì |
| flutter  | Flutter  | float | 0…100 % | 20 | sì |
| hiss     | Hiss     | float | 0…100 % | 30 | sì |
| age      | Age      | float | 0…100 % | 10 | sì (coeff. ogni 32 campioni) |
| output   | Output   | float | -18…+18 dB | 0 | sì |
| autogain | Auto Gain| bool  | on/off | on | sì (via output) |
| mix      | Mix      | float | 0…100 % | 100 | sì |
| bypass   | Bypass   | bool  | on/off | off | sì (dissolvenza) |
| monitor  | Monitor  | choice | Source / Tape | Source | sì (dissolvenza 20 ms) |
| rec      | Rec      | bool  | on/off | off | rampa = fade |
| autopunch| Auto Punch | bool | on/off | off | – |
| recmute  | Rec Mute | bool  | on/off | off | – |
| fade     | Fade     | float | 0…500 ms | 20 | – |
| pitch    | Pitch    | float | -12…+12 % | 0 | sì |
| nr       | NR       | choice | Off / B / C / S / DBX | Off | – |
| hxpro    | HX Pro   | bool  | on/off | on | – |
| azimuth  | Azimuth  | float | -30…+30 µs | 0 | per blocco |
| autoaz   | Auto Azimuth | bool | on/off | off | – |

## 4. I/O
- Mono→mono e stereo→stereo. Niente sidechain.
- Latenza dichiarata: sovracampionamento + 5 ms di trasporto (301 campioni a 48 kHz). Il DAW la compensa.

## 5. Misure (tests/measure.cpp, 48 kHz)
- **Null-test** (mix 0 e bypass): -240 dBFS ✔
- **Risposta** -40 dBFS, Type II 4,76: piatta ±0,2 dB 300 Hz–10 kHz, +2,1 dB a 62 Hz, -2,1 dB a 15 kHz.
- **THD 1 kHz** a -12 dBFS: Type I 1,2 % · II 0,8 % · IV 0,45 %. A 0 dBFS: 8,3 / 4,9 / 1,6 %.
- **Compressione acuti** (10 kHz a 0 dBFS): Type I -7,2 dB · II -3,2 dB.
- **Fruscio** default (30 %): -70 dBFS RMS Type I, -73 Type II.
- **Wow & flutter** default: 0,10 % RMS, 0,31 % picco (piastra discreta). Al massimo: 0,48 % RMS (walkman stanco).
- **Robustezza**: 44,1/48/96/192 kHz, mono/stereo, blocchi 1…4096, parametri estremi e cambi a caldo: nessun NaN.
- **pluginval --strictness-level 10** (VST3, Linux): SUCCESS.
- **CPU**: ~8 % di un core (stereo 48 kHz).

## 6. UI
- Provvisoria: tasti della piastra + contagiri + linea del collage + editor generico JUCE.
- Definitiva: grafica da Gemini (`ui/GEMINI_PROMPTS.md`), asset via `ui/assets.manifest.json`.
- Il processor espone già `outLevelL/R` e `getDeckState()` (lock-free) per VU, contagiri e collage.

## 7. Formati & piattaforme
- macOS: AU + VST3 + Standalone, universal (build e `auval` sul Mac mini).
- Linux: VST3 + Standalone (sviluppo e test).

## 8. Possibili v2
- Dolby B/C (encode in registrazione, decode in riproduzione, con l'errore di tracking tipico).
- Crosstalk tra tracce, rumore di modulazione, preset "piastra hi-fi / walkman / autoradio".
