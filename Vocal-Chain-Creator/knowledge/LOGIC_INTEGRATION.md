# Entrare in Logic Pro — tre strade, una obbligatoria

## Strada A (obbligatoria) — il plugin come AU
La destinazione dichiarata del progetto è Logic, quindi **AU non è un formato opzionale**: è il path primario.

Requisiti non negoziabili:
- `auval -v aufx Vfrg Mypl` → **SUCCEEDED** (sottotipo `aufx`, categoria dynamics/EQ).
- Il **prompt testuale fa parte dello stato**: salvare il progetto, chiuderlo e riaprirlo deve restituire
  la stessa chain **e** lo stesso testo. (Lo stato va nel blob dell'APVTS, non in un file esterno.)
- **Latenza dichiarata** con `setLatencySamples` (solo lookahead del limiter): Logic compensa da solo,
  ma solo se gliela dichiari. Test: nulling contro una traccia parallela non processata.
- Automazione: tutti i parametri della tabella in `PLUGIN_SPEC.md` sono automatizzabili; `promptText` **no**
  (è stato, non parametro).
- Bundle universal (arm64 + x86_64) — Logic gira nativo su Apple Silicon.
- Installazione: `~/Library/Audio/Plug-Ins/Components/VOCAL FORGE.component`.
  Se Logic non lo vede: `killall -9 AudioComponentRegistrar`, poi riaprire Logic.

**Posizione consigliata nella traccia**: unico plugin della strip vocale, prima di eventuali
Tuner/Pitch Correction se li vuoi *dopo* la dinamica, o dopo di essi se li vuoi *prima*. Le mandate
(riverbero/delay) restano fuori, su bus aux.

## Strada B (consigliata, sempre disponibile) — la ricetta con i plugin stock
Il compilatore esporta una **ricetta** in Markdown + JSON che traduce la chain nei soli plugin **stock di Logic**.
Serve a due cose: capire *cosa* fa il plugin, e avere la chain "aperta" e modificabile senza dipendere da noi.

Mappatura dei moduli:

| Modulo della chain | Plugin stock di Logic | Note |
|---|---|---|
| Trim / Polarity | **Gain** | Phase invert incluso |
| Gate | **Noise Gate** | usare *Side Chain Filter* (HP/LP) = il nostro key-filter; Hysteresis attiva |
| HPF | **Channel EQ** (banda 1, HP) | slope 24 dB/oct |
| Room-tamer | **Channel EQ** dinamico non esiste → usare **Multipressor** o notch stretti in Channel EQ | il "dinamico" si perde: la ricetta lo dichiara come approssimazione |
| EQ sottrattiva | **Channel EQ** (bande 2–4) | |
| De-esser 1 | **DeEsser 2** | modo *Split*, prima del compressore |
| Comp 1 | **Compressor** (Platinum o FET) | attacco veloce |
| Comp 2 | **Compressor** (VCA/Opto) | attacco lento, ratio basso |
| Saturazione | **Phat FX** (Distortion) o **Tape Delay** off-mix / **Vintage EQ** | scelta secondo `sat.type` |
| EQ tonale | **Channel EQ** (secondo strumento) o **Vintage Console EQ** | |
| De-esser 2 | **DeEsser 2** | modo *Wide*, dopo la saturazione |
| Limiter | **Limiter** | ceiling dal preset |
| Mandate | **ChromaVerb** / **Tape Delay** su bus aux | mai in serie sulla voce |

Regole della ricetta:
- **Solo plugin stock.** Nessuna dipendenza da terze parti, altrimenti non è riproducibile da chiunque.
- Ogni riga porta i valori esatti e la motivazione (`why`).
- Dove lo stock non può fare quello che fa la chain (notch dinamici), la ricetta lo **dichiara**
  come approssimazione invece di fingere equivalenza.
- Prova cronometrata: un utente deve poterla montare a mano in **meno di 5 minuti**.

## Strada C (R&D, tagliabile) — il Channel Strip `.cst`
Logic salva le strip in `~/Music/Audio Music Apps/Channel Strip Settings/Audio/*.cst`: formato binario
**non documentato** (plist con blob di stato dei plugin). Generarlo significa reverse-engineering dei blob AU.

Posizione del progetto:
- È esplicitamente **fase F4 e tagliabile**: se non è affidabile al 100%, non si spedisce.
- Approccio realistico: partire da un `.cst` salvato a mano dall'utente come **template**, e sostituire
  i soli blob dei plugin di cui conosciamo la struttura — non generarlo da zero.
- **Mai scrivere dentro la libreria di Logic senza conferma esplicita dell'utente**, e mai sovrascrivere
  una strip esistente: scrivere sempre un nome nuovo.

## Cosa NON facciamo
- Niente scripting/automazione di Logic via AppleScript o accessibilità: fragile, si rompe a ogni update.
- Niente installazione automatica di preset all'insaputa dell'utente.
