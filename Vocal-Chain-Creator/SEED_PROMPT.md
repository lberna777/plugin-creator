# SEED_PROMPT — VOCAL FORGE, il prompt del "giorno 1"

Copia/incolla questo all'assistente per avviare il sottoprogetto.
È scritto nella forma del `SEED_PROMPT.md` del padre, con le precondizioni di dominio in più.

```
Costruisci VOCAL FORGE: un plugin audio JUCE (AU+VST3) che, data una RICHIESTA TESTUALE,
compila una vocal chain completa tarata su una sorgente NOTA, e che gira dentro Logic Pro.
Vincolanti, in quest'ordine: Plugin-Creator/CLAUDE.md, poi Vocal-Chain-Creator/CLAUDE.md.
Prima del codice, applica le PRECONDIZIONI.

[SORGENTE — costante del progetto, non si negozia]
- Stanza NON trattata, piccola (cameretta/home studio): boxiness 150–400 Hz, riflessioni precoci,
  flutter 1–4 kHz, noise floor alto, nessun bass trap. Il segnale arriva SEMPRE con un po' di stanza dentro.
- Microfono Focusrite (dinamico o condensatore entry-level, cardioide), ripresa ravvicinata
  → effetto prossimità, sibilanti marcate, plosive.
- Interfaccia Scarlett a DUE ingressi (2i2): preamp pulito, headroom limitato, il gain lo sbaglia l'utente.
  Assumere livello d'ingresso NON ottimale e correggerlo esplicitamente (input trim + target loudness).
- Destinazione: Logic Pro. Il plugin gira come AU; in più esporta una ricetta con i soli plugin STOCK di Logic.
  Dettagli in knowledge/ROOM_MIC_PROFILE.md e knowledge/LOGIC_INTEGRATION.md.

[SPEC] — vedi PLUGIN_SPEC.md (già compilato e CONGELATO)
- Identità: VOCAL FORGE / PLUGIN_CODE "Vfrg" / MANUFACTURER "Mypl" / com.myplugins.vocalforge / 1.0.0
- Catena (ordine fisso, moduli attivabili):
  in → trim+polarity → gate key-filtered → HPF → room-tamer (3 notch dinamici) → EQ sottrattiva
     → de-esser 1 → comp 1 (veloce) → comp 2 (lento/glue) → saturazione → EQ tonale → de-esser 2
     → limiter → out (+ mandate delay/riverbero come suggerimento, NON come effetto interno)
- I/O: mono→stereo e stereo→stereo. Sidechain esterno: no. Latenza: solo lookahead del limiter, dichiarata.

[TESTO → CHAIN — il cuore del progetto]
- Il testo NON seleziona un preset da una lista: genera i valori.
- Pipeline deterministica: testo → INTENT (schema/intent.schema.json) → REGOLE (tools/data/rules.json)
  → CHAIN PRESET (schema/chain_preset.schema.json).
- Il riferimento eseguibile esiste già: tools/chain_compiler.py + i suoi test. Il C++ deve
  riprodurre LO STESSO output byte-per-byte sui preset d'esempio (test di parità obbligatorio).
- Nessun LLM nel path audio. Un LLM è ammesso solo offline, per mappare frasi ignote sul vocabolario
  di knowledge/PROMPT_GRAMMAR.md, e il suo output deve comunque passare per le stesse regole.
- Ogni valore prodotto porta con sé una MOTIVAZIONE ("why") mostrata in UI. Un valore senza motivazione è un bug.

[DECISIONI PRIMA DI MONTARE]
- Per OGNI elemento UI: baked o codice? (cambia/si muove/mostra stato → NON baked)
- Layout in layout.json (0..1). Vietati numeri magici nei .cpp — e vietati anche i valori DSP hardcoded:
  stanno in rules.json, imbarcato come BinaryData.
- Le posizioni dei controlli si MISURANO dal riferimento, non si stimano.

[LOOP OBBLIGATORIO, DAL PRIMO COMMIT]
1. Compilatore di regole prima di tutto (Python, già presente): test verdi su tutti i prompt d'esempio.
2. DSP: SmoothedValue su tutti i continui, ZERO alloc in processBlock, filtri TPT,
   isBusesLayoutSupported (mono+stereo), atomics verso la UI, bypass, ScopedNoDenormals.
3. Validazione DSP PRIMA della grafica: null-test (tutti i moduli off → diff < −120 dBFS),
   pluginval --strictness-level 10, auval -v aufx Vfrg Mypl.
4. Test di dominio voce: sweep, sibilanti, plosive, transienti, gate su noise floor reale,
   nessun clipping interno a −6 dBFS d'ingresso, target loudness rispettato ±1 LU.
5. UI montata da layout.json + asset; verifica con render HEADLESS (tools/shoot) vs reference/.
6. Commit a ogni iterazione (anche WIP). Asset solo via tools/build_assets.py + manifest.

[OUTPUT ATTESO]
- Plugin che valida, suona e non alloca; scrivi una frase nel box e la chain si costruisce, con il "perché" a schermo.
- Parità Python ↔ C++ sui preset di examples/.
- Ricetta Logic (stock plugin) esportata e riproducibile a mano in meno di 5 minuti.
- Chiedimi conferma SOLO su ciò che non puoi misurare o derivare (estetica, semantica ambigua).
  Tutto il misurabile, misuralo.
```
