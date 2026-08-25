# Taratura — dal "colloso e saturato" al "preciso e pulito"

Segnalazione dell'utente: *«le catene sono troppo collose e saturate a palla, e lo restano anche
quando il gain d'ingresso è basso; le voglio più precise e più pulite, ma con i riferimenti artista
ancora riconoscibili»*.

Questo file dice **cosa era sbagliato nella taratura**, **cosa è cambiato** e **i numeri prima/dopo**.
La misura si rifà con:

```
python3 tools/tests/test_gain_staging.py            # tabella prima/dopo sul preset
python3 -m unittest discover -s tools/tests         # gli stessi numeri come invarianti
./build/vocalforge_selftest_artefacts/Release/vocalforge_selftest   # riduzione misurata sul DSP vero
```

Nessuna riga di DSP è cambiata, nessun modulo è stato aggiunto o spostato: è cambiata solo
`tools/data/rules.json`, più il supporto alle grandezze derivate (simmetrico Python ↔ C++).

---

## 1. Cosa era rotto

### a) I makeup non recuperavano niente: erano un guadagno fisso legato alla densità
`c1Makeup = 2 + 3·density` e `c2Makeup = 1.5 + 2·density` non sapevano nulla della riduzione che i
loro compressori stavano facendo. Più densità chiedevi, più alzavano — **anche quando il compressore
non riduceva un solo dB**. Con `outGain = loudness_target + 14` sopra, i profili trap arrivavano a
**13.5 dB di guadagno statico** dentro la catena. È esattamente la sensazione di "colloso": la
dinamica non viene domata, viene *riempita*.

Ed è anche la spiegazione del **caso a gain basso**, quello che l'utente lamentava di più: con un
ingresso a −18 dBFS i compressori non agganciano niente (riduzione 0.0 dB), ma i makeup alzano lo
stesso di 8.5 dB e il drive di saturazione resta al 42–48% perché è un numero assoluto. Misurato sul
DSP vero: **12 dB di gain in meno in ingresso producevano solo 2.6 dB di uscita in meno.**

### b) La doppia compressione superava il limite di CLAUDE.md
"Nessuno stadio da solo può superare 6 dB" era scritto nel documento ma **non era vero sui profili
artista**: soglie e rapporti erano formule indipendenti, e la riduzione che ne usciva non la
controllava nessuno. Al livello di lavoro dichiarato (picchi a −12 dBFS) risultava:

| profilo | comp 1 | comp 2 | totale |
|---|---|---|---|
| Shiva | **8.19 dB** | 5.88 dB | 14.1 dB |
| Glockyy | **8.39 dB** | 5.74 dB | 14.1 dB |
| Capo Plaza | **7.37 dB** | 6.42 dB | 13.8 dB |
| "metal, voce urlata" | **9.82 dB** | 4.79 dB | 14.6 dB |

Il vincolo passava sui prompt neutri e falliva proprio dove serviva.

### c) La saturazione era slegata dal livello
`satDrive = 15 + 55·character` non guardava né quanto il segnale fosse già compresso né a che livello
arrivasse. Sui profili artista il drive stava al 40–48%, e su "indie lo-fi" al 70% — con il segnale
**sopra** il ginocchio della tanh (`sat_excess = +1.5 dB`): distorsione vera, non colore.

### d) La compressione parallela non era una valvola di sfogo
`mix` scendeva sotto il 100% solo oltre densità 0.7 e toglieva al massimo il 14%. Sulla maggior parte
dei preset non entrava mai in gioco.

### e) Le mandate impastavano
Il doubler stava a **−14 dB** — 8÷10 dB sopra riverbero e delay — con il ducking più basso di tutti
(2 + 3·density). Tre bus paralleli poco duckati sopra una voce già densa.

---

## 2. Cosa è cambiato

### Grandezze derivate: soglia, rapporto e makeup parlano della stessa riduzione
Nuovo blocco `derived` in `rules.json`, valutato prima della catena e disponibile come variabili nelle
espressioni (`work_peak`, `gr1_target`, `c1_makeup`, `c2_in`, `sat_in`, `sat_excess`,
`chain_out_rms`, …). Nessun numero è entrato nel codice: le espressioni stanno nel JSON, il supporto
è di sette righe in `tools/chain_compiler.py` e undici in `plugin/source/RulesEngine.cpp`.

Con questo la logica si rovescia. Prima si sceglieva una soglia e si sperava; adesso si dichiara la
**riduzione voluta** e la soglia si calcola all'indietro:

```
gr1_target = clamp(1.2 + 3.0·density + 1.5·pos(aggression), 0, 5.0)     ← tetto 5.0, sotto i 6 di CLAUDE.md
c1Thresh   = work_peak − gr1_target / (1 − 1/c1_ratio)
c1Makeup   = 0.65 · gr1_target                                           ← recupera quello che ha tolto, non di più
```

Il fattore 0.65 non è cosmetico: la riduzione è misurata sui **picchi**, il livello medio ne perde di
meno, quindi restituire il 100% vorrebbe dire *alzare*. Lo stadio lento fa lo stesso su
`gr2_target = clamp(0.8 + 2.4·density, 0, 3.5)`, e prende come riferimento il livello che esce
davvero dal primo stadio (`c2_in`), non una nuova ipotesi.

