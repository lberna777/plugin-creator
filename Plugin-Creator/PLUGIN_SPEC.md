# PLUGIN_SPEC — scheda del plugin da sviluppare

> Compilare PRIMA di scrivere codice. I campi vuoti vengono riempiti dall'assistente facendo domande.
> Una volta completa, questa scheda è il contratto: identità e parametri vengono congelati.

## 1. Identità (congelata al giorno 1)
- Nome prodotto (mostrato nel DAW): **SEA TAPE**
- PLUGIN_CODE (4 caratteri, univoco): **Seat**
- PLUGIN_MANUFACTURER_CODE (4 caratteri): **Mypl**
- COMPANY_NAME: **MyPlugins**
- BUNDLE_ID: **com.myplugins.seatape**
- Versione iniziale: **1.0.0**

## 2. Cosa fa
- Categoria/e: **Modulazione — Vintage Chorus / Tape Flanger** (delay modulato + carattere nastro)
- Descrizione in una riga: **Chorus + Flanger vintage a nastro, con switch di modo, feedback, saturazione tape e width stereo.**
- Catena di processing: `in → tape sat → delay line modulato (LFO + feedback con HF damping/tone) → mix dry/wet → stereo width → out`
- Modo (switch): **Chorus** (delay 5–30 ms, feedback basso, molto stereo) / **Flanger** (delay 0.1–10 ms, feedback marcato, jet-sound)

## 3. Parametri
| id | nome UI | tipo | range | skew | default | unità | smussato |
|----|---------|------|-------|------|---------|-------|----------|
| mode     | Mode      | choice | Chorus / Flanger | – | Chorus | – | – |
| rate     | Rate      | float | 0.02–10 | log | 0.50 | Hz | LFO |
| sync     | Sync      | bool  | on/off | – | off | – | – |
| syncDiv  | Division  | choice | 1/1…1/32 (+dotted/triplet) | – | 1/4 | – | – |
| depth    | Depth     | float | 0–100 | – | 35 | % | sì |
| delay    | Delay     | float | 0.1–30 | log | 6.0 | ms | sì |
| feedback | Feedback  | float | −95–95 | – | 0 | % | sì |
| tone     | Tone      | float | 200–18000 | log | 12000 | Hz | sì (LP su wet/feedback) |
| tape     | Tape      | float | 0–100 | – | 20 | % | sì (saturazione) |
| width    | Width     | float | 0–100 | – | 60 | % | sì (LFO sfasato L/R) |
| mix      | Mix       | float | 0–100 | – | 50 | % | sì |

> Nota: **Wow & Flutter** non è un parametro esposto (non selezionato); un filo di flutter fisso può essere parte del carattere "tape" se vorrai.

## 4. I/O
- Bus: **stereo in/out** (supporto anche mono in/out; con mono il Width è inerte)
- Sidechain esterno: **no**
- Latenza/lookahead: **no** (delay modulato, zero latenza dichiarata)

## 5. UI / Skin
- Stile: **fotorealistico (asset)**
- Foto di riferimento: **SÌ — in arrivo, va in `reference/`**
- Dimensione finestra (aspect): `__________`
- Per OGNI elemento — baked vs codice:
  - sfondo/piastra: baked (inerte)
  - knob/slider/switch: asset PNG-alpha
  - titolo/label/valori/scale/indicatori/meter: **codice** (mai baked se cambiano)

## 6. Asset attesi (PNG con alpha, @2x, in `template/source/assets/src/`)
- [ ] sfondo piastra (può essere JPEG se opaco, ma senza elementi mobili/testo che cambiano)
- [ ] knob (indicatore a ore 12)
- [ ] cap fader (se ci sono fader)
- [ ] switch stati on/off (se servono)
- [ ] altro: `__________`

## 7. Formati & piattaforme
- Formati: **AU + VST3**
- Piattaforme: **macOS** (universal, arm64+x86_64)

## 8. Validazione richiesta
- null-test, pluginval(10), auval, render headless (vedi CHECKLIST.md)
