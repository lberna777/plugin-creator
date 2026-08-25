# Ricetta Logic — "glockyy"

**Mock-up del suono di Glockyy.** Drill scura e ruvida: registro basso, saturazione evidente, ambiente quasi assente.

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
| `gateThresh` | -34.5 dB | 6 dB sopra il noise floor stimato, più 0.95 di severità richiesta |
| `gateRange` | 21.2 dB | attenuazione parziale: chiudere del tutto si sente più del rumore |
| `gateAtk` | 1.02 ms | apertura rapida ma non istantanea, per non tagliare l'attacco delle consonanti |
| `gateRel` | 121 ms | rilascio lungo per non troncare le code di parola |
| `gateKeyLo` | 102 Hz | il detector ignora sotto la fondamentale: rumore di traffico e ronzii non aprono il gate |
| `gateKeyHi` | 3400 Hz | il detector ignora sopra la banda di intelligibilità: ventole e fruscio non aprono il gate |

## 3. Channel EQ — High-Pass Filter
*Ripresa ravvicinata su cardioide: effetto prossimità (+5 dB) e plosive vanno tolti PRIMA dei detector dinamici.*

> banda 1 in modo High Pass, slope 24 dB/oct

| parametro | valore | perché |
|---|---|---|
| `hpfFreq` | 82 Hz | compensa la prossimità sul registro low, alzato/abbassato secondo il corpo richiesto |
| `hpfSlope` | 24 dB/oct | pendenza ripida: senza filtro anti-pop le plosive sono transienti di decine di dB |

## 4. Channel EQ — Room Tamer (notch dinamici)
*I modi della stanza si eccitano solo su certe note: tre notch stretti che agiscono SOLO sopra soglia, per non togliere corpo a tutto il resto.*

> APPROSSIMAZIONE: i notch stock sono statici, non dinamici. Usa metà della profondità indicata.

| parametro | valore | perché |
|---|---|---|
| `room1Freq` | 92 Hz | modo assiale stimato della stanza |
| `room1Q` | 6.0 Q | stretto: correzione, non gusto |
| `room1Depth` | -5.8 dB | attenuazione massima quando la risonanza supera la soglia |
| `room2Freq` | 184 Hz | secondo modo / boxiness bassa |
| `room2Q` | 5.0 Q | stretto |
| `room2Depth` | -6.0 dB | meno profondo se è stato chiesto corpo |
| `room3Freq` | 246 Hz | terzo modo / scatola |
| `room3Q` | 5.0 Q | stretto |
| `room3Depth` | -5.0 dB | attenuazione dinamica sulla scatola |
| `roomThresh` | -24.3 dB | sotto questa soglia i notch non lavorano: la voce resta intera |

## 5. Channel EQ — EQ sottrattiva
*Prima togli, poi aggiungi: ogni dB tolto qui è headroom per i due compressori.*

> bande 2–4, campane

| parametro | valore | perché |
|---|---|---|
| `sub1Freq` | 204 Hz | centro della boxiness della stanza (150–400 Hz) |
| `sub1Q` | 2.0 Q | banda media: è fango, non una risonanza singola |
| `sub1Gain` | -3.9 dB | toglie fango, ma restituisce corpo se il testo lo ha chiesto |
| `sub2Freq` | 850 Hz | banda nasale del registro low |
| `sub2Q` | 3.0 Q | stretto: la nasalità è localizzata |
| `sub2Gain` | -3.4 dB | toglie il naso senza spegnere l'intelligibilità |
| `sub3Freq` | 2380 Hz | banda del flutter echo della stanza (1000–4000 Hz) |
| `sub3Q` | 2.5 Q | medio |
| `sub3Gain` | -2.1 dB | asprezza della stanza, più quella tolta su richiesta esplicita |

## 6. DeEsser 2 — De-esser 1 (pre-compressione)
*Protegge il detector dei compressori: senza, ogni 's' innesca una riduzione che abbassa la parola intera.*

> modo Split, prima del compressore

| parametro | valore | perché |
|---|---|---|
| `ds1Freq` | 5652 Hz | centro sibilanza stimato del microfono sul registro low |
| `ds1Thresh` | -23.0 dB | quanto in basso agganciare le esse |
| `ds1Range` | 6.8 dB | attenuazione contenuta: il grosso lo fa il secondo de-esser |
| `ds1Mode` | split  | modo split: agisce solo sulla banda alta, non abbassa tutta la voce |