### Saturazione riferita al livello di lavoro
```
satDrive = clamp((12 + 38·character)·(1 − 0.3·density) − 2.5·pos(sat_excess), 0, 55)
```
Tre cambiamenti in una riga: il carattere resta il driver principale (i profili restano
distinguibili), il termine `(1 − 0.3·density)` toglie drive quando il segnale arriva **già** denso, e
`sat_excess` — di quanto la catena ha alzato il segnale rispetto al livello di lavoro — chiude
l'anello che mancava fra saturazione e livello.

### Output: quanto manca davvero, con un tetto onesto
```
outGain = clamp(loudness_target − chain_out_rms, −24, 4)
```
Non più `loudness_target + 14` a occhio, ma la differenza fra il target dichiarato e il livello
stimato in uscita dalla catena. Il tetto a 4 dB c'è perché questo stadio sta **dopo** il limiter:
oltre non è gain staging, è alzare il fader, e il tetto di true peak non reggerebbe. Il resto lo fa
il meter di loudness, e il `why` del parametro adesso lo dice.

### Compressione parallela come valvola vera
`mix = clamp(100 − 45·pos(density − 0.3), 78, 100)` — entra già da densità media invece che da 0.7, e
arriva a un massimo del 22% di segnale non lavorato (prima 14%, e quasi mai).

### Respiro e mandate
`c1Rel` 80→90 ms di base e `c2Rel` 300→320 ms: è il rilascio corto che insegue le sillabe e fa il
suono colloso. `c1Knee` 6→8 dB: la riduzione entra gradualmente invece di agganciare di colpo.
Tutte le mandate scendono di 3 dB, il doubler di 5 (−14 → −19 dB), e ogni gruppo guadagna 1÷2 dB di
ducking.

---

## 3. I numeri, per profilo artista

Misura di `tools/tests/test_gain_staging.py`, livello d'ingresso **nominale** (picchi a −6 dBFS,
portati a −12 dBFS dall'input trim). Formato `prima → dopo`.

| profilo | makeup tot | outGain | gain statico | riduz. comp 1 | riduz. comp 2 | drive sat | sat vs. ginocchio | headroom pre-limiter | mix |
|---|---|---|---|---|---|---|---|---|---|
| Sfera Ebbasta | 8.5 → **5.0** | 5.0 → 4.0 | 13.5 → **9.0** | 6.54 → **4.53** | 6.97 → **3.16** | 42 → **22** | −4.2 → **−5.9** | 13.8 → 11.8 | 86 → 78 |
| Shiva | 8.5 → **5.3** | 5.0 → 4.0 | 13.5 → **9.3** | 8.19 → **4.98** | 5.88 → **3.19** | 45 → **23** | −4.3 → **−5.8** | 15.2 → 13.0 | 86 → 78 |
| Tony Boy | 8.0 → **4.5** | 4.0 → 4.0 | 12.0 → **8.5** | 5.49 → **4.00** | 6.95 → **2.97** | 40 → **21** | −4.0 → **−5.9** | 13.7 → 12.1 | 91 → 78 |
| Glockyy | 8.5 → **5.3** | 5.0 → 4.0 | 13.5 → **9.3** | 8.39 → **5.02** | 5.74 → **3.17** | 48 → **24** | −3.9 → **−5.6** | 15.2 → 12.9 | 86 → 78 |
| Gue | 7.7 → **4.5** | 4.0 → 4.0 | 11.7 → **8.5** | 5.75 → **4.12** | 6.33 → **2.81** | 45 → **25** | −3.1 → **−4.9** | 13.7 → 12.1 | 93 → 78 |
| Capo Plaza | 8.5 → **5.2** | 5.0 → 4.0 | 13.5 → **9.2** | 7.37 → **4.77** | 6.42 → **3.20** | 42 → **22** | −4.5 → **−6.0** | 14.4 → 12.3 | 86 → 78 |

Riduzione totale di catena (comp 1 + comp 2): da **12.1÷14.1 dB** a **6.9÷8.2 dB**.
Nessuno stadio supera più i 6 dB, su nessun profilo, a nessuno dei due livelli d'ingresso.
Il limiter non lavora (0.00 dB) in tutti i casi misurati: è tornato a essere un tetto.

### Il caso "gain d'ingresso basso" (picchi a −18 dBFS)

| profilo | gain statico | drive sat | sat vs. ginocchio |
|---|---|---|---|
| Sfera Ebbasta | 13.5 → **9.0** | 42 → **22** | −6.0 → **−10.2** |
| Shiva | 13.5 → **9.3** | 45 → **23** | −5.6 → **−9.6** |
| Tony Boy | 12.0 → **8.5** | 40 → **21** | −6.2 → **−10.9** |
| Glockyy | 13.5 → **9.3** | 48 → **24** | −5.1 → **−9.4** |
| Gue | 11.7 → **8.5** | 45 → **25** | −5.4 → **−10.0** |
| Capo Plaza | 13.5 → **9.2** | 42 → **22** | −6.0 → **−10.0** |

A gain basso i compressori non agganciano niente (riduzione 0.00 dB): è la condizione in cui, prima,
la catena spingeva comunque. Adesso spinge 4.5 dB in meno e satura la metà, con il segnale 10 dB
sotto il ginocchio della tanh invece di 5.

### Misura sul DSP vero (`vocalforge_selftest`)

Riduzione e picco d'uscita misurati facendo girare il processore vero con `fillVoiceLike()`,
normalizzato al picco esatto richiesto:

