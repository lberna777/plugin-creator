# Ricetta Logic — "indie lo-fi, voce vintage a nastro, poco brillante"

Generata da `chain_compiler.py` (regole v1.0.0) sul profilo `untreated_room_focusrite_scarlett`.
Solo plugin **stock** di Logic Pro, nella strip della traccia vocale, in quest'ordine.

Target: **-13.0 LUFS**, ceiling **-1.0 dBFS**.

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
| `gateThresh` | -39.5 dB | 6 dB sopra il noise floor stimato, più 0.45 di severità richiesta |
| `gateRange` | 13.2 dB | attenuazione parziale: chiudere del tutto si sente più del rumore |
| `gateAtk` | 2.0 ms | apertura rapida ma non istantanea, per non tagliare l'attacco delle consonanti |
| `gateRel` | 160 ms | rilascio lungo per non troncare le code di parola |
| `gateKeyLo` | 120 Hz | il detector ignora sotto la fondamentale: rumore di traffico e ronzii non aprono il gate |
| `gateKeyHi` | 4000 Hz | il detector ignora sopra la banda di intelligibilità: ventole e fruscio non aprono il gate |

## 3. Channel EQ — High-Pass Filter
*Ripresa ravvicinata su cardioide: effetto prossimità (+5 dB) e plosive vanno tolti PRIMA dei detector dinamici.*

> banda 1 in modo High Pass, slope 24 dB/oct

| parametro | valore | perché |
|---|---|---|
| `hpfFreq` | 100 Hz | compensa la prossimità sul registro mid, alzato/abbassato secondo il corpo richiesto |
| `hpfSlope` | 24 dB/oct | pendenza ripida: senza filtro anti-pop le plosive sono transienti di decine di dB |

## 4. Channel EQ — Room Tamer (notch dinamici)
*I modi della stanza si eccitano solo su certe note: tre notch stretti che agiscono SOLO sopra soglia, per non togliere corpo a tutto il resto.*

> APPROSSIMAZIONE: i notch stock sono statici, non dinamici. Usa metà della profondità indicata.

| parametro | valore | perché |
|---|---|---|
| `room1Freq` | 92 Hz | modo assiale stimato della stanza |
| `room1Q` | 6.0 Q | stretto: correzione, non gusto |
| `room1Depth` | -3.8 dB | attenuazione massima quando la risonanza supera la soglia |
| `room2Freq` | 184 Hz | secondo modo / boxiness bassa |
| `room2Q` | 5.0 Q | stretto |
| `room2Depth` | -4.3 dB | meno profondo se è stato chiesto corpo |
| `room3Freq` | 246 Hz | terzo modo / scatola |
| `room3Q` | 5.0 Q | stretto |
| `room3Depth` | -3.6 dB | attenuazione dinamica sulla scatola |
| `roomThresh` | -27.3 dB | sotto questa soglia i notch non lavorano: la voce resta intera |

## 5. Channel EQ — EQ sottrattiva
*Prima togli, poi aggiungi: ogni dB tolto qui è headroom per i due compressori.*

> bande 2–4, campane

| parametro | valore | perché |
|---|---|---|
| `sub1Freq` | 240 Hz | centro della boxiness della stanza (150–400 Hz) |
| `sub1Q` | 2.0 Q | banda media: è fango, non una risonanza singola |
| `sub1Gain` | -2.9 dB | toglie fango, ma restituisce corpo se il testo lo ha chiesto |
| `sub2Freq` | 1000 Hz | banda nasale del registro mid |
| `sub2Q` | 3.0 Q | stretto: la nasalità è localizzata |
| `sub2Gain` | -2.1 dB | toglie il naso senza spegnere l'intelligibilità |
| `sub3Freq` | 2800 Hz | banda del flutter echo della stanza (1000–4000 Hz) |
| `sub3Q` | 2.5 Q | medio |
| `sub3Gain` | -2.8 dB | asprezza della stanza, più quella tolta su richiesta esplicita |

## 6. DeEsser 2 — De-esser 1 (pre-compressione)
*Protegge il detector dei compressori: senza, ogni 's' innesca una riduzione che abbassa la parola intera.*

> modo Split, prima del compressore

| parametro | valore | perché |
|---|---|---|
| `ds1Freq` | 6650 Hz | centro sibilanza stimato del microfono sul registro mid |
| `ds1Thresh` | -26.7 dB | riferita al livello di lavoro: la banda alta di una 's' arriva a -22.0 dBFS, la soglia sta sotto di quanto serve per togliere qualche dB alle esse e niente al resto |
| `ds1Range` | 3.6 dB | tetto della riduzione sulla sola banda alta: oltre i 4-5 dB la 's' sparisce e la voce blesa |
| `ds1Mode` | split  | modo split: agisce solo sulla banda alta, non abbassa tutta la voce |

