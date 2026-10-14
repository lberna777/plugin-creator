# Ricetta Logic — "gue pequeno"

**Mock-up del suono di Gue.** Rap classico: voce baritonale, asciutta e autorevole, carattere analogico, niente effetti vistosi.

> Non è la catena reale dell'artista — nessuno la conosce fuori dal suo studio. È una ricostruzione del *risultato* che si sente sui dischi, partendo dal tuo segnale: stanza non trattata, microfono Focusrite, Scarlett a due ingressi.

Generata da `chain_compiler.py` (regole v1.0.0) sul profilo `untreated_room_focusrite_scarlett`.
Solo plugin **stock** di Logic Pro, nella strip della traccia vocale, in quest'ordine.

Target: **-10.0 LUFS**, ceiling **-1.0 dBFS**.

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
| `gateThresh` | -36.0 dB | 6 dB sopra il noise floor stimato, più 0.8 di severità richiesta |
| `gateRange` | 18.8 dB | attenuazione parziale: chiudere del tutto si sente più del rumore |
| `gateAtk` | 1.05 ms | il gate parte chiuso, quindi questo tempo entra due volte: e' la costante del detector E la rampa con cui il guadagno risale dal fondo del range fino a zero. Misurato sul DSP, la prima parola di una take esce piena dopo circa tre volte questo valore: a 2 ms perdeva 4,9 dB sui primi 10 ms con la sorgente 12 dB sotto il livello dichiarato, a 1,2 ne perde 3 |
| `gateRel` | 145 ms | rilascio lungo per non troncare le code di parola |
| `gateKeyLo` | 102 Hz | il detector ignora sotto la fondamentale: rumore di traffico e ronzii non aprono il gate |
| `gateKeyHi` | 3400 Hz | il detector ignora sopra la banda di intelligibilità: ventole e fruscio non aprono il gate |

## 3. Channel EQ — High-Pass Filter
*Ripresa ravvicinata su cardioide: effetto prossimità (+5 dB) e plosive vanno tolti PRIMA dei detector dinamici.*

> banda 1 in modo High Pass, slope 24 dB/oct

| parametro | valore | perché |
|---|---|---|
| `hpfFreq` | 80 Hz | compensa la prossimità sul registro low, alzato/abbassato secondo il corpo richiesto |
| `hpfSlope` | 24 dB/oct | pendenza ripida: senza filtro anti-pop le plosive sono transienti di decine di dB |

## 4. Channel EQ — Room Tamer (notch dinamici)
*I modi della stanza si eccitano solo su certe note: tre notch stretti che agiscono SOLO sopra soglia, per non togliere corpo a tutto il resto.*

> APPROSSIMAZIONE: i notch stock sono statici, non dinamici. Usa metà della profondità indicata.

| parametro | valore | perché |
|---|---|---|
| `room1Freq` | 92 Hz | modo assiale stimato della stanza |
| `room1Q` | 6.0 Q | stretto: correzione, non gusto |
| `room1Depth` | -5.2 dB | attenuazione massima quando la risonanza supera la soglia |
| `room2Freq` | 184 Hz | secondo modo / boxiness bassa |
| `room2Q` | 5.0 Q | stretto |
| `room2Depth` | -5.2 dB | meno profondo se è stato chiesto corpo |
| `room3Freq` | 246 Hz | terzo modo / scatola |
| `room3Q` | 5.0 Q | stretto |
| `room3Depth` | -4.4 dB | attenuazione dinamica sulla scatola |
| `roomThresh` | -34.8 dB | soglia riferita al livello a cui un modo di stanza arriva davvero al detector (12 dB sotto il picco di lavoro), non un numero assoluto in dBFS: sotto di lei i notch stanno fermi e la voce resta intera, sopra scavano quanto la risonanza eccede |

## 5. Channel EQ — EQ sottrattiva
*Prima togli, poi aggiungi: ogni dB tolto qui è headroom per i due compressori.*

> bande 2–4, campane

| parametro | valore | perché |
|---|---|---|
| `sub1Freq` | 204 Hz | centro della boxiness della stanza (150–400 Hz) |
| `sub1Q` | 2.0 Q | banda media: è fango, non una risonanza singola |
| `sub1Gain` | -3.3 dB | toglie fango, ma restituisce corpo se il testo lo ha chiesto |
| `sub2Freq` | 850 Hz | banda nasale del registro low |
| `sub2Q` | 3.0 Q | stretto: la nasalità è localizzata |
| `sub2Gain` | -3.0 dB | toglie il naso senza spegnere l'intelligibilità |
| `sub3Freq` | 2380 Hz | banda del flutter echo della stanza (1000–4000 Hz) |
| `sub3Q` | 2.5 Q | medio |
| `sub3Gain` | -2.0 dB | asprezza della stanza, più quella tolta su richiesta esplicita |

