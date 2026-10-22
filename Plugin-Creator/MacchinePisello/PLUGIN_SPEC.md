# PLUGIN_SPEC — MACCHINE PISELLO

> Contratto del plugin. Identità e id dei parametri sono **congelati**.

## 1. Identità (congelata il 2026-09-15)
- Nome prodotto (mostrato nel DAW): **Macchine Pisello**
- Target CMake: `MacchinePisello`
- PLUGIN_CODE: **Mcps**
- PLUGIN_MANUFACTURER_CODE: **Mypl**
- COMPANY_NAME: **MyPlugins**
- BUNDLE_ID: **com.myplugins.macchinepisello**
- Versione iniziale: **1.0.0** (unica fonte: `project(... VERSION)` in CMake, mostrata in UI)

## 2. Cosa fa
- Categoria: **Instrument — synth semi-modulare monofonico per ambient / ritmica generativa**
  (famiglia Mother-32 / 0-Coast / DFAM / Music Easel).
- Una riga: *una macchina analogica che suona da sola: due VCO che derivano, wavefolder,
  ladder 24 dB, sequencer a 8 step con probabilità/mutazione/ratchet, tape delay e uno spazio enorme.*
- Catena: `seq/MIDI → 2×VCO (morph tri→saw→square→pulse) + sub + noise → wavefolder → ladder 24dB (drive) → VCA (AD/S o DRONE) → tape delay → FDN space → width → volume → out`
- Modulazioni: LFO (sine/tri/ramp/square/S&H/drift) → pitch, cutoff, fold; drift analogico (random walk) sui VCO; CHAOS = random per-step su cutoff/decay/accento.
- Sequencer:
  - **RUN on**: gira quando il DAW è in play (sincronizzato a ppq) oppure quando tieni una nota (clock libero al BPM host, 120 di default). La nota MIDI tenuta **traspone** la sequenza (root di default C3).
  - **RUN off**: synth mono classico da MIDI (last-note priority, glide).
  - Step quantizzati alla scala; PROB = probabilità che lo step suoni; MUTATE = mutazione tipo Turing Machine (a 0 la sequenza si blocca e torna ai knob); RATCHET = probabilità di 2–3 retrigger nello step; SWING sui passi dispari.
  - Pitch bend ±2 st, mod wheel → fold.

## 3. Parametri (id congelati)
| id | UI | tipo | range | skew | default | unità | smussato |
|----|----|------|-------|------|---------|-------|----------|
| oscWave | WAVE | float | 0–1 | lin | 0.40 | morph | sì |
| oscTune | TUNE | float | −24–24 | lin | 0 | st | sì |
| osc2Detune | OSC 2 | float | −12–12 | lin | 0.07 | st | sì |
| glide | GLIDE | float | 0–2000 | log-ish | 25 | ms | sì |
| oscSub | SUB | float | 0–1 | lin | 0.30 | % | sì |
| oscNoise | NOISE | float | 0–1 | lin | 0.04 | % | sì |
| fold | FOLD | float | 0–1 | lin | 0.15 | % | sì |
| drift | DRIFT | float | 0–1 | lin | 0.35 | % | sì |
| cutoff | CUTOFF | float | 20–18000 | log | 700 | Hz | sì (moltiplicativo) |
| reso | RESO | float | 0–1 | lin | 0.45 | % | sì |
| filterEnv | ENV AMT | float | −1–1 | lin | 0.50 | ± | sì |
| filterDrive | DRIVE | float | 0–1 | lin | 0.30 | % | sì |
| attack | ATTACK | float | 0.5–5000 | log | 4 | ms | sì |
| decay | DECAY | float | 10–10000 | log | 420 | ms | sì |
| sustain | SUSTAIN | float | 0–1 | lin | 0.0 | % | sì |
| drone | DRONE | bool | off/on | – | off | – | sì (crossfade VCA) |
| lfoRate | RATE | float | 0.01–30 | log | 0.25 | Hz | sì |
| lfoShape | SHAPE | choice | Sine/Triangle/Ramp/Square/S&H/Drift | – | Sine | – | – |
| lfoPitch | → PITCH | float | 0–1 | lin | 0 | % | sì |
| lfoCutoff | → CUTOFF | float | 0–1 | lin | 0.30 | % | sì |
| lfoFold | → FOLD | float | 0–1 | lin | 0.20 | % | sì |
| chaos | CHAOS | float | 0–1 | lin | 0.15 | % | campionato al trigger |
| seqOn | RUN | bool | off/on | – | on | – | – |
| seqRate | RATE | choice | 1/2,1/4,1/8,1/16,1/32,1/8T,1/16T | – | 1/16 | – | – |
| seqScale | SCALE | choice | Chromatic/Major/Minor/Dorian/Phrygian/Pent. Minor/Whole Tone/Hirajoshi | – | Minor | – | – |
| seqLength | LENGTH | int | 1–8 | – | 8 | step | – |
| seqProb | PROB | float | 0–1 | lin | 0.85 | % | campionato al trigger |
| seqMutate | MUTATE | float | 0–1 | lin | 0.10 | % | campionato al trigger |
| seqRatchet | RATCHET | float | 0–1 | lin | 0.10 | % | campionato al trigger |
| seqSwing | SWING | float | 0–0.6 | lin | 0 | % | campionato al trigger |
| step1…step8 | 1…8 | float | −12–12 | lin | 0,0,7,0,12,3,0,−5 | st | campionato al trigger |
| delayTime | TIME | float | 10–2000 | log | 375 | ms | sì (moltiplicativo, lento = pitch tape) |
| delayFb | FEEDBACK | float | 0–0.97 | lin | 0.45 | % | sì |
| delayMix | DELAY | float | 0–1 | lin | 0.25 | % | sì |
| spaceSize | SPACE | float | 0–1 | lin | 0.70 | decay 0.6–40 s | sì |
| spaceMix | REVERB | float | 0–1 | lin | 0.35 | % | sì |
| width | WIDTH | float | 0–1 | lin | 0.60 | % | sì |
| volume | VOLUME | float | −60–6 | lin | −8 | dB | sì |

> "Campionato al trigger": il valore viene letto solo nell'istante in cui parte uno step, quindi
> non può produrre zipper; uno SmoothedValue lì non avrebbe effetto udibile.

## 4. I/O
- Bus: **nessun input**, output **stereo o mono** (mono = somma L+R).
- MIDI in: sì. MIDI out: no. Latenza 0. Tail dichiarata 10 s.

## 5. UI
- Stile: **vettoriale in codice** (pannello semi-modulare scuro, accento ambra, LED step, oscilloscopio, meter).
- Nessuna immagine bitmap: niente da "cuocere". Tutte le posizioni in `source/assets/layout.json`
  (generato in modo deterministico da `tools/gen_layout.py`, coordinate 0..1).
- Finestra base 1140×720, ridimensionabile ad aspect ratio fisso.

## 6. Formati & piattaforme
- AU (aumu) + VST3 + Standalone, macOS universal (arm64 + x86_64).

## 7. Validazione
- Synth, quindi il "null-test" diventa: (a) **silenzio esatto** senza MIDI/transport; (b) **determinismo**
  e **invarianza al block size** (diff < −120 dBFS); (c) **zero allocazioni** in processBlock (contatore su operator new).
- 44.1 / 48 / 96 kHz, mono e stereo, block 32…1024 e variabile.
- `auval -v aumu Mcps Mypl`, `pluginval --strictness-level 10`, `shoot`.
