# CHECKLIST — VOCAL FORGE

Include **integralmente** `../Plugin-Creator/CHECKLIST.md`. Qui sotto solo le voci aggiuntive di dominio.

## Testo → chain (compilatore)
- [ ] stesso prompt + stesso profilo → preset **identico** (determinismo, 100 esecuzioni)
- [ ] ogni parametro generato ha un `why` non vuoto
- [ ] preset conforme a `schema/chain_preset.schema.json`; intent conforme a `schema/intent.schema.json`
- [ ] nessun numero DSP hardcoded fuori da `tools/data/rules.json`
- [ ] prompt vuoto / senza parole note → profilo neutro + elenco dei termini non riconosciuti
- [ ] i 12 prompt di `examples/` rigenerano output **byte-identici** (`--examples --check`)
- [ ] **parità Python ↔ C++** su tutti gli esempi (interi esatti, float entro 1e-6)
- [ ] cambiare `sourceProfile` cambia davvero la chain (stanza trattata ≠ non trattata)

## DSP della voce
- [ ] **null-test**: tutti i moduli off, trim 0, mix 100 → diff < −120 dBFS
- [ ] nessuno stadio di compressione supera **6 dB** di GR sul materiale di test
- [ ] nessun clipping interno con ingresso a **−6 dBFS** di picco (check su tutti i punti di misura)
- [ ] target loudness del preset rispettato **±1 LU** (misura integrata sul frammento di test)
- [ ] **sibilanti**: frasi con molte "s" — nessun lisp, nessuna sibilante non trattata dopo la saturazione
- [ ] **plosive**: "p"/"b" ravvicinate — HPF e gate non producono pompaggio
- [ ] **gate** sul noise floor reale della stanza: non taglia le code di parola, non apre sul rumore
- [ ] **room-tamer**: massimo 3 notch attivi, nessuna banda oltre −8 dB
- [ ] **latenza dichiarata** corretta (verifica di allineamento in Logic con nulling contro traccia parallela)
- [ ] mono→stereo e stereo→stereo, a 44.1 / 48 / 96 kHz, block piccolo e grande
- [ ] passaggio da un prompt a un altro **mentre l'audio suona**: nessun click (tutti i target smussati)

## Logic
- [ ] `auval -v aufx Vfrg Mypl` → SUCCEEDED
- [ ] il plugin appare in Logic, si automatizza, salva/ricarica lo **stato completo incluso `promptText`**
- [ ] la ricetta stock esportata è riproducibile a mano in Logic in **< 5 minuti** (prova cronometrata)
- [ ] la ricetta usa **solo plugin stock** (nessuna dipendenza da terze parti)
- [ ] nessuna scrittura nella libreria utente di Logic senza conferma esplicita

## UI
- [ ] il pannello "perché" mostra la motivazione di ogni modulo attivo
- [ ] badge "modificato" quando l'utente tocca un parametro dopo la generazione
- [ ] prompt box: testo lungo, emoji, caratteri non ASCII → nessun crash, nessun troncamento silenzioso
