# MIX COMPASS 0.1.0 (MVP)

Carichi un pezzo trap di riferimento, metti il plugin **ultimo sul master** e ti dice in italiano dove il tuo mix
è diverso. Hai anche un **A/B allo stesso volume**. In modalità *A* l'audio passa intatto.
Contratto: [PLUGIN_SPEC.md](PLUGIN_SPEC.md).

## Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j 10
```
Il build installa da solo `MIX COMPASS.component` e `MIX COMPASS.vst3` in `~/Library/Audio/Plug-Ins`.
JUCE 8.0.8 viene riusato da `../AtmosForge/build/_deps` se c'è, altrimenti si scarica.

## Test
| comando | cosa verifica |
|---|---|
| `./build/analysis_test` | libreria di analisi, 67 controlli: EBU Tech 3341 casi 1–5, true peak tra campioni, rumore rosa piatto, correlazione, frasi; a 44.1/48/96 kHz |
| `./build/plugintest_artefacts/Release/plugintest` | 26 controlli: null-test bit-identico mono/stereo × 44.1/48/96, zero allocazioni in A e in B, riferimento WAV 44.1 kHz su host 48 kHz, level match (±1 LU), ritorno ad A, stato salvato, bypass, layout |
| `auval -v aufx Mxcp Mypl` | validazione AU |
| `pluginval --strictness-level 10` | AU e VST3 |
| `./build/shoot_demo_artefacts/Release/shoot_demo shots/ui_demo.png` | render reale dell'editor con dati veri (`shoot` lo fa a vuoto) |

## Struttura
- `source/analysis/`: libreria **senza JUCE**, riusabile da VOCAL FORGE. Contiene:
  - `KWeighting`, `Loudness` (BS.1770-4) e `TruePeak` (4×);
  - `FFT`, `Spectrum` (1/6 d'ottava, stereo per banda);
  - `MixAnalyzer` (Snapshot) e `Compare` (le frasi).
- `PluginProcessor`: il thread audio copia soltanto l'ingresso in un FIFO lock-free. L'analisi gira su un suo thread
  e pubblica uno Snapshot alla UI con un triple buffer. Il riferimento passa al thread audio con un puntatore atomico + hazard.
- `ReferenceLoader`: decodifica, ricampionamento e analisi del file, su un thread separato.
- `Views` / `PluginEditor`: UI vettoriale dell'MVP. Tutte le posizioni stanno in `source/assets/layout.json`.
