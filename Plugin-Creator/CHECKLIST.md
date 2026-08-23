# CHECKLIST — validazione prima di "funziona"

## DSP / correttezza
- [ ] `processBlock`: nessuna allocazione (filtri precalcolati o TPT)
- [ ] tutti i parametri continui smussati (no zipper su automazione)
- [ ] `isBusesLayoutSupported`: mono e stereo gestiti
- [ ] bypass funzionante (processBlockBypassed o bypass param)
- [ ] `ScopedNoDenormals` presente
- [ ] **null-test** a parametri neutri: output == input (diff < -120 dBFS)
- [ ] **sweep** HP/LP/parametri continui senza zipper/glitch
- [ ] **transienti** (kick) ok per attack/release e meter
- [ ] sample-rate 44.1 / 48 / 96 kHz, block size piccolo e grande

## Validazione formati
- [ ] build Release AU + VST3 senza warning NUOVI
- [ ] `auval -v aufx <CODE> <MANU>` → SUCCEEDED   (macOS)
- [ ] `pluginval --strictness-level 10` su AU e VST3 → PASS

## UI
- [ ] `tools/shoot` produce il render REALE dell'editor
- [ ] overlay del render sul riferimento: controlli allineati
- [ ] stati estremi verificati (switch ON/OFF, knob min/max, meter pieno)
- [ ] nessun numero magico nei .cpp (tutto in layout.json)
- [ ] nessun testo/indicatore mobile cotto nello sfondo

## Asset & repo
- [ ] tutti gli asset si rigenerano con `python tools/build_assets.py <manifest>`
- [ ] asset finali in BinaryData; sorgenti grezzi NON in BinaryData
- [ ] VERSION coerente CMake ↔ UI
- [ ] `.gitignore` esclude `build/`
- [ ] commit per ogni iterazione (storia con diff/rollback reali)