| profilo | comp 2 @ −6 dBFS | picco uscita @ −6 dBFS | picco uscita @ −18 dBFS | Δ uscita per 12 dB d'ingresso |
|---|---|---|---|---|
| Sfera Ebbasta | 2.84 → **0.07** | −6.2 → −7.5 | −9.1 → **−14.0** | 2.9 → **6.5 dB** |
| Shiva | 3.24 → **0.27** | −5.9 → −6.5 | −7.4 → **−12.8** | 1.5 → **6.3 dB** |
| Tony Boy | 2.59 → **0.05** | −6.1 → −6.8 | −9.0 → **−13.5** | 2.9 → **6.7 dB** |
| Glockyy | 3.38 → **0.39** | −5.0 → −5.6 | −6.7 → **−11.2** | 1.7 → **5.6 dB** |
| Gue | 3.65 → **0.45** | −4.0 → −4.3 | −8.4 → **−12.3** | 4.4 → **8.0 dB** |
| Capo Plaza | 3.09 → **0.15** | −5.9 → −7.1 | −8.3 → **−13.3** | 2.4 → **6.2 dB** |

È la riga più importante del documento: **prima, 12 dB di gain in meno in ingresso davano in media
2.6 dB di uscita in meno — la catena rimetteva tutto a livello da sola. Adesso ne danno 6.6.**
La catena segue la sorgente invece di sostituirla. Il residuo (6.6 invece di 12) è quello che i
makeup, ormai piccoli, restituiscono ancora: azzerarlo del tutto richiederebbe un auto-gain nel DSP,
che è un'altra funzione, non una taratura.

---

## 4. Cosa NON è cambiato, di proposito

- **L'ordine della catena** e l'insieme dei moduli: identici.
- **I profili artista restano distinguibili** (`test_artists.py::test_artists_differ_from_each_other`
  è verde) e coerenti con il loro `sound`: Gue e Glockyy restano i più saturi (25% e 24%, coerente con
  «carattere analogico» e «saturazione evidente»), Tony Boy il più leggero (21%); Glockyy e Shiva
  restano i più compressi (5.02 e 4.98 dB sul primo stadio), Tony Boy il più aperto (4.00 dB).
  Le proporzioni fra i profili sono le stesse: è sceso il livello assoluto, non la loro identità.
- **La parità Python ↔ C++**: 18 preset, 1668 valori, 0 differenze.
- **Ogni `why`** dei parametri toccati è stato riscritto per descrivere la formula nuova — comprese
  quelle che adesso citano la riduzione di cui parlano.

## 5. Il limite residuo, dichiarato

Le regole sono statiche: producono un preset, non reagiscono al segnale. Un makeup che segua la
riduzione **istante per istante** (auto-gain nel compressore) chiuderebbe del tutto il caso a gain
basso, e sarebbe la mossa giusta se il problema si ripresentasse. Va nel DSP, non qui, e va
dichiarato nel preset. Quando (F3) esisterà l'analisi del segnale, `assumed_peak` smetterà di essere
una stima e `work_peak` diventerà una misura: a quel punto queste stesse formule diventano esatte.

---

# Secondo giro: dopo le correzioni del DSP

Il primo giro (tutto quello che sta sopra) ha tarato i numeri su un DSP che poi è cambiato sotto:
`REVIEW.md` ha trovato dodici difetti e sono stati corretti. Tre di quelle correzioni hanno tolto il
terreno da sotto ai piedi a numeri che qui erano dati per buoni. Questa sezione dice **quali numeri
non erano più veri**, **cosa è stato ritarato** e — importante quanto il resto — **cosa non è stato
toccato, e perché**.

La misura di questo giro non si rifà a mano sui preset: si rifà sul processore vero.

```
./build/vocalforge_selftest_artefacts/Release/vocalforge_selftest
```

Le tre sezioni nuove del self test sono la prova:
`de-esser: banda sibilante`, `ducking: quanto scende ogni mandata`, `satTilt`.
Gli invarianti corrispondenti sui numeri stanno in `tools/tests/test_gain_staging.py::TestSecondPass`.

---

## 1. Il de-esser: i numeri erano tarati su un modulo che faceva un'altra cosa

Prima della correzione B2 il de-esser era un **allpass**: abbassava il corpo della voce di 5,3 dB a
200 Hz e *alzava* le sibilanti di 0,9 dB. Le soglie `ds1Thresh`/`ds2Thresh` erano numeri assoluti in
dBFS (`-14 - 12·sibilance_control`) scelti per quel comportamento. Adesso il crossover è vero, il
detector guarda la banda alta vera — e quei numeri, misurati, **non agganciano niente**.

Segnale di prova nuovo: `fillVoiceWithSibilants()`, una vocale con una **'s' vera** ogni 400 ms, la
cui banda alta arriva ~10 dB sotto il picco della voce (è quello che fa una ripresa ravvicinata su
cardioide). Il vecchio `fillVoiceLike()` aveva una riga fissa a 6,8 kHz 18 dB sotto la fondamentale:
un segnale su cui nessun de-esser onesto interviene, e infatti non interveniva.

Misura sul DSP vero, ingresso a −6 dBFS di picco. "relativo" è la banda alta **rispetto al corpo**:
è la de-essatura vera, perché il corpo cambia anche lui (togliendo energia alle esse i compressori
riducono meno). Formato `prima → dopo`.

