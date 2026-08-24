# CLAUDE.md — VOCAL FORGE (aggiunte di dominio)

`../Plugin-Creator/CLAUDE.md` resta **integralmente valido**. Questo file aggiunge i vincoli
che nascono dal dominio: voce, stanza non trattata, catena Focusrite→Scarlett, destinazione Logic.
In caso di conflitto vince la regola **più restrittiva**.

## Testo → chain
- La pipeline è **deterministica**: stesso testo + stesso profilo di sorgente → stesso preset, sempre.
  Nessuna randomizzazione, nessuna chiamata di rete nel path di generazione.
- Il parsing produce un **INTENT** conforme a `schema/intent.schema.json`; le regole leggono l'intent,
  mai il testo grezzo. Chi aggiunge una parola tocca `knowledge/PROMPT_GRAMMAR.md` + `tools/data/rules.json`, non il codice.
- **Tutti** i numeri (dB, Hz, ratio, ms) stanno in `tools/data/rules.json`. Un numero DSP hardcoded in un `.cpp`
  o in `chain_compiler.py` è un bug, esattamente come un pixel hardcoded nella UI.
- Ogni parametro generato porta un campo `why` (stringa breve, causale). Il preset senza `why` non è valido.
- Testo non riconosciuto → si applica il profilo **neutro** e si dichiara cosa non si è capito.
  Mai inventare un intento non presente nel vocabolario.
- **Parità Python ↔ C++**: `tools/chain_compiler.py` è la specifica eseguibile. Il motore C++ deve produrre
  gli stessi preset di `examples/` (confronto JSON, tolleranza 0 su interi, 1e-6 sui float).

## DSP della voce
- **Ordine della catena fisso** (vedi `knowledge/CHAIN_ARCHITECTURE.md`): i moduli si accendono/spengono,
  non si riordinano. Riordinare = un altro plugin.
- **De-esser sdoppiato**: uno prima della compressione (protegge il detector), uno dopo la saturazione
  (che genera armoniche sulle sibilanti). Mai un solo de-esser al 100%.
- **Gate**: detector **key-filtered** (band-pass sulla banda di voce), mai sul segnale full-range,
  altrimenti il rumore di stanza apre il gate. Hysteresis obbligatoria.
- **HPF**: mai sotto 70 Hz e mai sopra 140 Hz da regole automatiche. Fuori da questo range solo su richiesta esplicita.
- **Room-tamer**: massimo 3 notch, Q stretti, **attenuazione dinamica** (agiscono solo quando la risonanza supera
  la soglia), guadagno complessivo mai oltre −8 dB su una singola banda.
- **Compressione in due stadi**: nessuno stadio da solo può superare **6 dB** di riduzione.
- **Gain staging esplicito**: input trim porta al target di lavoro, output porta al target di loudness dichiarato
  nel preset. Nessuno stadio interno può clippare con ingresso a −6 dBFS di picco.
- Il **riverbero/delay non è mai in serie**: la voce esce asciutta dalla catena e gli effetti di ambiente
  vivono su **due bus paralleli** (uno reverb, uno delay), ognuno filtrato HP+LP e duckato dalla voce.
  Un send non può superare −6 dB. Il gruppo sceglie **una sola** variante, per priorità, in modo deterministico.
  `sendsMode = internal` li fa girare nel plugin, `sendsMode = logic` li spegne e li esporta come bus aux.
- Latenza: solo il lookahead del limiter, **dichiarata** con `setLatencySamples`.

## Sorgente
- Il profilo di sorgente (`stanza`, `mic`, `interfaccia`) è un **input del preset**, non un'ipotesi implicita:
  compare in `source_profile` e cambia il risultato. Cambiare stanza deve cambiare la chain.
- Il default del progetto è quello dichiarato in `knowledge/ROOM_MIC_PROFILE.md` e non si modifica
  "per far suonare meglio un esempio".
- Se in futuro esiste l'analisi del segnale (F3), le misure **sovrascrivono** le stime del profilo,
  e il `why` deve dire quale delle due ha deciso.

## Logic
- Path primario: **il plugin come AU dentro Logic**. Tutto il resto è aggiuntivo e non può bloccare il rilascio.
- La ricetta stock è un **artefatto testuale versionato** (Markdown + JSON), riproducibile a mano.
- `.cst` (Channel Strip) è **R&D**: se non è affidabile al 100% non si spedisce. Mai scrivere file
  dentro la libreria utente di Logic senza conferma esplicita dell'utente.

## Test — prima di dire "funziona"
Oltre alla `CHECKLIST.md` del padre: null-test con tutti i moduli off, sibilanti, plosive, gate sul noise
floor reale della stanza, parità Python↔C++, target loudness ±1 LU. Dettaglio in `CHECKLIST.md`.
