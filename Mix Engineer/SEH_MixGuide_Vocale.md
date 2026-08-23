# SEH — Guida al Mix Vocale
**Template:** Barca Mafia · **Beat:** IMPICCI (Flaco G × Lubi) · **Key:** C minore · **BPM:** 120 · **SR:** 48 kHz

---

## 1. Architettura del Routing — Mapping Tracce

Prima di intervenire su qualsiasi parametro, è necessario definire il routing corretto per le tre tipologie vocali. Il template Barca Mafia prevede già la struttura gerarchica necessaria; si tratta unicamente di assegnare ciascuna voce al canale appropriato.

| Tipologia vocale | Traccia Logic | Bus di uscita | Pan |
|---|---|---|---|
| **Main (Lead)** | Audio 2 | Aux 1 (Vocal Group Bus) | Centro (0) |
| **Doppie** | Audio 3 | Aux 1 o Aux 2 | ±18° L/R (es. L –18, R +18) |
| **Sporche / Ad-lib** | Audio 5 (o 6) | Aux 1 diretto o bypass Aux | Centro o leggero stereo ±10° |

> **Nota sul panning delle doppie:** nelle produzioni trap italiana contemporanea il doppiaggio viene raramente allargato oltre ±25°. Un panning eccessivo sposta la massa tonale fuori dal centro e causa problemi in mono (club, Bluetooth). Inizia a ±15° e testa in mono prima di procedere.

---

## 2. Gain Staging — Punto di Partenza

Tutti gli interventi successivi presuppongono che il gain staging sia corretto. Verifica questi valori prima di toccare qualsiasi plugin:

- **Picco in ingresso su Audio 2, 3, 5:** tra **–18 dBFS e –12 dBFS**
- **Fader di bilanciamento rough:**
  - Main (Audio 2): **0 dB**
  - Doppie (Audio 3): **–6 / –8 dB**
  - Sporche (Audio 5/6): **–8 / –12 dB** (dipende dall'aggressività della performance)
- **Master output:** non superare **–6 dBFS** prima del mastering

---

## 3. Main Vocal (Audio 2) — Catena Completa

La voce principale è il canale con il processing più elaborato del template. La catena è in serie, nell'ordine seguente:

### 3.1 Pitch Correction — Waves Tune (o Pitch Correction Logic)

Nel genere trap italiana il pitch correction è parte integrante del timbro, non una correzione trasparente. Si lavora su due livelli:

- **Speed:** 20–35 ms (correzione veloce ma non robotica; per effetto Auto-Tune più marcato scendere a 0–10 ms)
- **Retune Speed su note target:** impostare la scala su **C minore** (coerente con la chiave del progetto)
- **Formant correction:** attivo se la correzione è aggressiva, per evitare l'effetto "chipmunk" sulle note basse

> Se si utilizza il **Pitch Correction nativo Logic** (sostituto): impostare *Speed* verso *Fast*, attivare *Snap to Scale*, selezionare la scala C minore.

### 3.2 Compressor — Preset "Male Vocal – Wide Band"

Il compressore sulla singola traccia ha il compito di controllare i picchi dinamici *prima* che il segnale raggiunga il bus. Non deve levigare: quello è lavoro della catena su Aux 1.

| Parametro | Valore |
|---|---|
| Threshold | –18 / –22 dBFS (dipende dal livello del segnale in ingresso) |
| Ratio | 3:1 – 4:1 |
| Attack | 8–12 ms (lascia passare il transitorio, preserva il taglio consonantico) |
| Release | Auto, oppure 80–120 ms |
| Gain Reduction | 4–8 dB in media |
| Make-up Gain | Compensa a orecchio: il livello percepito non deve cambiare |

### 3.3 DeEsser 2

Le sibilanti aggressive (6–10 kHz) vengono gestite qui prima di raggiungere il bus, dove un secondo DeEsser opera sull'insieme.

| Parametro | Valore |
|---|---|
| Frequenza | 6.5–8 kHz (ascolta dove si addensa la sibilante specifica di questo artista) |
| Threshold | Impostare fino a percepire riduzione solo sulle "s" più acute |
| Gain Reduction | Non oltre –6 dB: il de-essing eccessivo toglie presenza |

### 3.4 Limiter

Ceiling di protezione prima del bus. Funzione esclusivamente preventiva: se il limiter lavora costantemente, il problema è nel gain staging a monte.

- **Output Ceiling:** –1.0 dBFS (headroom verso Aux 1)
- **Gain Reduction visibile:** occasionale sui picchi transienti, non continua

---

## 4. Doppie (Audio 3) — Colore Vintage

Il doppiaggio è il canale con il carattere timbrico più distinto. La presenza della UADx Century Tube Channel Strip (emulazione Neve 1073) — o delle sue alternative native — imprime un colore armonico caldo sulle medie frequenze, differenziando il double dal main.

### 4.1 Filosofia del Double nel Trap Italiano

A differenza del pop o dell'R&B, nel trap italiano le doppie raramente sono raddoppi puntuali all'unisono. Più spesso:
- Entrano solo su certi pattern ritmici (ritornello, hook)
- Hanno una leggera discrepanza di timing (+10/–10 ms) rispetto al main: **non correggere questa discrepanza con quantizzazione**
- Il livello è percettivamente sotto il main: deve essere *sentita* come spessore, non come seconda voce distinta

### 4.2 Century Tube Channel Strip / Neve 1073 (o alternativa Logic)

| Parametro | Valore |
|---|---|
| Input Gain | +2 / +4 dB per eccitare l'armonica della valvola |
| EQ Low (80 Hz) | +2 dB shelf (corpo, fondamentale) |
| EQ Mid (1.6 kHz) | –1 / –2 dB (riduce la nasalità; non competere con la presenza del main) |
| EQ High (12 kHz) | +1 dB shelf (aria, coerente col main) |
| Output | Compensare il gain aggiunto in input |

**Alternativa nativa Logic:** Vintage Console EQ + Vintage VCA Compressor
- Vintage Console EQ: replica il comportamento del Neve 1073 con curve più morbide
- Vintage VCA: ratio 2:1, attack 20 ms, release 150 ms — compressione leggera e musicale

### 4.3 Channel EQ (rifinitura post-Century)

Dopo la Century Tube, un Channel EQ fa rifinitura chirurgica:
- **High-Pass a 120 Hz** (le doppie non devono aggiungere energia alle basse; la voce principale già gestisce quel range)
- **Notch stretto (–3 dB)** attorno a 400 Hz se la voce suona "chiusa" o "in scatola"
- **Shelf alta a +1 dB** a 10 kHz per coerenza con il main

---

## 5. Sporche / Ad-lib (Audio 5 o 6) — Processing Aggressivo

Le voci "sporche" nell'estetica trap italiana sono spesso concepite come elemento di texture e aggressività, non come linea melodica. Il processing riflette questa filosofia.

### 5.1 Catena Consigliata

Poiché Audio 5/6 è una traccia di riserva senza plugin precaricati, costruire la catena dall'inizio nel seguente ordine:

1. **High-Pass Filter** a 200 Hz (in Channel EQ) — elimina tutto il basso; le sporche vivono nelle medie e alte
2. **Compressor (Vintage FET / 1176)** — ratio 8:1 o 10:1, attack 1–2 ms, release auto. Compressione molto aggressiva (8–12 dB GR) è intenzionale: crea quella densità compressa tipica degli ad-lib trap
3. **Saturatore / Distorsore** — Bitcrusher (Logic) a 12–14 bit per un'aggressività digitale, oppure Overdrive leggero per un'esasperazione armonica. Inizia con Drive al 20–30%, poi testa nel contesto del mix
4. **DeEsser** — facoltativo; se le sporche sono molto sature le sibilanti diventano meno critiche
5. **Limiter** — ceiling –1 dBFS di protezione

### 5.2 Routing delle Sporche

Le sporche possono essere inviate direttamente ad Aux 1 (nel bus vocale collettivo) oppure tenute fuori dal bus e gestite in parallelo direttamente sul master. La seconda opzione dà maggiore controllo ma richiede un panning e una gestione livelli più attenta.

**Opzione consigliata per questo mix:** inviare ad Aux 1, con il fader di send più basso (–4 / –6 dB) rispetto alle altre voci.

---

## 6. Bus di Gruppo — Aux 1 (Vocal Group Bus)

Tutti i canali vocali confluiscono qui. Il processing su Aux 1 opera sull'insieme: ogni modifica impatta main, doppie e sporche contemporaneamente.

### 6.1 Channel EQ (globale)

| Frequenza | Intervento |
|---|---|
| < 80 Hz | High-Pass a 80 Hz, 24 dB/oct — rumori ambientali, vibrazione meccanica |
| 200–400 Hz | Taglio –2/–3 dB, Q medio (0.5–0.7) — nasalità dell'insieme |
| 3–5 kHz | Boost lieve +1/+2 dB, Q ampio — presenza e intelligibilità |
| 8–12 kHz | Shelf +1 dB — aria e brillantezza |

### 6.2 DeEsser sul Bus (seconda linea)

Il DeEsser su Aux 1 cattura le sibilanti che sfuggono ai de-esser sui singoli canali, specialmente quando le doppie e le sporche si sommano al main nella stessa regione di frequenza. Frequenza target: **7–9 kHz**, threshold conservativo.

### 6.3 Compressione 1176 → LA-2A (o native Logic)

La catena in serie è una delle più classiche dell'ingegneria del suono vocale.

**Vintage FET Compressor (emulazione 1176):**
| Parametro | Valore |
|---|---|
| Ratio | 4:1 |
| Attack | 3 ms |
| Release | Auto |
| Gain Reduction | 4–6 dB |

**Vintage Opto Compressor (emulazione LA-2A):**
| Parametro | Valore |
|---|---|
| Mode | Peak |
| Gain Reduction | 2–4 dB |
| Funzione | Leviga il sustain dopo il FET; non deve essere udibile, solo percepita |

---

## 7. Bus Effetti in Parallel Send

### 7.1 Aux 3 — Riverbero

Nel trap italiano il riverbero è usato con parsimonia sulla voce principale: l'ambiente deve percepirsi come supporto spaziale, non come coda evidente.

| Parametro | Valore |
|---|---|
| Plugin | Quantec Room Simulator preset "Club" / Space Designer preset "Small Room" |
| Mix sull'Aux | 100% Wet |
| Pre-delay | **20–25 ms** sulla main, **10–15 ms** sulle sporche (più vicine, più aggressive) |
| Send da Audio 2 (main) | –18 / –20 dB (punto di partenza; aumenta fino a sentire la spazialità senza perdere definizione) |
| Send da Audio 3 (doppie) | –22 / –24 dB (meno riverbero delle main: devono restare più secche) |
| Send da Audio 5/6 (sporche) | –24 / –30 dB o 0 (le sporche spesso stanno bene completamente dry) |

### 7.2 Aux 4 — Delay

Il delay nel trap italiano è quasi sempre sincronizzato al BPM e brevissimo nel feedback. La tecnica del sidechain (delay che emerge solo nelle pause) è altamente raccomandata per mantenere pulizia.

| Parametro | Valore |
|---|---|
| Plugin | H-Delay (Waves) / Tape Delay o Echo (Logic nativi) |
| Sync | Attivo — Nota: **1/8 nota puntata** (tipico nel trap, crea un'eco che "incastra" nel groove) |
| Feedback | 15–20% |
| High-Cut | 6–8 kHz (le ripetizioni devono essere più scure della voce dry) |
| Send da Audio 2 | –22 / –24 dB |
| Send da Audio 3/5/6 | 0 o send minimo; le doppie e sporche raramente traggono beneficio dal delay |

**Tecnica sidechain:** inserire un compressore su Aux 4 con side-chain triggerato da Audio 2. Quando la voce principale è attiva, il delay viene abbassato automaticamente; emerge solo nelle pause.

---

## 8. Aux 2 — Bus Secondario (Colore Armonico)

Aux 2 con Century Channel Strip / Neve 1073 può essere utilizzato in due modi:

1. **Separazione dei gruppi:** instradare le doppie e le sporche su Aux 2, la main su Aux 1 — processing diversificato per tipologia
2. **Warm-up dell'insieme:** inviare Aux 1 → Aux 2 per un secondo stadio di colorazione armonica sull'intero mix vocale

**Raccomandazione per questo mix:** usare Aux 2 come bus di gruppo separato per le doppie, mantenendo la main su Aux 1. Questo permette di regolare il carattere delle doppie indipendentemente dal main.

---

## 9. Sequenza Operativa Consigliata

Il rough mix si costruisce in questa sequenza, senza saltare passaggi:

1. **Bilanciamento a fader flat** — nessun plugin attivo, solo livelli e pan
2. **Pitch correction** su Audio 2 (e Audio 3 se necessario)
3. **EQ e compressione** su singoli canali (Audio 2, 3, 5/6)
4. **EQ e compressione** su Aux 1
5. **Apertura dei send** verso Aux 3 e Aux 4, livello per livello
6. **A/B comparison** con la reference: alterna in loop tra il mix e il video di riferimento ogni 30–60 secondi
7. **Test in mono** (Stereo Out → Mono): verifica che le doppie non spariscano e che il main mantenga il centro
8. **Bounce del rough mix** a –3 dBFS peak, 24 bit, 48 kHz

---

## 10. Reference Track — Criteri di Confronto

Non avendo potuto identificare il titolo specifico del brano di riferimento (ID: qaBx6qc4eqQ), è possibile condurre ugualmente un confronto produttivo per attributi:

| Attributo | Da valutare nella reference |
|---|---|
| **Presence della voce** | Quanto è in primo piano rispetto al beat? (range tipico trap IT: molto presente) |
| **Aggressività del pitch** | Il pitch correction è udibile come effetto estetico o trasparente? |
| **Spazio delle doppie** | Sono panned o restano al centro? Entrano solo sul ritornello? |
| **Coda del riverbero** | Corta (< 1 s) o aperta? |
| **Brillantezza** | La voce ha presenza nelle alte (8–12 kHz) o è più "dark"? |

Utilizzare il **Channel EQ in modalità analizzatore** con la reference importata su una traccia separata per confrontare visivamente la distribuzione spettrale.

---

*Guida generata da Claude · Progetto SEH · Mix Engineer Workspace · Maggio 2026*