| profilo | riduz. DS1 | riduz. DS2 | banda alta vs. corpo | corpo (positivo = lisp) |
|---|---|---|---|---|
| Sfera Ebbasta | 2,12 → **4,92 dB** | 1,78 → **3,50 dB** | −1,20 → **−3,04 dB** | +0,56 → **+1,35** (sale) |
| Shiva | 0,44 → **4,08 dB** | 0,78 → **3,20 dB** | −0,34 → **−2,51 dB** | +0,31 → **+1,12** (sale) |
| Tony Boy | 1,28 → **4,50 dB** | 0,58 → **3,30 dB** | −0,64 → **−2,72 dB** | +0,49 → **+1,02** (sale) |
| Glockyy | 0,95 → **4,40 dB** | 0,90 → **3,20 dB** | −0,75 → **−3,98 dB** | +0,41 → **+1,19** (sale) |
| Gue | **0,00** → **3,54 dB** | **0,00** → **2,70 dB** | −0,45 → **−3,33 dB** | +0,35 → **+0,98** (sale) |
| Capo Plaza | 1,70 → **4,71 dB** | 1,71 → **3,40 dB** | −0,86 → **−2,73 dB** | +0,50 → **+1,36** (sale) |

La riga di Guè è la più eloquente: **zero**. Il de-esser era acceso, dichiarato nel preset, con il suo
`why` — e non toccava una sola esse.

### Cosa è cambiato nelle regole

Le soglie diventano **derivate**, con lo stesso criterio con cui il primo giro aveva legato i makeup
alla riduzione: si dichiara *dove sta la sibilante*, e la soglia si calcola da lì.

```
sib_peak   = work_peak - 10                                  ← misurato sul DSP, non ipotizzato
ds1_thresh = clamp(sib_peak - 2 - 6·sibilance_control, -45, 0)
ds1_range  = clamp(2.5 + 2.5·sibilance_control, 0, 8)        ← tetto: oltre 5 dB la 's' sparisce
ds2_thresh = clamp(sib_peak - 1 - 4·sibilance_control - 3·character, -45, 0)
ds2_range  = clamp(1.5 + 1.5·sibilance_control + 1.0·character, 0, 5)
```

`sib_peak` è la misura, non una stima di gusto: la banda sopra il crossover, durante una 's', arriva
a circa 10 dB sotto il picco di lavoro. Le soglie stanno sotto di quello quanto basta per prendere le
esse e lasciare stare le vocali, che nella stessa banda stanno 20 dB più giù.

I `range` **scendono**, anche se la riduzione sale: prima erano 8,0 e 6,0 dB su un modulo che non ci
arrivava mai; adesso sono un tetto vero (5,0 e 3,5 al massimo) e servono a impedire il lisp.

| profilo | ds1Thresh | ds1Range | ds2Thresh | ds2Range | ds2Mode |
|---|---|---|---|---|---|
| Sfera Ebbasta | −26,0 → **−30,0** | 8,0 → **5,0** | −20,0 → **−28,5** | 6,0 → **3,5** | wide → **split** |
| Shiva | −23,6 → **−28,8** | 7,0 → **4,5** | −18,6 → **−27,8** | 5,5 → **3,2** | wide → **split** |
| Tony Boy | −24,8 → **−29,4** | 7,5 → **4,8** | −19,0 → **−28,0** | 5,6 → **3,3** | wide → **split** |
| Glockyy | −23,0 → **−28,5** | 6,8 → **4,4** | −18,4 → **−27,8** | 5,5 → **3,2** | wide → **split** |
| Gue | −19,4 → **−26,7** | 5,2 → **3,6** | −15,8 → **−26,4** | 4,5 → **2,7** | wide → **split** |
| Capo Plaza | −25,4 → **−29,7** | 7,8 → **4,9** | −19,6 → **−28,3** | 5,8 → **3,4** | wide → **split** |

### Il modo del secondo de-esser: da `wide` a `split`

`wide` significa `(low + high) · gain`: la riduzione decisa dalla banda alta si applica a **tutta**
la voce. Con l'allpass era un dettaglio invisibile — il modulo non riduceva quasi mai. Con il
crossover vero e 3 dB di riduzione, `wide` abbassa l'intera voce di 3 dB a ogni 's': è un pompaggio a
banda larga, cioè esattamente il "colloso" che questo lavoro deve togliere. Il de-esser 2 esiste per
le armoniche che la saturazione rigenera **sulle sibilanti**: si tratta la banda alta, non il resto.

---

## 2. Il margine sul limiter: adesso è un tetto, e il conto va rifatto

Con la correzione B4 il limiter è l'**ultimo** stadio: mix, tre bus di mandata e output gain stanno
davanti a lui. Il modello di `tools/tests/test_gain_staging.py` metteva ancora il limiter in mezzo e
l'output gain dopo — cioè calcolava un headroom che nessuno stadio reale vedeva. È stato riordinato,
e ora somma anche le mandate (caso peggiore: tutte in fase).

| | headroom davanti al limiter (nominale) | limiter |
|---|---|---|
| modello vecchio (limiter in mezzo) | 10,2 ÷ 13,3 dB | 0,00 dB |
| modello corretto (limiter ultimo) | **6,6 ÷ 11,3 dB** | **0,00 dB** |

Il margine è 3÷4 dB più stretto di quanto il primo giro credesse — ma c'è, su tutti i 21 prompt e a
tutti e due i livelli d'ingresso. **Sul DSP vero il limiter resta a 0,00 dB di riduzione su tutti e
sei i profili artista, sia a −6 sia a −18 dBFS.** Nessun numero è stato cambiato per questo: il tetto
di 4 dB su `outGain` deciso al primo giro regge anche adesso che il limiter sta dopo. È cambiato il
`why`, che diceva il contrario ("questo stadio sta DOPO il limiter").

