# Ricetta Logic — "shiva"

**Mock-up del suono di Shiva.** Drill italiana: voce asciutta e in faccia, poco riverbero, medio-bassi presenti, poca aria.

> Non è la catena reale dell'artista — nessuno la conosce fuori dal suo studio. È una ricostruzione del *risultato* che si sente sui dischi, partendo dal tuo segnale: stanza non trattata, microfono Focusrite, Scarlett a due ingressi.

Generata da `chain_compiler.py` (regole v1.0.0) sul profilo `untreated_room_focusrite_scarlett`.
Solo plugin **stock** di Logic Pro, nella strip della traccia vocale, in quest'ordine.

Target: **-9.0 LUFS**, ceiling **-1.0 dBFS**.

## 1. Gain — Input Trim / Polarity
*Il gain della Scarlett viene quasi sempre impostato a occhio: porto il segnale al livello di lavoro prima che qualunque soglia in dB abbia senso.*

> Gain = inTrim; Phase Invert = polarity

| parametro | valore | perché |
|---|---|---|
| `inTrim` | -6.0 dB | porta i picchi stimati (-6 dBFS) al target di lavoro (-12 dBFS) |
| `polarity` | off  | nessun secondo microfono da allineare: la polarità resta dritta |

## 2. Noise Gate — Gate (key-filtered)
*Stanza non trattata: il noise floor sta a -50 dBFS e tutto quello che viene dopo lo amplificherebbe. Il detector è filtrato sulla banda di voce, così ronzii e ventole non lo aprono.*

> attiva Side Chain Filter (HP=gateKeyLo, LP=gateKeyHi) e Hysteresis −6 dB

| parametro | valore | perché |
|---|---|---|
| `gateThresh` | -35.5 dB | 6 dB sopra il noise floor stimato, più 0.85 di severità richiesta |
| `gateRange` | 19.6 dB | attenuazione parziale: chiudere del tutto si sente più del rumore |
| `gateAtk` | 1.1 ms | apertura rapida ma non istantanea, per non tagliare l'attacco delle consonanti |
| `gateRel` | 124 ms | rilascio lungo per non troncare le code di parola |
| `gateKeyLo` | 120 Hz | il detector ignora sotto la fondamentale: rumore di traffico e ronzii non aprono il gate |
| `gateKeyHi` | 4000 Hz | il detector ignora sopra la banda di intelligibilità: ventole e fruscio non aprono il gate |

## 3. Channel EQ — High-Pass Filter
*Ripresa ravvicinata su cardioide: effetto prossimità (+5 dB) e plosive vanno tolti PRIMA dei detector dinamici.*

> banda 1 in modo High Pass, slope 24 dB/oct

| parametro | valore | perché |
|---|---|---|
| `hpfFreq` | 98 Hz | compensa la prossimità sul registro mid, alzato/abbassato secondo il corpo richiesto |
| `hpfSlope` | 24 dB/oct | pendenza ripida: senza filtro anti-pop le plosive sono transienti di decine di dB |

## 4. Channel EQ — Room Tamer (notch dinamici)
*I modi della stanza si eccitano solo su certe note: tre notch stretti che agiscono SOLO sopra soglia, per non togliere corpo a tutto il resto.*

> APPROSSIMAZIONE: i notch stock sono statici, non dinamici. Usa metà della profondità indicata.

| parametro | valore | perché |
|---|---|---|
| `room1Freq` | 92 Hz | modo assiale stimato della stanza |
| `room1Q` | 6.0 Q | stretto: correzione, non gusto |
| `room1Depth` | -5.4 dB | attenuazione massima quando la risonanza supera la soglia |
| `room2Freq` | 184 Hz | secondo modo / boxiness bassa |
| `room2Q` | 5.0 Q | stretto |
| `room2Depth` | -5.7 dB | meno profondo se è stato chiesto corpo |
| `room3Freq` | 246 Hz | terzo modo / scatola |
| `room3Q` | 5.0 Q | stretto |
| `room3Depth` | -4.8 dB | attenuazione dinamica sulla scatola |
| `roomThresh` | -24.9 dB | sotto questa soglia i notch non lavorano: la voce resta intera |

## 5. Channel EQ — EQ sottrattiva
*Prima togli, poi aggiungi: ogni dB tolto qui è headroom per i due compressori.*

> bande 2–4, campane