## 7. Compressor — Compressore 1 (veloce)
*Nessuno canta fermo davanti al microfono: questo stadio prende i picchi e i cambi di distanza. La soglia è calcolata all'indietro dalla riduzione voluta (3.15 dB al livello di lavoro), non scelta a occhio: così non supera mai i 6 dB di CLAUDE.md.*

> circuito FET (Platinum se preferisci più trasparenza)

| parametro | valore | perché |
|---|---|---|
| `c1Thresh` | -17.1 dB | posta dove, con un picco al livello di lavoro (-12.0 dBFS) e rapporto 2.65:1, la riduzione si ferma a 3.15 dB |
| `c1Ratio` | 2.6 :1 | rapporto moderato, il livellamento vero lo fa il secondo stadio |
| `c1Atk` | 8.0 ms | più aggressivo = attacco più corto = più controllo sui transienti |
| `c1Rel` | 111 ms | rilascio abbastanza lungo da non inseguire le sillabe: è il rilascio corto che fa il suono 'colloso' |
| `c1Knee` | 8.0 dB | ginocchio largo: la riduzione entra gradualmente invece di agganciare di colpo |
| `c1Makeup` | 2.0 dB | recupera il 65% dei 3.15 dB tolti qui: la riduzione è sui picchi, il livello medio ne perde meno, restituire tutto vorrebbe dire alzare |

## 8. Compressor — Compressore 2 (lento, glue)
*Livella la frase e dà densità. Due stadi leggeri pompano meno di uno pesante: qui la riduzione voluta è 2.36 dB, meno del primo stadio, perché i picchi li ha già presi lui.*

> circuito VCA o Opto

| parametro | valore | perché |
|---|---|---|
| `c2Thresh` | -17.6 dB | calcolata sul livello che esce davvero dal primo stadio (-13.1 dBFS), non su un'ipotesi: la riduzione si ferma a 2.36 dB |
| `c2Ratio` | 2.1 :1 | rapporto basso: colla, non controllo |
| `c2Atk` | 34 ms | attacco lento: lascia passare i transienti |
| `c2Rel` | 255 ms | rilascio lungo, segue la frase e non la sillaba |
| `c2Knee` | 8.0 dB | ginocchio morbido: deve essere invisibile |
| `c2Makeup` | 1.5 dB | recupera il 65% dei 2.36 dB tolti qui, con lo stesso criterio del primo stadio |

## 9. Phat FX — Saturazione
*Dopo la dinamica il livello che la attacca è stabile (-13.93 dBFS di picco stimato), quindi la quantità di armoniche è prevedibile. Il preamp della Scarlett è pulito: il carattere lo mette qui.*

> solo la sezione Distortion; tape→Tape saturation, tube→Tube, transistor→Transistor

| parametro | valore | perché |
|---|---|---|
| `satDrive` | 40 % | carattere richiesto (1), ridotto perché il segnale arriva già compresso (densità 0.65) e riferito al livello che lo attacca: il drive non è più un valore assoluto slegato dal gain |
| `satType` | tape  | tipo di armoniche coerente con vintage/aggressività richieste |
| `satTilt` | -0.4 dB | due shelf speculari a 700 Hz: il valore e' meta' dell'inclinazione totale, e vale anche a drive 0. Tenuto basso perche' si somma all'EQ tonale, che la brillantezza la fa gia' lei |

## 10. Channel EQ — EQ tonale
*Qui si aggiunge, su un segnale già pulito e già denso: i boost non riportano su fango o rumore.*

> seconda istanza, bande a campana + shelf alta per airGain

| parametro | valore | perché |
|---|---|---|
| `tone1Freq` | 180 Hz | corpo del registro mid |
| `tone1Q` | 0.8 Q | campana larga: è timbro, non correzione |
| `tone1Gain` | 0.0 dB | corpo richiesto dal testo |
| `tone2Freq` | 3200 Hz | presenza / intelligibilità |
| `tone2Q` | 1.2 Q | campana media |
| `tone2Gain` | -0.4 dB | ridotto perché il microfono ha già un suo picco di presenza |
| `tone3Freq` | 8000 Hz | brillantezza alta |
| `tone3Q` | 1.0 Q | campana media |
| `tone3Gain` | -0.0 dB | annullata se è stato chiesto un timbro vintage |
| `airOn` | off  | aria solo se richiesta e se non contraddice il vintage |
| `airGain` | 0.0 dB | shelf alta, tenuta bassa: la stanza non trattata ha già fruscio |