## 7. Compressor — Compressore 1 (veloce)
*Nessuno canta fermo davanti al microfono: questo stadio prende i picchi e i cambi di distanza. La soglia è calcolata all'indietro dalla riduzione voluta (5.0 dB al livello di lavoro), non scelta a occhio: così non supera mai i 6 dB di CLAUDE.md.*

> circuito FET (Platinum se preferisci più trasparenza)

| parametro | valore | perché |
|---|---|---|
| `c1Thresh` | -18.7 dB | posta dove, con un picco al livello di lavoro (-12.0 dBFS) e rapporto 3.98:1, la riduzione si ferma a 5.0 dB |
| `c1Ratio` | 4.0 :1 | rapporto moderato, il livellamento vero lo fa il secondo stadio |
| `c1Atk` | 4.1 ms | più aggressivo = attacco più corto = più controllo sui transienti |
| `c1Rel` | 70 ms | rilascio abbastanza lungo da non inseguire le sillabe: è il rilascio corto che fa il suono 'colloso' |
| `c1Knee` | 5.4 dB | ginocchio largo: la riduzione entra gradualmente invece di agganciare di colpo |
| `c1Makeup` | 3.2 dB | recupera il 65% dei 5.0 dB tolti qui: la riduzione è sui picchi, il livello medio ne perde meno, restituire tutto vorrebbe dire alzare |

## 8. Compressor — Compressore 2 (lento, glue)
*Livella la frase e dà densità. Due stadi leggeri pompano meno di uno pesante: qui la riduzione voluta è 3.2 dB, meno del primo stadio, perché i picchi li ha già presi lui.*

> circuito VCA o Opto

| parametro | valore | perché |
|---|---|---|
| `c2Thresh` | -19.1 dB | calcolata sul livello che esce davvero dal primo stadio (-13.75 dBFS), non su un'ipotesi: la riduzione si ferma a 3.2 dB |
| `c2Ratio` | 2.5 :1 | rapporto basso: colla, non controllo |
| `c2Atk` | 25 ms | attacco lento: lascia passare i transienti |
| `c2Rel` | 220 ms | rilascio lungo, segue la frase e non la sillaba |
| `c2Knee` | 8.0 dB | ginocchio morbido: deve essere invisibile |
| `c2Makeup` | 2.1 dB | recupera il 65% dei 3.2 dB tolti qui, con lo stesso criterio del primo stadio |

## 9. Phat FX — Saturazione
*Dopo la dinamica il livello che la attacca è stabile (-14.87 dBFS di picco stimato), quindi la quantità di armoniche è prevedibile. Il preamp della Scarlett è pulito: il carattere lo mette qui.*

> solo la sezione Distortion; tape→Tape saturation, tube→Tube, transistor→Transistor

| parametro | valore | perché |
|---|---|---|
| `satDrive` | 24 % | carattere richiesto (0.6), ridotto perché il segnale arriva già compresso (densità 1.0) e riferito al livello che lo attacca: il drive non è più un valore assoluto slegato dal gain |
| `satType` | transistor  | tipo di armoniche coerente con vintage/aggressività richieste |
| `satTilt` | -0.2 dB | inclina lo spettro delle armoniche verso il timbro richiesto |

## 10. Channel EQ — EQ tonale
*Qui si aggiunge, su un segnale già pulito e già denso: i boost non riportano su fango o rumore.*

> seconda istanza, bande a campana + shelf alta per airGain

| parametro | valore | perché |
|---|---|---|
| `tone1Freq` | 153 Hz | corpo del registro low |
| `tone1Q` | 0.8 Q | campana larga: è timbro, non correzione |
| `tone1Gain` | 0.5 dB | corpo richiesto dal testo |
| `tone2Freq` | 2720 Hz | presenza / intelligibilità |
| `tone2Q` | 1.2 Q | campana media |
| `tone2Gain` | -0.1 dB | ridotto perché il microfono ha già un suo picco di presenza |
| `tone3Freq` | 6800 Hz | brillantezza alta |
| `tone3Q` | 1.0 Q | campana media |
| `tone3Gain` | -0.1 dB | annullata se è stato chiesto un timbro vintage |
| `airOn` | off  | aria solo se richiesta e se non contraddice il vintage |
| `airGain` | 0.0 dB | shelf alta, tenuta bassa: la stanza non trattata ha già fruscio |

## 11. DeEsser 2 — De-esser 2 (post-saturazione)
*Saturazione e boost di presenza rigenerano sibilanti: un solo de-esser all'inizio non basta.*

> modo Wide, dopo la saturazione