| parametro | valore | perché |
|---|---|---|
| `sub1Freq` | 240 Hz | centro della boxiness della stanza (150–400 Hz) |
| `sub1Q` | 2.0 Q | banda media: è fango, non una risonanza singola |
| `sub1Gain` | -3.8 dB | toglie fango, ma restituisce corpo se il testo lo ha chiesto |
| `sub2Freq` | 1000 Hz | banda nasale del registro mid |
| `sub2Q` | 3.0 Q | stretto: la nasalità è localizzata |
| `sub2Gain` | -3.1 dB | toglie il naso senza spegnere l'intelligibilità |
| `sub3Freq` | 2800 Hz | banda del flutter echo della stanza (1000–4000 Hz) |
| `sub3Q` | 2.5 Q | medio |
| `sub3Gain` | -2.0 dB | asprezza della stanza, più quella tolta su richiesta esplicita |

## 6. DeEsser 2 — De-esser 1 (pre-compressione)
*Protegge il detector dei compressori: senza, ogni 's' innesca una riduzione che abbassa la parola intera.*

> modo Split, prima del compressore

| parametro | valore | perché |
|---|---|---|
| `ds1Freq` | 6650 Hz | centro sibilanza stimato del microfono sul registro mid |
| `ds1Thresh` | -23.6 dB | quanto in basso agganciare le esse |
| `ds1Range` | 7.0 dB | attenuazione contenuta: il grosso lo fa il secondo de-esser |
| `ds1Mode` | split  | modo split: agisce solo sulla banda alta, non abbassa tutta la voce |

## 7. Compressor — Compressore 1 (veloce)
*Nessuno canta fermo davanti al microfono: questo stadio prende i picchi e i cambi di distanza. Mai oltre 6 dB.*

> circuito FET (Platinum se preferisci più trasparenza)

| parametro | valore | perché |
|---|---|---|
| `c1Thresh` | -22.4 dB | profondità di lavoro secondo densità (1) e aggressività richieste |
| `c1Ratio` | 4.7 :1 | rapporto moderato, il livellamento vero lo fa il secondo stadio |
| `c1Atk` | 4.4 ms | più aggressivo = attacco più corto = più controllo sui transienti |
| `c1Rel` | 62 ms | rilascio legato alla densità richiesta |
| `c1Knee` | 3.6 dB | ginocchio morbido salvo richiesta aggressiva |
| `c1Makeup` | 5.0 dB | recupero del livello perso, stimato sulla riduzione attesa |

## 8. Compressor — Compressore 2 (lento, glue)
*Livella la frase e dà densità. Due stadi leggeri pompano meno di uno pesante.*

> circuito VCA o Opto

| parametro | valore | perché |
|---|---|---|
| `c2Thresh` | -24.0 dB | aggancia il corpo della frase, non i picchi |
| `c2Ratio` | 3.0 :1 | rapporto basso: colla, non controllo |
| `c2Atk` | 25 ms | attacco lento: lascia passare i transienti |
| `c2Rel` | 180 ms | rilascio lungo, segue la frase |
| `c2Knee` | 8.0 dB | ginocchio morbido: deve essere invisibile |
| `c2Makeup` | 3.5 dB | recupero del livello dello stadio lento |

## 9. Phat FX — Saturazione
*Dopo la dinamica il livello che la attacca è stabile, quindi la quantità di armoniche è prevedibile. Il preamp della Scarlett è pulito: il carattere lo mette qui.*

> solo la sezione Distortion; tape→Tape saturation, tube→Tube, transistor→Transistor

| parametro | valore | perché |
|---|---|---|
| `satDrive` | 45 % | quantità di carattere richiesta dal testo (0.55) |
| `satType` | transistor  | tipo di armoniche coerente con vintage/aggressività richieste |
| `satTilt` | 0.3 dB | inclina lo spettro delle armoniche verso il timbro richiesto |

## 10. Channel EQ — EQ tonale
*Qui si aggiunge, su un segnale già pulito e già denso: i boost non riportano su fango o rumore.*

> seconda istanza, bande a campana + shelf alta per airGain

| parametro | valore | perché |
|---|---|---|
| `tone1Freq` | 180 Hz | corpo del registro mid |
| `tone1Q` | 0.8 Q | campana larga: è timbro, non correzione |
| `tone1Gain` | 0.4 dB | corpo richiesto dal testo |
| `tone2Freq` | 3200 Hz | presenza / intelligibilità |
| `tone2Q` | 1.2 Q | campana media |
| `tone2Gain` | 0.2 dB | ridotto perché il microfono ha già un suo picco di presenza |
| `tone3Freq` | 8000 Hz | brillantezza alta |
| `tone3Q` | 1.0 Q | campana media |
| `tone3Gain` | 0.2 dB | annullata se è stato chiesto un timbro vintage |
| `airOn` | on  | aria solo se richiesta e se non contraddice il vintage |
| `airGain` | 0.3 dB | shelf alta, tenuta bassa: la stanza non trattata ha già fruscio |

