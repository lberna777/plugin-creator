# IDEA — VOCAL FORGE

## Una riga
Un plugin AU/VST3 a cui **scrivi a parole** che voce vuoi ("trap aggressiva tipo Travis, ma non stridula")
e che **costruisce la vocal chain giusta**, sapendo già da dove viene il segnale:
**stanza non trattata + microfono Focusrite + Scarlett a due ingressi**, e che **finisce dentro Logic**.

## Il problema vero
Le vocal chain "da preset" falliscono per un motivo preciso: sono tarate su un segnale che non hai.
Chi registra in cameretta ha tre problemi che nessun preset generico conosce:

1. **La stanza suona.** Riflessioni precoci, boxiness 150–400 Hz, code di flutter 1–4 kHz, rumore di fondo.
2. **La catena è nota e fissa.** Microfono Focusrite (dinamico o condensatore da entry-level) su Scarlett a
   due ingressi: preamp con un carattere prevedibile, headroom prevedibile, rumore prevedibile.
3. **Chi mixa parla in aggettivi**, non in dB: "più avanti", "più calda", "meno nasale", "voglia di radio".

Il punto è che 1 e 2 sono **costanti misurabili** e 3 è **traducibile**. Quindi la chain non va indovinata:
va *compilata*, come si compila un sorgente, da un intento testuale + un profilo di sorgente noto.

## L'idea
Un **compilatore di vocal chain**:

```
testo utente  ─┐
profilo stanza ├─►  INTENT (JSON)  ─►  REGOLE  ─►  CHAIN PRESET (JSON)  ─►  ┌─ plugin AU/VST3 (suona)
profilo mic/IF ┘                                     (13 moduli)            └─ ricetta Logic (stock plugin)
```

- **Un solo plugin**, non 12 in serie: catena interna fissa nell'ordine, moduli accesi/spenti e tarati dal preset.
- **Nessun parametro inventato a occhio**: ogni valore esce da regole tracciabili (`tools/data/rules.json`),
  e il plugin mostra *perché* ("cut 240 Hz −3.5 dB: boxiness stanza non trattata").
- **Analisi del segnale reale (fase 2)**: 8 secondi di parlato → misura noise floor, fondamentale,
  risonanze, sibilanza, crest factor → le regole si spostano sul *tuo* segnale, non sulla media.
- **Uscita su Logic in due modi**: il plugin gira come AU dentro Logic (path primario), e in più esporta
  una **ricetta con i soli plugin stock di Logic** (Channel EQ, Compressor, DeEsser 2, Phat FX, ChromaVerb…)
  con valori esatti, per chi vuole la chain "aperta" e modificabile.

## Perché non è "l'ennesimo preset manager"
- Il dominio è **chiuso e dichiarato** (una stanza non trattata, un mic, una interfaccia): questo permette
  scelte forti che un plugin generico non può fare — es. gate key-filtrato *sempre* attivo, HPF mai sotto 70 Hz,
  de-esser sdoppiato pre/post compressione, notch dinamici sui modi tipici delle stanze piccole.
- Il testo non sceglie un preset da una lista: **genera** i valori. "Aggressiva" e "aggressiva ma scura"
  producono due chain diverse, non la stessa con un tilt.
- Tutto ciò che è misurabile viene misurato (null-test, sweep, analisi del segnale), come impone `Plugin-Creator/CLAUDE.md`.

## Fasi
| Fase | Contenuto | Definizione di "fatto" |
|---|---|---|
| **F0** | Scaffold, spec congelata, grammatica del prompt, regole v1, compilatore Python + test | `chain_compiler.py` produce preset validi per 12 prompt d'esempio |
| **F1** | Plugin JUCE: catena DSP completa, parametri APVTS, bypass, null-test, pluginval 10, auval | Suona e valida senza UI definitiva |
| **F2** | Prompt box in UI + motore regole portato in C++ + "spiegazione" dei valori | Scrivi il testo nel plugin, la chain si costruisce |
| **F3** | Analisi del segnale (calibrazione su 8 s di voce reale) | Le regole si adattano al segnale misurato |
| **F4** | Export ricetta Logic (Markdown/JSON) + tentativo Channel Strip `.cst` | Ricetta riproducibile a mano in Logic in < 5 minuti |
| **F5** | UI fotorealistica da `layout.json` + render headless vs riferimento | Regole grafiche di `Plugin-Creator/CLAUDE.md` rispettate |

## Rischi dichiarati
- **`.cst` di Logic è un formato non documentato.** Path primario = plugin AU + ricetta leggibile.
  Il `.cst` resta R&D: se non si reverse-engineerizza in modo affidabile, si taglia senza danno.
- **Nessun LLM nel path audio.** Il parsing del testo è deterministico (vocabolario + regole).
  Un eventuale LLM sta *fuori* dal real-time e serve solo a mappare frasi ignote sul vocabolario.
- **"Perfetta" è una parola dell'utente, non una specifica.** Il contratto misurabile è:
  no zipper, no clipping, gain-staged, sibilanti sotto controllo, risonanze di stanza attenuate,
  e ogni valore giustificabile.
