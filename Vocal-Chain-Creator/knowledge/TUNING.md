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