## 6. DeEsser 2 — De-esser 1 (pre-compressione)
*Protegge il detector dei compressori: senza, ogni 's' innesca una riduzione che abbassa la parola intera.*

> modo Split, prima del compressore

| parametro | valore | perché |
|---|---|---|
| `ds1Freq` | 5652 Hz | centro sibilanza stimato del microfono sul registro low |
| `ds1Thresh` | -26.7 dB | riferita al livello di lavoro: la banda alta di una 's' arriva a -22.0 dBFS, la soglia sta sotto di quanto serve per togliere qualche dB alle esse e niente al resto |
| `ds1Range` | 3.6 dB | tetto della riduzione sulla sola banda alta: oltre i 4-5 dB la 's' sparisce e la voce blesa |
| `ds1Mode` | split  | modo split: agisce solo sulla banda alta, non abbassa tutta la voce |

## 7. Compressor — Compressore 1 (veloce)
*Nessuno canta fermo davanti al microfono: questo stadio prende i picchi e i cambi di distanza. La soglia è calcolata all'indietro dalla riduzione voluta (4.12 dB al livello di lavoro), non scelta a occhio: così non supera mai i 6 dB di CLAUDE.md.*

> circuito FET (Platinum se preferisci più trasparenza)

| parametro | valore | perché |
|---|---|---|
| `c1Thresh` | -18.0 dB | posta dove, con un picco al livello di lavoro (-12.0 dBFS) e rapporto 3.23:1, la riduzione si ferma a 4.12 dB |
| `c1Ratio` | 3.2 :1 | rapporto moderato, il livellamento vero lo fa il secondo stadio |
| `c1Atk` | 6.5 ms | più aggressivo = attacco più corto = più controllo sui transienti |
| `c1Rel` | 92 ms | rilascio abbastanza lungo da non inseguire le sillabe: è il rilascio corto che fa il suono 'colloso' |
| `c1Knee` | 7.0 dB | ginocchio largo: la riduzione entra gradualmente invece di agganciare di colpo |
| `c1Makeup` | 2.7 dB | recupera il 65% dei 4.12 dB tolti qui: la riduzione è sui picchi, il livello medio ne perde meno, restituire tutto vorrebbe dire alzare |

## 8. Compressor — Compressore 2 (lento, glue)
*Livella la frase e dà densità. Due stadi leggeri pompano meno di uno pesante: qui la riduzione voluta è 2.84 dB, meno del primo stadio, perché i picchi li ha già presi lui.*

> circuito VCA o Opto

| parametro | valore | perché |
|---|---|---|
| `c2Thresh` | -18.4 dB | calcolata sul livello che esce davvero dal primo stadio (-13.44 dBFS), non su un'ipotesi: la riduzione si ferma a 2.84 dB |
| `c2Ratio` | 2.3 :1 | rapporto basso: colla, non controllo |
| `c2Atk` | 29 ms | attacco lento: lascia passare i transienti |
| `c2Rel` | 235 ms | rilascio lungo, segue la frase e non la sillaba |
| `c2Knee` | 8.0 dB | ginocchio morbido: deve essere invisibile |
| `c2Makeup` | 1.8 dB | recupera il 65% dei 2.84 dB tolti qui, con lo stesso criterio del primo stadio |

## 9. Phat FX — Saturazione
*Dopo la dinamica il livello che la attacca è stabile (-14.44 dBFS di picco stimato), quindi la quantità di armoniche è prevedibile. Il preamp della Scarlett è pulito: il carattere lo mette qui.*

> solo la sezione Distortion; tape→Tape saturation, tube→Tube, transistor→Transistor

| parametro | valore | perché |
|---|---|---|
| `satDrive` | 25 % | carattere richiesto (0.55), ridotto perché il segnale arriva già compresso (densità 0.85) e riferito al livello che lo attacca: il drive non è più un valore assoluto slegato dal gain |
| `satType` | tube  | tipo di armoniche coerente con vintage/aggressività richieste |
| `satTilt` | 0.2 dB | due shelf speculari a 700 Hz: il valore e' meta' dell'inclinazione totale, e vale anche a drive 0. Tenuto basso perche' si somma all'EQ tonale, che la brillantezza la fa gia' lei |

## 10. Channel EQ — EQ tonale
*Qui si aggiunge, su un segnale già pulito e già denso: i boost non riportano su fango o rumore.*

> seconda istanza, bande a campana + shelf alta per airGain

| parametro | valore | perché |
|---|---|---|
| `tone1Freq` | 153 Hz | corpo del registro low |
| `tone1Q` | 0.8 Q | campana larga: è timbro, non correzione |
| `tone1Gain` | 0.8 dB | corpo richiesto dal testo |
| `tone2Freq` | 2720 Hz | presenza / intelligibilità |
| `tone2Q` | 1.2 Q | campana media |
| `tone2Gain` | 0.2 dB | ridotto perché il microfono ha già un suo picco di presenza |
| `tone3Freq` | 6800 Hz | brillantezza alta |
| `tone3Q` | 1.0 Q | campana media |
| `tone3Gain` | 0.1 dB | annullata se è stato chiesto un timbro vintage |
| `airOn` | on  | aria solo se richiesta e se non contraddice il vintage |
| `airGain` | 0.2 dB | shelf alta, tenuta bassa: la stanza non trattata ha già fruscio |