Picco d'uscita misurato, ingresso a −6 e a −18 dBFS di picco:

| profilo | picco @ −6 | picco @ −18 | Δ per 12 dB d'ingresso |
|---|---|---|---|
| Sfera Ebbasta | −3,1 | −14,5 | **11,4 dB** |
| Shiva | −2,9 | −12,6 | **9,7 dB** |
| Tony Boy | −2,9 | −13,5 | **10,6 dB** |
| Glockyy | −2,5 | −11,0 | **8,5 dB** |
| Gue | −2,7 | −12,5 | **9,8 dB** |
| Capo Plaza | −3,0 | −12,8 | **9,8 dB** |

La riga più importante del primo giro diceva «12 dB in meno in ingresso danno 6,6 dB in meno in
uscita». Con il DSP corretto **ne danno 10,0 di media**: parte di quel 6,6 non era taratura, era il
de-esser allpass che si mangiava 5 dB di corpo prima dei compressori. Il numero va letto così, e la
tabella del primo giro non è più riproducibile: quei valori appartengono a un DSP che non esiste più.

---

## 3. Ducking: misurato, e **lasciato dov'era** per riverbero e delay

Prima della correzione B5 il detector di ducking non veniva aggiornato per delay e doubler: i
`duck_db` delle regole erano numeri scelti al buio. Adesso hanno effetto su tutti e tre i bus, quindi
sono stati misurati bus per bus — quanto scende la mandata **sotto la parola** e quanto risale **nel
vuoto** dopo (parola di 300 ms, poi silenzio; il wet si isola per sottrazione).

| profilo | riverbero: sotto la parola / nel vuoto | delay: sotto la parola / nel vuoto |
|---|---|---|
| Sfera Ebbasta | −4,08 / −0,85 dB | (1/8 puntato: il primo eco cade dopo la parola) |
| Shiva | −4,25 / −0,81 dB | (1/8 puntato) |
| Tony Boy | −3,91 / −0,79 dB | (1/8 puntato) |
| Glockyy | −4,67 / −0,92 dB | −4,99 / −1,12 dB |
| Gue | −3,90 / −0,80 dB | −4,03 / −1,02 dB |
| Capo Plaza | −4,46 / −0,93 dB | (1/8 puntato) |

È esattamente il comportamento voluto: **4÷5 dB sotto la parola, meno di 1 dB nel vuoto**. Le mandate
si sentono fra le parole e non sopra. I `duck_db` di riverbero e delay (7,4 ÷ 9,0 dB nominali, che
sull'inviluppo reale diventano i 4÷5 dB misurati) **non sono stati cambiati**: erano giusti, e adesso
c'è la misura che lo dice invece della speranza.

### Il doubler è l'eccezione, e lì il numero era sbagliato

Il doubler non è un ambiente: **allarga la voce mentre parla**. Duckarlo come il riverbero lo spegne
proprio dove serve. Con il ducking morto non si vedeva; adesso si misura.

| | duck_db nelle regole | tolto al doubler sulla parola |
|---|---|---|
| prima | 8,0 dB (`4 + 4·density`) | **−4,88 dB** |
| dopo | 3,4 ÷ 4,0 dB (`2 + 2·density`) | **−2,38 dB** |

Restano quel paio di dB che servono a non raddoppiare il centro sulle consonanti, e sparisce la
cancellazione del doubler proprio sulla sillaba che doveva allargare.

---

## 4. `satTilt`: era un parametro finto, adesso inclina davvero — e inclinava troppo

Con la correzione B11 `satTilt` è due shelf speculari a 700 Hz, **attive anche a drive 0**. Il valore
del parametro è quindi **metà** dell'inclinazione totale: la shelf bassa va a −satTilt e l'alta a
+satTilt. Misurato fra 200 Hz e 8 kHz, con la sola saturazione accesa e drive 0:

| satTilt | dislivello misurato 200 Hz → 8 kHz |
|---|---|
| 3,0 dB (il massimo della formula vecchia) | **6,15 dB** |
| 1,5 dB (il massimo della formula nuova) | **3,08 dB** |

`3.0 · brightness` produceva fino a 2,8 dB sui profili — cioè **5,6 dB di inclinazione**, con il corpo
della voce abbassato di 2,8 dB, sopra un'EQ tonale che la brillantezza la fa già lei (`tone2Gain`,
`tone3Gain`, `airGain`). Era una seconda EQ mascherata da colore, e toglieva corpo di nascosto.

```
satTilt = clamp(1.5 * brightness, -2, 2)
```

| profilo | satTilt |
|---|---|
| Sfera Ebbasta | 1,4 → **0,7** |
| Shiva | 0,3 → **0,2** |
| Tony Boy | 0,9 → **0,4** |
| Glockyy | −0,2 → **−0,1** |
| Gue | 0,3 → **0,2** |
| Capo Plaza ("voce molto brillante") | 2,8 → **1,4** |

---

## 5. Cosa NON è stato cambiato, e perché

- **I makeup, le soglie dei compressori, il drive di saturazione, `outGain`, `mix`**: il primo giro
  regge. Misurati sul DSP corretto, comp 1 e comp 2 fanno 0,25÷0,41 e 0,75÷1,15 dB al livello
  nominale, il limiter 0,00: nessuno stadio oltre i 6 dB di CLAUDE.md, a nessuno dei due livelli.
- **Il `duck_db` di riverbero e delay**: misurato giusto (vedi §3). Cambiarlo sarebbe stato fare
  qualcosa per far vedere che si è fatto qualcosa.