## 11. DeEsser 2 — De-esser 2 (post-saturazione)
*Saturazione e boost di presenza rigenerano sibilanti: un solo de-esser all'inizio non basta.*

> modo Wide, dopo la saturazione

| parametro | valore | perché |
|---|---|---|
| `ds2Freq` | 7350 Hz | leggermente più in alto del primo: qui si trattano le armoniche generate |
| `ds2Thresh` | -27.8 dB | riferita allo stesso livello di lavoro del primo, ma piu' in alto: qui la banda alta arriva gia' abbassata dal de-esser 1 e rialzata dalla saturazione |
| `ds2Range` | 3.2 dB | rifinitura: circa meta' del primo stadio, sempre sulla sola banda alta |
| `ds2Mode` | split  | modo split: da quando il crossover e' vero, il modo wide abbasserebbe TUTTA la voce a ogni 's' — un pompaggio a banda larga, non una de-essatura |

## 12. Gain — Output / Mix
*Porta al target di loudness dichiarato; il MIX è compressione parallela di tutta la catena, la via di fuga quando è 'troppo lavorata'.*

> Gain = outGain, PRIMA del limiter (il MIX non ha equivalente stock: usa un bus parallelo)

| parametro | valore | perché |
|---|---|---|
| `outGain` | 4.0 dB | quanto manca davvero dal livello stimato in uscita dalla catena (-19.93 dBFS) al target di -13.0 LUFS, con tetto a 4 dB: questo stadio sta PRIMA del limiter, quindi tutto quello che chiede in più se lo mangia il limiter — e un limiter che lavora non è gain staging, è un effetto. Il resto lo fa il meter di loudness |
| `mix` | 84 % | compressione parallela: già da densità media rientra un po' di segnale non lavorato, ed è la valvola di sfogo contro il suono impastato (dry al massimo 22%) |

## 13. Limiter — Limiter
*Tetto di sicurezza, non effetto: se lavora più di 2–3 dB sono sbagliati gli stadi prima.*

> ultimo della catena, dopo output gain e mandate: Output Level = limCeiling

| parametro | valore | perché |
|---|---|---|
| `limCeiling` | -1.0 dBFS | 1 dB di margine per i true peak dopo la codifica lossy |
| `limRelease` | 120 ms | rilascio coerente con l'energia richiesta |

## Mandate (bus aux — bus PARALLELI, mai in serie sulla voce)

### Slapback — Tape Delay (sync off)
*Carattere vintage: una sola ripetizione corta al posto della coda, tiene la voce avanti e asciutta.*

| parametro | valore |
|---|---|
| `sync` | off |
| `time_ms` | 110 |
| `feedback` | 8 |
| `hpf` | 300 |
| `lpf` | 2300 |
| `duck_db` | 6.6 |
| `send_db` | -29.0 |

> Manda la voce a un bus aux e imposta il livello di send a `send_db`. Se il bus ha un compressore in sidechain dalla voce, usa `duck_db` come riduzione.

### Doubler largo — Stereo Delay (tempi diversi L/R) o Modulation Delay
*Trap italiana: il doppiaggio allarga la voce senza toccare il centro, dove restano la main e la 808. A differenza di riverbero e delay non va duckato sotto la parola: allarga MENTRE la voce parla, e duckarlo lo spegne proprio dove serve.*

| parametro | valore |
|---|---|
| `time_l_ms` | 19 |
| `time_r_ms` | 28 |
| `detune_cents` | 12 |
| `width_pct` | 60 |
| `hpf` | 300 |
| `lpf` | 5000 |
| `duck_db` | 3.3 |
| `send_db` | -19.0 |

> Manda la voce a un bus aux e imposta il livello di send a `send_db`. Se il bus ha un compressore in sidechain dalla voce, usa `duck_db` come riduzione.

### Room vintage — ChromaVerb (Room / Chamber)
*Timbro vintage richiesto: una camera corta e scura invece di un riverbero moderno, coerente con il carattere a nastro.*

| parametro | valore |
|---|---|
| `decay_s` | 1.0 |
| `predelay` | 15 |
| `size_pct` | 45 |
| `hpf` | 320 |
| `lpf` | 3000 |
| `width_pct` | 70 |
| `duck_db` | 6.6 |
| `send_db` | -27.0 |

> Manda la voce a un bus aux e imposta il livello di send a `send_db`. Se il bus ha un compressore in sidechain dalla voce, usa `duck_db` come riduzione.

## Avvisi

- Stanza non trattata: le riflessioni precoci non sono correggibili a valle. Avvicinati al microfono e metti qualcosa di morbido dietro di te.
