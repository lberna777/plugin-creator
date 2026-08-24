# PLUGIN_SPEC — VOCAL FORGE

> Compilato al giorno 1. Identità e id dei parametri sono **congelati**.

## 1. Identità (congelata)
- Nome prodotto (mostrato nel DAW): **VOCAL FORGE**
- PLUGIN_CODE (4 caratteri): **Vfrg**
- PLUGIN_MANUFACTURER_CODE (4 caratteri): **Mypl**
- COMPANY_NAME: **MyPlugins**
- BUNDLE_ID: **com.myplugins.vocalforge**
- Versione iniziale: **1.0.0**

## 2. Cosa fa
- Categoria: **Channel strip vocale / vocal chain generativa** (AU `aufx`, sottotipo dynamics/EQ)
- Descrizione in una riga: **Scrivi che voce vuoi; il plugin compila la vocal chain, tarata su stanza non trattata + Focusrite/Scarlett, e ti dice perché.**
- Catena di processing (ordine fisso, moduli attivabili):
  `in → trim/polarity → gate (key-filtered) → HPF → room-tamer (3 notch dinamici) → EQ sottrattiva (3 bande)
   → de-esser 1 → comp 1 (veloce) → comp 2 (lento) → saturazione → EQ tonale (3 bande) → de-esser 2 → limiter → out`
- Il **prompt testuale** non è un effetto: è un generatore di preset. Il DSP resta lo stesso, cambiano i valori.
- Riverbero e delay **non sono nella catena**: sono due **bus paralleli** (send), filtrati e duckati,
  attivabili dentro il plugin o esportabili come bus aux di Logic. Vedi `knowledge/CHAIN_ARCHITECTURE.md`.

