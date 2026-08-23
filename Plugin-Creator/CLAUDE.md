# CLAUDE.md — convenzioni per plugin audio JUCE

Istruzioni vincolanti per chi genera/modifica un plugin in questo scaffold.
Nascono dall'autopsia di NEMO: ogni regola corrisponde a un errore pagato lì.

## DSP (path audio)
- **Zero allocazioni in `processBlock`.** I coefficienti dei filtri si precalcolano per-blocco
  o si usano filtri TPT (`dsp::StateVariableTPTFilter`). Mai `makeHighPass`/`new` per-sample.
  (NEMO: `PluginProcessor.cpp:182-192` allocava a ogni sample durante un drag.)
- **Tutti i parametri continui via `SmoothedValue`** — inclusi makeup, width, gain, mix.
  (NEMO: makeup e width non smussati → zipper.)
- **`isBusesLayoutSupported`** gestito: mono e stereo. Mai un `return` che lascia il buffer intatto.
  (NEMO: `if (numChannels < 2) return;` = silenzio funzionale sulle tracce mono.)
- **UI ← DSP solo via `std::atomic`** (lock-free). Niente lock né puntatori condivisi.
- **Bypass**: implementa `processBlockBypassed` o usa il bypass param di JUCE.
- **`ScopedNoDenormals`** all'inizio di `processBlock`.

## Parametri
- Tutti in `AudioProcessorValueTreeState`, con id stabili (mai rinominati dopo il rilascio).
- Range/skew/default/unità decisi nello spec e congelati.

## UI / Asset
- **Sfondo = piastra inerte.** Niente testo, indicatori, scale, valori, parti mobili cotti dentro.
- **Asset mobili = PNG con alpha**, @2x, sfondo trasparente, un file per elemento
  (knob con indicatore a ore 12; cap fader; switch in stati on/off). Sorgenti in `assets/src/`, versionati.
- **Layout in `layout.json`** (coordinate normalizzate 0..1) letto da `Layout.h`. **Vietati numeri magici nei `.cpp`.**
- **Le posizioni si misurano** dall'immagine di riferimento (detection), non a occhio.
- Decidi per ogni elemento **baked vs codice** PRIMA di montarlo: se cambia/si muove/mostra stato → non baked.

## Build
- `juce_add_binary_data` solo per gli **asset finali** (non i sorgenti grezzi).
- **Identità** (`PRODUCT_NAME`, `PLUGIN_CODE`, `BUNDLE_ID`, `COMPANY_NAME`) decisa al giorno 1 e congelata
  (rinominarla dopo = i progetti perdono l'istanza del plugin).
- `VERSION` in CMake = unica fonte di verità, mostrata anche in UI.
- `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0` salvo necessità (riduce dipendenze, utile su Linux).

## Test — prima di dire "funziona"
- **null-test** a parametri neutri (output == input, diff < -120 dBFS).
- **`pluginval --strictness-level 10`** su AU e VST3.
- **`auval -v aufx <code> <manu>`** (macOS).
- **sweep** filtri, **transienti**, **mono e stereo**, a 44.1/48/96 kHz.
- **`tools/shoot`**: il render reale dell'editor combacia col riferimento.

## Loop di lavoro
1. DSP prima, validato (null-test+pluginval) PRIMA della grafica.
2. UI da `layout.json`.
3. `build → shoot → confronto col riferimento` a ogni iterazione.
4. Commit a ogni iterazione (anche WIP grafico): diff e rollback devono esistere.
5. Ogni trasformazione d'immagine sta in `tools/build_assets.py` + manifest. Niente editing usa-e-getta.