| parametro | valore | perché |
|---|---|---|
| `ds2Freq` | 6248 Hz | leggermente più in alto del primo: qui si trattano le armoniche generate |
| `ds2Thresh` | -18.4 dB | tiene conto delle armoniche aggiunte dalla saturazione |
| `ds2Range` | 5.5 dB | rifinitura, non correzione principale |
| `ds2Mode` | wide  | modo wide: rifinisce l'insieme dopo la colorazione |

## 12. Limiter — Limiter
*Tetto di sicurezza, non effetto: se lavora più di 2–3 dB sono sbagliati gli stadi prima.*

> Output Level = limCeiling

| parametro | valore | perché |
|---|---|---|
| `limCeiling` | -1.0 dBFS | 1 dB di margine per i true peak dopo la codifica lossy |
| `limRelease` | 81 ms | rilascio coerente con l'energia richiesta |

## 13. Gain — Output / Mix
*Porta al target di loudness dichiarato; il MIX è compressione parallela di tutta la catena, la via di fuga quando è 'troppo lavorata'.*

> Gain = outGain (il MIX non ha equivalente stock: usa un bus parallelo)

| parametro | valore | perché |
|---|---|---|
| `outGain` | 4.0 dB | quanto manca davvero dal livello stimato in uscita dalla catena (-20.87 dBFS) al target di -9.0 LUFS, con tetto a 4 dB: questo stadio sta DOPO il limiter, quindi oltre non è più gain staging ma solo alzare il fader — e il tetto di true peak non reggerebbe. Il resto lo fa il meter di loudness |
| `mix` | 78 % | compressione parallela: già da densità media rientra un po' di segnale non lavorato, ed è la valvola di sfogo contro il suono impastato (dry al massimo 22%) |

## Mandate (bus aux — bus PARALLELI, mai in serie sulla voce)

### Slapback — Tape Delay (sync off)
*Scelta dal profilo Glockyy. Carattere vintage: una sola ripetizione corta al posto della coda, tiene la voce avanti e asciutta.*

| parametro | valore |
|---|---|
| `sync` | off |
| `time_ms` | 97 |
| `feedback` | 13 |
| `hpf` | 255 |
| `lpf` | 3500 |
| `duck_db` | 8.0 |
| `send_db` | -25.1 |

> Manda la voce a un bus aux e imposta il livello di send a `send_db`. Se il bus ha un compressore in sidechain dalla voce, usa `duck_db` come riduzione.

### Doubler largo — Stereo Delay (tempi diversi L/R) o Modulation Delay
*Scelta dal profilo Glockyy. Trap italiana: il doppiaggio allarga la voce senza toccare il centro, dove restano la main e la 808.*

| parametro | valore |
|---|---|
| `time_l_ms` | 19 |
| `time_r_ms` | 28 |
| `detune_cents` | 10 |
| `width_pct` | 60 |
| `hpf` | 255 |
| `lpf` | 6900 |
| `duck_db` | 8.0 |
| `send_db` | -19.0 |

> Manda la voce a un bus aux e imposta il livello di send a `send_db`. Se il bus ha un compressore in sidechain dalla voce, usa `duck_db` come riduzione.

### Ambience corta — ChromaVerb (Room)
*Scelta dal profilo Glockyy. Voce parlata: un filo di ambienza corta per non farla suonare sotto vuoto, senza toccare l'intelligibilità.*

| parametro | valore |
|---|---|
| `decay_s` | 0.5 |
| `predelay` | 10 |
| `size_pct` | 35 |
| `hpf` | 340 |
| `lpf` | 5000 |
| `width_pct` | 60 |
| `duck_db` | 8.0 |
| `send_db` | -32.0 |

> Manda la voce a un bus aux e imposta il livello di send a `send_db`. Se il bus ha un compressore in sidechain dalla voce, usa `duck_db` come riduzione.

## Produzione — quello che il plugin non fa

La catena tratta il suono. Questi passaggi stanno *fuori* dal plugin e sono quelli che rendono riconoscibile il riferimento.

- **Intonazione / Auto-Tune** — Nessun autotune percepibile. Se serve, correzione trasparente (retune 40 ms).
- **Doppiaggi** — Doppia in punch (solo le ultime parole della barra), stretto e forte.
- **Ad-lib** — Ad-lib gravi, saturati, con slapback corto invece del delay ritmico.
- **Extra** — La ruvidità è saturazione, non distorsione: se sfrigola, il drive è troppo.

## Avvisi

- Stanza non trattata: le riflessioni precoci non sono correggibili a valle. Avvicinati al microfono e metti qualcosa di morbido dietro di te.