## 11. DeEsser 2 — De-esser 2 (post-saturazione)
*Saturazione e boost di presenza rigenerano sibilanti: un solo de-esser all'inizio non basta.*

> modo Wide, dopo la saturazione

| parametro | valore | perché |
|---|---|---|
| `ds2Freq` | 7350 Hz | leggermente più in alto del primo: qui si trattano le armoniche generate |
| `ds2Thresh` | -18.6 dB | tiene conto delle armoniche aggiunte dalla saturazione |
| `ds2Range` | 5.5 dB | rifinitura, non correzione principale |
| `ds2Mode` | wide  | modo wide: rifinisce l'insieme dopo la colorazione |

## 12. Limiter — Limiter
*Tetto di sicurezza, non effetto: se lavora più di 2–3 dB sono sbagliati gli stadi prima.*

> Output Level = limCeiling

| parametro | valore | perché |
|---|---|---|
| `limCeiling` | -1.0 dBFS | 1 dB di margine per i true peak dopo la codifica lossy |
| `limRelease` | 84 ms | rilascio coerente con l'energia richiesta |

## 13. Gain — Output / Mix
*Porta al target di loudness dichiarato; il MIX è compressione parallela di tutta la catena, la via di fuga quando è 'troppo lavorata'.*

> Gain = outGain (il MIX non ha equivalente stock: usa un bus parallelo)

| parametro | valore | perché |
|---|---|---|
| `outGain` | 5.0 dB | stima iniziale per il target di -9.0 LUFS, poi corretta dal meter |
| `mix` | 86 % | un filo di segnale non compresso quando la densità richiesta è alta |

## Mandate (bus aux — bus PARALLELI, mai in serie sulla voce)

### 1/8 puntato — Tape Delay o Stereo Delay (sync on)
*Scelta dal profilo Shiva. Esecuzione ritmica: il puntato riempie tra le parole senza mangiare l'intelligibilità come farebbe piu' riverbero.*

| parametro | valore |
|---|---|
| `sync` | on |
| `division` | 1/8 dotted |
| `feedback` | 24 |
| `hpf` | 350 |
| `lpf` | 4000 |
| `duck_db` | 8.0 |
| `send_db` | -21.2 |

> Manda la voce a un bus aux e imposta il livello di send a `send_db`. Se il bus ha un compressore in sidechain dalla voce, usa `duck_db` come riduzione.

### Doubler largo — Stereo Delay (tempi diversi L/R) o Modulation Delay
*Scelta dal profilo Shiva. Trap italiana: il doppiaggio allarga la voce senza toccare il centro, dove restano la main e la 808.*

| parametro | valore |
|---|---|
| `time_l_ms` | 20 |
| `time_r_ms` | 29 |
| `detune_cents` | 9 |
| `width_pct` | 64 |
| `hpf` | 300 |
| `lpf` | 7000 |
| `duck_db` | 5.0 |
| `send_db` | -14.0 |

> Manda la voce a un bus aux e imposta il livello di send a `send_db`. Se il bus ha un compressore in sidechain dalla voce, usa `duck_db` come riduzione.

### Ambience corta — ChromaVerb (Room)
*Scelta dal profilo Shiva. Voce parlata: un filo di ambienza corta per non farla suonare sotto vuoto, senza toccare l'intelligibilità.*

| parametro | valore |
|---|---|
| `decay_s` | 0.5 |
| `predelay` | 10 |
| `size_pct` | 35 |
| `hpf` | 400 |
| `lpf` | 5000 |
| `width_pct` | 60 |
| `duck_db` | 6.0 |
| `send_db` | -30.0 |

> Manda la voce a un bus aux e imposta il livello di send a `send_db`. Se il bus ha un compressore in sidechain dalla voce, usa `duck_db` come riduzione.

## Produzione — quello che il plugin non fa

La catena tratta il suono. Questi passaggi stanno *fuori* dal plugin e sono quelli che rendono riconoscibile il riferimento.

- **Intonazione / Auto-Tune** — Poco o niente autotune: al massimo una correzione leggera (retune 20–40 ms) per tenere l'intonazione.
- **Doppiaggi** — Doppia solo le battute chiave, panning stretto (60 L/R): la voce deve restare compatta e centrale.
- **Ad-lib** — Ad-lib scuri e bassi, filtrati sotto i 6 kHz, quasi asciutti.
- **Extra** — Niente coda lunga: l'ambiente è corto, la voce deve stare davanti alla 808.

## Avvisi

- Stanza non trattata: le riflessioni precoci non sono correggibili a valle. Avvicinati al microfono e metti qualcosa di morbido dietro di te.