- **Il tetto di 4 dB su `outGain`**: regge anche con il limiter dopo. È cambiato solo il `why`.
- **L'ordine della catena, i moduli, i profili artista**: identici. `test_artists.py` resta verde,
  la parità Python ↔ C++ resta 18 preset / 1668 valori / 0 differenze.
- **`logic_recipe`**: unica riga di documentazione toccata — output gain e limiter erano elencati
  nell'ordine vecchio. Adesso dice quello che fa il DSP: `out` e poi `limiter`, ultimo.

## 6. Il limite residuo, aggiornato

A **−18 dBFS d'ingresso i de-esser non agganciano nulla** (riduzione 0,00 su tutti i profili), perché
`sib_peak` è derivato da `work_peak`, che a sua volta assume che l'input trim abbia portato i picchi a
−12 dBFS. Se la sorgente arriva 12 dB più bassa di quanto il profilo dichiara, la sibilante finisce
sotto soglia. È lo stesso limite dichiarato al primo giro, con la stessa risposta: le regole sono
statiche, e quando esisterà l'analisi del segnale (F3) `work_peak` smetterà di essere una stima. Fino
ad allora la cosa onesta è dirlo, non alzare le soglie fino a far lavorare il de-esser sul rumore.

---

# Terzo giro: il limiter vero, il room tamer causale, il MIX che scala le mandate

Il secondo giro ha tarato su un DSP che è cambiato di nuovo (`REVIEW.md`, "Terzo passaggio" e
"Quarto passaggio"): limiter riscritto con un tetto vero, MIX che scala anche le mandate, room tamer
causale e per campione, tilt come stadio proprio, gate con isteresi, makeup smussati, coda dichiarata
calcolata da decay e feedback.

Questo giro **ha cambiato due numeri**. Tutto il resto è stato misurato sul DSP nuovo e **lasciato dov'era**,
con la misura al posto della speranza. La misura si rifà con:

```
./build/vocalforge_selftest_artefacts/Release/vocalforge_selftest
```

Le tre sezioni nuove sono `room tamer`, `mandate` e `coda dichiarata`; gli invarianti corrispondenti
stanno in `tools/tests/test_gain_staging.py::TestThirdPass`. Controlli headless: **229**.

---

## 1. Room tamer: era acceso, dichiarato, e non toglieva niente

Con la correzione R1 il detector legge l'**ingresso dello stadio** (prima la banda 2 leggeva il segnale
già filtrato dalla banda 1), cammina per campione insieme al filtro, e con B17 è normalizzato per il
guadagno del suo Q. `roomThresh` era invece un numero assoluto in dBFS (`-30 + 6·cleanliness`, cioè
−24÷−27 sui profili artista) scelto quando il modulo si comportava in un altro modo.

Il segnale di prova nuovo, `fillRoomResonance()`, è una voce dentro una stanza che risuona **davvero**:
i tre modi assiali (92 · 184 · 246 Hz) eccitati dalle sillabe con una coda di 250 ms — la coda è quello
che distingue un modo di stanza da una nota della voce, ed è l'unica cosa che un notch *dinamico* possa
inseguire. La catena viene **troncata subito dopo il tamer**: compressori e saturazione a valle
reagiscono all'energia tolta e falsano la misura dello stadio (con la catena intera la stessa
attenuazione si legge fino a 4 dB più profonda di quanto il notch stia davvero facendo).

Attenuazione misurata sulle tre bande, ingresso a −6 dBFS di picco. Formato `prima → dopo`:

| profilo | 92 Hz | 184 Hz | 246 Hz | voce (140/420 Hz) |
|---|---|---|---|---|
| Sfera Ebbasta | 0,00 → **1,17** | 0,16 → **3,29** | 0,02 → **2,10** | 0,02 → 0,48 |
| Shiva | 0,00 → **1,78** | 0,03 → **4,03** | 0,00 → **2,71** | 0,01 → 0,60 |
| Tony Boy | 0,01 → **0,72** | 0,41 → **1,96** | 0,06 → **1,15** | 0,06 → 0,28 |
| Glockyy | 0,00 → **3,39** | 0,02 → **4,82** | 0,00 → **3,25** | 0,00 → 0,77 |
| Guè | 0,00 → **2,77** | 0,16 → **3,79** | 0,02 → **2,49** | 0,02 → 0,60 |
| Capo Plaza | 0,01 → **0,58** | 0,42 → **2,11** | 0,06 → **1,26** | 0,06 → 0,30 |

**Prima: da 0,0 a 0,4 dB su tutti e sei i profili.** Il modulo era acceso, aveva le sue profondità
(−3,2÷−6,0 dB), il suo `why`, e non toccava una sola risonanza. È la stessa figura del de-esser del
secondo giro, sullo stadio accanto.

### Cosa è cambiato nelle regole

La soglia diventa **derivata**, con lo stesso criterio delle soglie del de-esser: si dichiara dove il
modo arriva davvero al detector, e la soglia si calcola da lì.

```
mode_peak   = work_peak - 12                                  ← misurato sul DSP, non ipotizzato
room_thresh = clamp(mode_peak - 6 - 6 * cleanliness, -60, 0)
```

I 12 dB sono una misura: il modo più forte, dentro il segnale di prova, arriva al detector 12 dB sotto
il picco di lavoro (il detector è un bandpass normalizzato a 0 dB al centro, quindi legge esattamente
quel livello). I 6 dB fissi sono il margine che tiene fermo il tamer su una voce **senza** risonanze —
c'è un controllo apposta, `senza risonanza il room tamer resta fermo`, e misura ≤ 0,1 dB.