## 11. DeEsser 2 — De-esser 2 (post-saturazione)
*Saturazione e boost di presenza rigenerano sibilanti: un solo de-esser all'inizio non basta.*

> modo Wide, dopo la saturazione

| parametro | valore | perché |
|---|---|---|
| `ds2Freq` | 6248 Hz | leggermente più in alto del primo: qui si trattano le armoniche generate |
| `ds2Thresh` | -26.4 dB | riferita allo stesso livello di lavoro del primo, ma piu' in alto: qui la banda alta arriva gia' abbassata dal de-esser 1 e rialzata dalla saturazione |
| `ds2Range` | 2.7 dB | rifinitura: circa meta' del primo stadio, sempre sulla sola banda alta |
| `ds2Mode` | split  | modo split: da quando il crossover e' vero, il modo wide abbasserebbe TUTTA la voce a ogni 's' — un pompaggio a banda larga, non una de-essatura |

## 12. Gain — Output / Mix
*Porta al target di loudness dichiarato; il MIX è compressione parallela di tutta la catena, la via di fuga quando è 'troppo lavorata'.*

> Gain = outGain, PRIMA del limiter (il MIX non ha equivalente stock: usa un bus parallelo)

| parametro | valore | perché |
|---|---|---|
| `outGain` | 4.0 dB | quanto manca davvero dal livello stimato in uscita dalla catena (-20.44 dBFS) al target di -10.0 LUFS, con tetto a 4 dB: questo stadio sta PRIMA del limiter, quindi tutto quello che chiede in più se lo mangia il limiter — e un limiter che lavora non è gain staging, è un effetto. Il resto lo fa il meter di loudness |
| `mix` | 78 % | compressione parallela: già da densità media rientra un po' di segnale non lavorato, ed è la valvola di sfogo contro il suono impastato (dry al massimo 22%) |

## 13. Limiter — Limiter
*Tetto di sicurezza, non effetto: se lavora più di 2–3 dB sono sbagliati gli stadi prima.*

> ultimo della catena, dopo output gain e mandate: Output Level = limCeiling

| parametro | valore | perché |
|---|---|---|
| `limCeiling` | -1.0 dBFS | 1 dB di margine per i true peak dopo la codifica lossy |
| `limRelease` | 105 ms | rilascio coerente con l'energia richiesta |

## Mandate (bus aux — bus PARALLELI, mai in serie sulla voce)

### Slapback — Tape Delay (sync off)
*Scelta dal profilo Gue. Carattere vintage: una sola ripetizione corta al posto della coda, tiene la voce avanti e asciutta.*

| parametro | valore |
|---|---|
| `sync` | off |
| `time_ms` | 105 |
| `feedback` | 10 |
| `hpf` | 255 |
| `lpf` | 3080 |
| `duck_db` | 7.4 |
| `send_db` | -27.5 |

> Manda la voce a un bus aux e imposta il livello di send a `send_db`. Se il bus ha un compressore in sidechain dalla voce, usa `duck_db` come riduzione.

### Room vintage — ChromaVerb (Room / Chamber)
*Scelta dal profilo Gue. Timbro vintage richiesto: una camera corta e scura invece di un riverbero moderno, coerente con il carattere a nastro.*

| parametro | valore |
|---|---|
| `decay_s` | 1.0 |
| `predelay` | 15 |
| `size_pct` | 45 |
| `hpf` | 272 |
| `lpf` | 3975 |
| `width_pct` | 70 |
| `duck_db` | 7.4 |
| `send_db` | -27.0 |

> Manda la voce a un bus aux e imposta il livello di send a `send_db`. Se il bus ha un compressore in sidechain dalla voce, usa `duck_db` come riduzione.

## Produzione — quello che il plugin non fa

La catena tratta il suono. Questi passaggi stanno *fuori* dal plugin e sono quelli che rendono riconoscibile il riferimento.

- **Intonazione / Auto-Tune** — Niente autotune. La voce è parlata-rappata: l'intonazione non è il punto.
- **Doppiaggi** — Doppia solo le rime finali, centrata, a −8 dB. Nessun allargamento.
- **Ad-lib** — Ad-lib asciutti e centrati, stesso trattamento della main ma 6 dB sotto.
- **Extra** — Il carattere sta nella saturazione a valvola e nel corpo sui 180 Hz, non nell'aria.

## Avvisi

- Stanza non trattata: le riflessioni precoci non sono correggibili a valle. Avvicinati al microfono e metti qualcosa di morbido dietro di te.
