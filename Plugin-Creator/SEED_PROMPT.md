# SEED_PROMPT — il prompt del "giorno 1"

Copia/incolla questo all'assistente quando vuoi avviare un nuovo plugin in questo scaffold.

```
Costruisci un plugin audio JUCE (AU+VST3) funzionante, funzionale e bello AL PRIMO GIRO,
usando questo scaffold (CLAUDE.md è vincolante). Prima del codice, applica le PRECONDIZIONI.

[SPEC] — vedi/compila PLUGIN_SPEC.md
- Tipo di effetto: <…>
- Parametri (id, nome, range, skew, default, unità, smussato): <…>
- Catena di processing (ordine): <in → … → out>
- I/O e bus (mono/stereo/sidechain): <…>
- Identità DEFINITIVA (nome, PLUGIN_CODE, manufacturer, BUNDLE_ID): <…>  ← si congela ORA
- Formati/piattaforme: <…>
- Riferimento visivo: reference/<file> (allegato) — oppure "genera tu lo stile"
- Asset mobili: PNG con ALPHA @2x in template/source/assets/src/ (un file per elemento;
  knob con indicatore a ore 12; cap fader; switch on/off). Sfondo = PIASTRA INERTE.

[DECISIONI PRIMA DI MONTARE]
- Per OGNI elemento: baked o codice? (cambia/si muove/mostra stato → NON baked)
- Layout in layout.json (coordinate 0..1). Vietati numeri magici nel .cpp.
- Le posizioni dei controlli si MISURANO dall'immagine (detection numpy), non si stimano.

[LOOP OBBLIGATORIO, DAL PRIMO COMMIT]
1. DSP prima: SmoothedValue su tutti i continui, ZERO alloc in processBlock,
   isBusesLayoutSupported (mono+stereo), atomics verso la UI, bypass gestito.
2. Validazione DSP: null-test + auval + pluginval(strict 10) PRIMA della grafica.
3. UI montata da layout.json + asset.
4. Verifica con render HEADLESS (tools/shoot → PNG del vero render JUCE) vs riferimento.
   NON usare mock/PIL come prova finale.
5. Commit a ogni iterazione (anche WIP grafico).
6. Asset solo via tools/build_assets.py + manifest (assert su alpha e dimensioni).

[OUTPUT ATTESO]
Plugin che valida e suona (null-test ok), UI allineata al riferimento al primo render,
repo con CLAUDE.md + layout.json + build_assets.py + shoot + checklist.
Chiedimi conferma SOLO su ciò che non puoi misurare o derivare (estetica, semantica
ambigua di un asset). Tutto il misurabile, misuralo.
```