Il termine su `cleanliness` **ha cambiato segno**. Prima più pulizia chiesta *alzava* la soglia, cioè
faceva agganciare i notch più tardi, mentre le profondità crescevano: le due metà del modulo si
annullavano a vicenda. Adesso più pulizia = aggancia prima e scava di più, e si vede nell'ordine dei
profili: Glockyy (cleanliness 0,95) è il più corretto, Tony Boy e Capo Plaza (0,45) i più intatti.

| profilo | roomThresh |
|---|---|
| Sfera Ebbasta | −25,8 → **−34,2** |
| Shiva | −24,9 → **−35,1** |
| Tony Boy | −27,3 → **−32,7** |
| Glockyy | −24,3 → **−35,7** |
| Guè | −25,2 → **−34,8** |
| Capo Plaza | −27,3 → **−32,7** |

### Le profondità e i Q: **non cambiati**

Le profondità (`-2.0 - 4.0·cleanliness` e sorelle, da −3,2 a −6,0 dB) erano irraggiungibili, non
sbagliate: adesso che la soglia aggancia sono un **tetto** che si tocca solo in una stanza pessima. Sul
segnale di prova — che è già una risonanza severa, +6÷9 dB sui modi — il tamer ne usa il 60÷80%, e
resta corsa per il caso peggiore. Il massimo misurato è 4,82 dB su Glockyy, dentro il limite di
CLAUDE.md (mai oltre 8 dB su una singola banda).

**Nessuno zipper con i Q attuali (5÷6):** il salto massimo campione-campione con il tamer acceso è
*minore* di quello a tamer spento (0,0105 contro 0,0121 su Shiva) — la profondità è smussata a 30 ms e
i coefficienti si aggiornano ogni 16 campioni, indipendentemente dal blocco. C'è il controllo.

### Due cose dichiarate, non aggiustate

- **La banda 1 (92 Hz) sta sotto l'HPF della catena** (80÷100 Hz, 24 dB/oct): sui profili con HPF a
  96÷100 Hz (Sfera, Tony Boy, Capo Plaza) la risonanza arriva al detector già tagliata e il notch trova
  poco da togliere (0,58÷1,17 dB); su quelli con HPF a 80÷82 Hz (Glockyy, Guè) morde per intero
  (2,77÷3,39 dB). Non è un errore di taratura, è un doppione: il lavoro l'ha già fatto l'HPF. Per
  questo il controllo chiede il minimo ai modi 2 e 3, non al modo 1.
- **A −18 dBFS d'ingresso il tamer non aggancia niente** (0,00 dB su tutti i profili), esattamente come
  i de-esser: `mode_peak` deriva da `work_peak`, che assume che l'input trim abbia portato i picchi a
  −12 dBFS. È lo stesso limite dichiarato ai due giri precedenti, con la stessa risposta: si dice,
  non si abbassa la soglia fino a far lavorare i notch sul rumore.

---

## 2. Margine sul limiter: il tetto adesso è vero, e il margine **regge**. Niente cambiato

Prima della correzione B14 il limiter di JUCE riamplificava di −threshold: il tetto reale era sempre
0 dBFS e abbassare il ceiling *alzava* l'uscita. Adesso è una riduzione verso il tetto più un clamp sul
campione. Il conto del secondo giro andava rifatto su questo — ed è stato rifatto, sul DSP vero.

Picco d'uscita misurato con `fillAtPeak()`, ceiling a −1,0 dBFS su tutti i profili:

| profilo | picco @ −6 dBFS | margine sul tetto | picco @ −18 dBFS | riduzione del limiter |
|---|---|---|---|---|
| Sfera Ebbasta | −7,4 | 6,4 dB | −17,8 | **0,00 dB** |
| Shiva | −6,3 | 5,3 dB | −16,5 | **0,00 dB** |
| Tony Boy | −6,8 | 5,8 dB | −17,6 | **0,00 dB** |
| Glockyy | −5,6 | 4,6 dB | −15,0 | **0,00 dB** |
| Guè | −6,1 | 5,1 dB | −16,1 | **0,00 dB** |
| Capo Plaza | −6,7 | 5,7 dB | −16,7 | **0,00 dB** |

Il limiter non lavora su nessun profilo, a nessuno dei due livelli: **4,6÷6,4 dB di margine sul tetto
vero**. Il modello statico di `test_gain_staging.py` dice la stessa cosa su tutti e 21 i prompt
(headroom davanti al limiter 6,6÷11,3 dB, riduzione 0,00). `outGain`, `mix`, i makeup e le soglie dei
compressori **non sono stati toccati**: il tetto di 4 dB su `outGain` del primo giro regge anche
adesso che il tetto del limiter è reale.

Unica cosa che si muove, ed è un effetto del §1: con il room tamer che finalmente toglie energia nella
banda dei modi, i compressori a valle riducono un po' meno (comp 1 da 0,79÷1,87 a 0,59÷1,33 dB, comp 2
da 1,15÷1,68 a 0,79÷1,28). Va nella direzione giusta: meno lavoro perché arriva meno da domare.

---

## 3. Mandate e MIX: il rapporto **non** cambia. Un solo numero era sbagliato, e non era il MIX

