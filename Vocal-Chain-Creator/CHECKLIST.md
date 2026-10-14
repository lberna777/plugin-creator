# CHECKLIST — VOCAL FORGE

> Stato al primo build: le voci marcate [x] sono verificate in automatico da
> `plugin/tools/vocalforge_selftest` e `plugin/tools/vocalforge_parity`, che girano a ogni build.
> Le voci che richiedono macOS o un DAW restano aperte e sono dichiarate tali in `plugin/INSTALL_MAC.md`.

Include **integralmente** `../Plugin-Creator/CHECKLIST.md`. Qui sotto solo le voci aggiuntive di dominio.

## Testo → chain (compilatore)
- [x] stesso prompt + stesso profilo → preset **identico** (determinismo, 100 esecuzioni)
- [x] ogni parametro generato ha un `why` non vuoto
- [ ] preset conforme a `schema/chain_preset.schema.json`; intent conforme a `schema/intent.schema.json`
- [x] nessun numero DSP hardcoded fuori da `tools/data/rules.json`
- [ ] prompt vuoto / senza parole note → profilo neutro + elenco dei termini non riconosciuti
- [x] i 12 prompt di `examples/` rigenerano output **byte-identici** (`--examples --check`)
- [x] **parità Python ↔ C++** su tutti gli esempi (interi esatti, float entro 1e-6)
- [x] cambiare `sourceProfile` cambia davvero la chain (stanza trattata ≠ non trattata)

## DSP della voce
- [x] **null-test**: tutti i moduli off, trim 0, mix 100 → diff < −120 dBFS
- [ ] nessuno stadio di compressione supera **6 dB** di GR sul materiale di test
- [x] nessun clipping interno con ingresso a **−6 dBFS** di picco (check su tutti i punti di misura)
- [ ] target loudness del preset rispettato **±1 LU** (misura integrata sul frammento di test)
- [ ] **sibilanti**: frasi con molte "s" — nessun lisp, nessuna sibilante non trattata dopo la saturazione
- [ ] **plosive**: "p"/"b" ravvicinate — HPF e gate non producono pompaggio
- [ ] **gate** sul noise floor reale della stanza: non taglia le code di parola, non apre sul rumore
- [ ] **room-tamer**: massimo 3 notch attivi, nessuna banda oltre −8 dB
- [x] **latenza dichiarata** presente (allineamento in Logic ancora da confermare) (verifica di allineamento in Logic con nulling contro traccia parallela)
- [x] mono→stereo e stereo→stereo, a 44.1 / 48 / 96 kHz, block piccolo e grande
- [x] passaggio da un prompt a un altro **mentre l'audio suona**: nessun click (tutti i target smussati)

## Logic
- [ ] `auval -v aufx Vfrg Mypl` → SUCCEEDED
- [~] stato completo incluso `promptText` verificato headless; da confermare dentro Logic
- [ ] la ricetta stock esportata è riproducibile a mano in Logic in **< 5 minuti** (prova cronometrata)
- [ ] la ricetta usa **solo plugin stock** (nessuna dipendenza da terze parti)
- [ ] nessuna scrittura nella libreria utente di Logic senza conferma esplicita

## UI
- [ ] il pannello "perché" mostra la motivazione di ogni modulo attivo
- [ ] badge "modificato" quando l'utente tocca un parametro dopo la generazione
- [ ] prompt box: testo lungo, emoji, caratteri non ASCII → nessun crash, nessun troncamento silenzioso