## 3. Parametri (APVTS, id congelati)
### Globali
| id | nome UI | tipo | range | skew | default | unità | smussato |
|----|---------|------|-------|------|---------|-------|----------|
| inTrim | Input Trim | float | −24…+24 | – | 0 | dB | sì |
| polarity | Polarity | bool | on/off | – | off | – | – |
| outGain | Output | float | −24…+12 | – | 0 | dB | sì |
| mix | Mix | float | 0…100 | – | 100 | % | sì |
| bypass | Bypass | bool | on/off | – | off | – | – |
| intensity | Intensity | float | 0…100 | – | 60 | % | sì (scala globale dell'aggressività delle regole) |

### Moduli (ogni modulo ha `<mod>On` bool; i continui sono tutti smussati)
| modulo | id parametri | range |
|---|---|---|
| gate | `gateOn`, `gateThresh` (−80…0 dB), `gateRange` (0…40 dB), `gateAtk` (0.1…20 ms, log), `gateRel` (10…1000 ms, log), `gateKeyLo` (60…400 Hz, log), `gateKeyHi` (1…12 kHz, log) |
| hpf | `hpfOn`, `hpfFreq` (40…200 Hz, log), `hpfSlope` (12/24 dB/oct) |
| room | `roomOn`, `room1Freq/Q/Depth`, `room2Freq/Q/Depth`, `room3Freq/Q/Depth` (80…800 Hz log, Q 1…12, Depth 0…−12 dB), `roomThresh` (−60…0 dB) |
| eqSub | `eqSubOn`, `sub1Freq/Q/Gain` … `sub3Freq/Q/Gain` (60…8000 Hz log, Q 0.4…8, Gain −12…0 dB) |
| ds1 | `ds1On`, `ds1Freq` (3…12 kHz, log), `ds1Thresh` (−40…0 dB), `ds1Range` (0…12 dB), `ds1Mode` (wide/split) |
| comp1 | `c1On`, `c1Thresh` (−40…0 dB), `c1Ratio` (1…10), `c1Atk` (0.1…30 ms, log), `c1Rel` (10…300 ms, log), `c1Knee` (0…12 dB), `c1Makeup` (0…18 dB) |
| comp2 | `c2On`, `c2Thresh`, `c2Ratio` (1…6), `c2Atk` (5…100 ms, log), `c2Rel` (50…1000 ms, log), `c2Knee`, `c2Makeup` |
| sat | `satOn`, `satDrive` (0…100 %), `satType` (tape/tube/transistor), `satTilt` (−6…+6 dB) |
| eqTone | `eqToneOn`, `tone1Freq/Q/Gain` … `tone3Freq/Q/Gain` (80…16000 Hz log, Q 0.4…6, Gain −9…+9 dB), `airOn`, `airGain` (0…6 dB) |
| ds2 | `ds2On`, `ds2Freq`, `ds2Thresh`, `ds2Range` |
| limiter | `limOn`, `limCeiling` (−6…0 dBFS), `limRelease` (10…500 ms, log) |
| send reverb | `revOn`, `revVariant` (none/ambience/room/hall/plate), `revDecay` (0.2…4 s, log), `revPredelay` (0…80 ms), `revSize` (10…100 %), `revHpf` (100…900 Hz, log), `revLpf` (2…12 kHz, log), `revWidth` (0…100 %), `revDuck` (0…9 dB), `revSend` (−40…−6 dB) |
| send delay | `dlyOn`, `dlyVariant` (slap/eighth/quarter), `dlySync` (bool), `dlyTime` (60…1500 ms, log), `dlyDivision` (choice), `dlyFeedback` (0…45 %), `dlyHpf` (100…900 Hz, log), `dlyLpf` (1.5…8 kHz, log), `dlyDuck` (0…9 dB), `dlySend` (−40…−8 dB) |
| send mode | `sendsMode` (choice: internal / logic) — `internal` fa girare i bus nel plugin, `logic` li spegne e li esporta |

### Prompt (non automatizzabili)
| id | tipo | note |
|---|---|---|
| `promptText` | stringa (stato, non parametro automatizzabile) | il testo dell'utente, salvato nello stato |
| `sourceProfile` | choice | `untreated_room_focusrite_scarlett` (default) / `neutral` / `treated_room` |

> Regola: il prompt **scrive** i parametri sopra, poi l'utente li può ritoccare a mano.
> Ritoccare a mano non "rompe" il preset: mostra solo un badge "modificato".

## 4. I/O
- Bus: **mono→stereo** e **stereo→stereo** (la voce resta centrale; nessun width nella v1).
- Sidechain esterno: **no** (il gate usa un key-filter interno).
- Latenza: **solo il lookahead del limiter** (≤ 2 ms), dichiarata con `setLatencySamples`.

## 5. UI / Skin
- Stile: **fotorealistico (asset)**, coerente con lo scaffold padre.
- Elementi:
  - piastra/sfondo: **baked** (inerte)
  - knob, switch, cap fader: **asset PNG-alpha** (indicatore a ore 12)
  - **prompt box** (testo), pannello "**perché**" (le motivazioni), meter GR dei 5 stadi dinamici,
    curva EQ, valori numerici, badge "modificato": **codice**, mai baked
- Dimensione finestra (aspect): `__________` ← da fissare con la foto di riferimento
- Foto di riferimento: da mettere in `reference/`

## 6. Asset attesi (PNG alpha, @2x, in `source/assets/src/`)
- [ ] piastra di sfondo (inerte)
- [ ] knob grande / knob piccolo
- [ ] switch on/off (due stati)
- [ ] cornice del prompt box (senza testo dentro)
- [ ] LED / indicatori di modulo attivo (due stati)

## 7. Formati & piattaforme
- Formati: **AU + VST3** (AU è il path obbligatorio: la destinazione è Logic)
- Piattaforme: **macOS** universal (arm64 + x86_64)

## 8. Validazione richiesta
`CHECKLIST.md` di questo sottoprogetto (che include integralmente quella di `Plugin-Creator`).