Da B4 il MIX moltiplica anche le tre mandate. La paura era che la valvola parallela del primo giro
(mix 78% su tutti i profili artista) si portasse via l'ambiente. Misurato bus per bus — il doubler non
è ambiente, e sommarlo al riverbero nasconde il profilo che il doubler non ce l'ha — la risposta è no:

| profilo | riverbero (mix preset → mix 100%) | delay | doubler |
|---|---|---|---|
| Sfera Ebbasta | −32,7 / −32,5 | −40,9 / −40,6 | −30,1 / −29,6 |
| Shiva | −39,3 / −39,3 | −38,6 / −38,3 | −31,4 / −30,7 |
| Tony Boy | −31,0 / −31,0 | −42,1 / −42,0 | −31,3 / −30,8 |
| Glockyy | −39,7 / −38,8 | −39,8 / −38,7 | −31,9 / −30,4 |
| Guè | −36,8 / −35,9 | −40,7 / −39,7 | — |
| Capo Plaza | −33,6 / −33,7 | −39,7 / −39,5 | −30,4 / −30,0 |

Il MIX scala il wet **e** la voce con cui si somma: il rapporto si sposta di meno di 1,5 dB fra mix
78% e mix 100%. È il comportamento giusto, ed è quello che il DSP fa adesso. **Nessun `send_db` è
stato cambiato per il MIX**, e c'è il controllo che lo tiene fermo.

### `rev_ambience`: quello sì

Il caso vero di "l'ambiente sparisce dove serve" non era il MIX, era una mandata già troppo bassa in
partenza. `rev_ambience` è la variante con mezzo secondo di coda, scritta per la voce parlata — e i
profili di **Shiva e Glockyy la impongono** (`forced_sends`). A parità di mandata una coda di 0,5 s
restituisce molta meno energia di una plate o di una hall: misurato, il loro riverbero stava a
**−43,3 e −43,7 dB sotto la voce**, contro i −31÷−37 di tutti gli altri. Non è "asciutto", è assente.

```
rev_ambience.send_db:  clamp(-32 + 4·neg(intimacy), -40, -6)  →  clamp(-28 + 4·neg(intimacy), -40, -6)
```

Adesso Shiva sta a −39,3 e Glockyy a −39,7: restano i due **più asciutti** dei sei, come vuole il loro
profilo, ma l'ambiente c'è. Le mandate di riverbero di tutti i 18 preset stanno ora in una fascia
coerente (−23,2 ÷ −28,0 dB), e c'è l'invariante che lo tiene.

Il `duck_db` di riverbero e delay, il `send_db` di delay e doubler, il ducking del doubler: **non
toccati**, misurati giusti al secondo giro e ancora giusti adesso (la sezione `ducking` del self test
gira su questo giro con gli stessi 4÷5 dB sotto la parola e meno di 1,2 dB nel vuoto).

---

## 4. Coda dichiarata all'host: misurata, **nessun preset la allunga assurdamente**

Da R7 `getTailLengthSeconds` è calcolata da `revDecay` (fino a 4 s) e `dlyFeedback` (fino al 45%).
Misurata sul processore vero, per profilo:

| profilo | coda dichiarata | revDecay | dlyFeedback |
|---|---|---|---|
| Sfera Ebbasta | 2,01 s | 1,00 s | 18% |
| Shiva | 2,32 s | 0,50 s | 24% |
| Tony Boy | 2,52 s | 1,98 s | 16% |
| Glockyy | 1,01 s | 0,50 s | 13% |
| Guè | 1,52 s | 1,00 s | 10% |
| Capo Plaza | 2,16 s | 1,00 s | 21% |

Su tutti e 21 i prompt il massimo è **3,06 s** (cantautore acustico, hall da 2,52 s). I due estremi
temuti non si presentano: `revDecay` a 4 s richiederebbe intimità massima con densità zero, e le
formule del feedback si fermano al 30% molto prima del tetto del parametro (45%). **Niente da
accorciare** — accorciare qui avrebbe tolto coda al suono per risolvere un problema che il bounce non
ha. C'è l'invariante (≤ 6 s) e la misura per profilo nel self test.

---

## 5. Riepilogo: cosa è cambiato, cosa no

**Cambiato (2 numeri):**
- `roomThresh` → derivato da `mode_peak` (`work_peak - 12`), con il segno di `cleanliness` corretto.
  Da 0,0÷0,4 dB di attenuazione a 0,6÷4,8 dB su una risonanza vera.
- `rev_ambience.send_db` da −32 a −28 dB: era la mandata che faceva sparire l'ambiente su Shiva e
  Glockyy, gli unici due profili che quella variante ce l'hanno imposta.

**Non cambiato, e misurato:** profondità e Q del room tamer, `outGain` e il suo tetto di 4 dB, `mix`,
makeup e soglie dei due compressori, `satDrive` e `satTilt`, soglie e range dei due de-esser, tutti i
`duck_db`, i `send_db` di delay e doubler, `revDecay` e `dlyFeedback`, l'ordine della catena.

**Non più vero, corretto qui:** la tabella dei picchi d'uscita del secondo giro (−2,5÷−3,1 dBFS)
apparteneva al limiter di JUCE che riamplificava di −threshold. Con il tetto vero i picchi stanno a
−5,6÷−7,4 dBFS: quel giro leggeva 1 dB di riamplificazione più il livello pieno delle mandate a mix
78%. I numeri buoni sono quelli del §2.

Parità Python ↔ C++: 18 preset, 1683 valori, 0 differenze. Test Python: 84. Controlli headless: 229.
