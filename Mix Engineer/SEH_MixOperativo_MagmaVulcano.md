# SEH — Guida Operativa di Mix × Magma Vulcano
**Reference:** Gallagher feat. Traffik – *Magma Vulcano* (prod. Youngotti, 2018)
**Progetto:** SEH.logicx · C minore · 120 BPM · 48 kHz
**Canali vocali:** Main (Audio 2) · Doppie (Audio 3) · Sporche (Audio 5 o 6)

---

> Questa guida è costruita sui plugin **effettivamente presenti** nel tuo ProjectData, nell'ordine esatto in cui compaiono nella catena di ciascun canale. Nulla è ipotetico: ogni istruzione corrisponde a un plugin già caricato nel tuo template.

---

## PRIMA DI TUTTO — Preparazione Obbligatoria

### 1. Verifica il beat importato
Nel tuo progetto il beat si chiama **"Beat (intuisci)"**. Prima di qualsiasi intervento vocale:
- Apri il mixer (tasto **X**) e individua la traccia del beat
- Abbassa il fader a **–6 dB** rispetto al default: la voce deve respirare nel mix senza doversi "guadagnare" lo spazio in fase di bilanciamento
- In Magma Vulcano il beat di Youngotti è sempre leggermente arretrato rispetto alla voce: questa proporzione è intenzionale e va replicata

### 2. Imposta la chiave del progetto
Vai in **Transport Bar → Key Signature**: la tonalità del progetto è già impostata su **C minore**. Poiché i tuoi take vocali sono stati registrati su questo beat, la chiave è coerente. Non modificare.

### 3. Gain staging in ingresso — controlla prima di toccare i plugin
Apri ciascuna traccia vocale e verifica il livello delle regioni audio:
- I picchi devono stare tra **–18 dBFS e –12 dBFS**
- Se le forme d'onda sono troppo alte o troppo basse, usa il **gain clip** (tasto **G** sulla regione, poi Function → Gain) prima di avviare il processing
- I plugin del template sono calibrati per lavorare in questo range. Segnali più forti faranno lavorare il compressore troppo; più deboli lasceranno troppo rumore di fondo

---

## AUDIO 2 — Main Vocal (Lead)

La catena sul tuo canale è, nell'ordine dall'alto verso il basso nell'Inspector:

```
Cnsl EQ  →  Waves Tune  →  Compressor  →  DeEsser 2  →  Limiter
```

Procedi nell'ordine della catena, non saltare passaggi.

---

### Step 1 · Cnsl EQ (Vintage Console EQ — pre-EQ)

Questo EQ è posizionato **prima** di Waves Tune. La sua funzione in questa posizione è preparare il segnale alla pitch correction: una voce pulita spettralmente è più facile da correggere intonazione e suona più naturale anche con correzione aggressiva.

| Banda | Intervento | Motivazione |
|---|---|---|
| High-Pass | **80 Hz, 24 dB/oct** | Taglia tutto sotto: click, vibrazioni meccaniche, rumori ambientali. Obbligatorio prima della pitch correction |
| Low-Mid (~250–350 Hz) | **–2 dB, Q medio** | Riduce la "muddiness" tipica delle riprese in ambienti non trattati. Gallagher ha una voce pulita nelle basse-medie: replicalo |
| Presence (~3–4 kHz) | **+1 dB, Q ampio** | Leggero boost di presenza pre-Tune aiuta l'algoritmo di pitch detection a lavorare sulle armoniche più definite |

> In Magma Vulcano la voce è brillante ma mai aggressiva nelle alte-medie. Non esagerare con l'EQ in questa sede: è un pre-processing, non il punto dove costruire il timbro.

---

### Step 2 · Waves Tune

Nel trap italiano del 2018 (Gallagher/Traffik) il pitch correction è un **elemento estetico esplicito**, non una correzione trasparente. Si sente, fa parte del suono.

**Impostazioni operative:**

| Parametro | Valore | Nota |
|---|---|---|
| Scale | **C Minor** | Coerente con la chiave del progetto |
| Retune Speed | **30–50 ms** | Veloce ma non robotico. Per ottenere l'effetto più marcato tipico delle note lunghe in Magma Vulcano, scendi a **15–20 ms** solo nelle sezioni hook/ritornello |
| Vibrato | **Naturale** (ridotto al 30–40%) | Gallagher non ha vibrato marcato: il Tune non deve appiattire ma nemmeno esagerarlo |
| Note segmentation | **Medium** | Evita correzioni eccessive su consonanti |

**Come verificarlo:** ascolta le note lunghe del tuo ritornello. In Magma Vulcano senti chiaramente il "lock" della voce sulla nota target, specialmente sulle vocali tenute. Se il tuo Tune è troppo lento (> 80 ms) non ottieni quell'effetto; troppo veloce (< 10 ms) suona robotico e distante dall'estetica del pezzo.

---

### Step 3 · Compressor

Il compressore su Audio 2 gestisce le dinamiche della singola voce prima che raggiunga il bus. Il suo compito è **controllare i picchi**, non levigare: il lavoro di levigatura avviene su Aux 1.

Il preset caricato è **"Male Vocal – Wide Band"**: è già calibrato per voci maschili. Modifica solo i parametri che deviamo dalla reference:

| Parametro | Valore target | Come agire |
|---|---|---|
| Threshold | **–18 / –20 dBFS** | Regola finché il gain reduction meter mostra costantemente 4–6 dB nei punti più forti della performance |
| Ratio | **3:1** (lascia il preset se è già qui) | Gallagher ha una voce dinamicamente contenuta: una ratio più alta suonerebbe artificiale |
| Attack | **10–15 ms** | Lascia passare il transitorio consonantico. Se comprimi troppo veloce perdi il taglio delle "k", "t", "p" |
| Release | **Auto** | Lascia che il compressore gestisca autonomamente: le variazioni di frase sono irregolari nel trap |
| Make-up Gain | Compensa a orecchio | Il livello percepito dopo il compressore deve essere uguale a prima. Usa il bypass (A/B) per verificare |

---

### Step 4 · DeEsser 2

Le sibilanti della voce principal devono essere ridotte qui, prima del bus, dove un secondo DeEsser catturerà ciò che sfugge a questo stadio.

**Procedura operativa:**
1. Metti il DeEsser 2 in modalità **Detect: Broadband** (o la modalità equivalente nel tuo preset)
2. Riproduci una sezione con molte "s" e "sh"
3. Muovi la frequenza tra **6.000 Hz e 9.000 Hz** finché il meter di gain reduction si attiva con ogni sibilante
4. Imposta il threshold in modo che la riduzione sia **–3 / –5 dB massimo**: mai di più, altrimenti la voce perde presenza
5. In Magma Vulcano le sibilanti ci sono ma non sono aggressive: l'estetica non richiede de-essing estremo

---

### Step 5 · Limiter

Ceiling di protezione finale prima del bus. **Non deve lavorare in modo udibile.**

- **Output Ceiling: –1.0 dBFS**
- Se il meter di gain reduction si illumina frequentemente e con continuità, il problema è a monte (gain staging o compressore troppo debole)
- Se invece si illumina solo su picchi transienti isolati: corretto

---

### Routing di Audio 2

Output del canale → **Bus 1 (Aux 1)**
Send → **Aux 3** (riverbero): apri il send, imposta a **–20 dB**
Send → **Aux 4** (delay): apri il send, imposta a **–24 dB**

---

## AUDIO 3 — Doppie

La catena sul canale è:

```
Waves Tune  →  Doubler4 (m->s)  →  Compressor  →  Vintage Console EQ
```

Il **Doubler4 (m->s)** è il plugin chiave di questo canale: crea automaticamente un effetto di raddoppio stereofonico partendo da un segnale mono. È una delle ragioni per cui il template Barca Mafia ha un sound così caratteristico sulle doppie.

---

### Step 1 · Waves Tune (sulle doppie)

Le doppie non devono essere corrette all'identico della main. In Magma Vulcano le voci doppiate hanno una leggera discrepanza di intonazione rispetto al main: è parte del suono, non un errore.

| Parametro | Valore | Differenza rispetto al main |
|---|---|---|
| Scale | **C Minor** | Uguale |
| Retune Speed | **60–80 ms** | Più lento del main: la double deve avere una micro-discrepanza naturale |
| Vibrato | **Naturale** (al 50–60%, leggermente più del main) | Una leggera oscillazione differenzia timbricament le due voci |

---

### Step 2 · Doubler4 (m->s)

Questo è il plugin che crea la "larghezza" delle doppie nel tuo template. Lavora con un segnale mono in entrata e genera un'uscita stereo con micro-delay e pitch-shift controllati.

**Impostazioni operative per il sound Magma Vulcano:**

| Parametro | Valore | Nota |
|---|---|---|
| Voice L — Delay | **8–12 ms** | Anticipa leggermente la voce sinistra |
| Voice R — Delay | **12–18 ms** | Ritarda leggermente la voce destra |
| Voice L — Pitch | **–8 / –10 cent** | Micro-detune sinistra verso il basso |
| Voice R — Pitch | **+8 / +10 cent** | Micro-detune destra verso l'alto |
| Mix | **60–70%** | Non full wet: lascia trasparire il segnale dry originale per coerenza con il main |

> Gallagher usa doppie relativamente strette (non allargate al massimo). Un Doubler con detune eccessivo (> 20 cent) suona moderno pop, non trap italiana 2018. Rimani contenuto.

**Test in mono obbligatorio:** dopo aver impostato il Doubler, premi il pulsante Mono sullo Stereo Out del master. Le doppie non devono scomparire o "combinarsi" con il main in modo anomalo. Se senti phase cancellation (la voce si svuota), riduci il detune o il delay del Doubler.

---

### Step 3 · Compressor

Compressione leggera sulle doppie: devono mantenere dinamica naturale perché il Doubler è già un processing pesante.

| Parametro | Valore |
|---|---|
| Ratio | **2:1** |
| Attack | **20–30 ms** (più lento del main) |
| Release | **150–200 ms** |
| Gain Reduction | **2–4 dB** massimo |

---

### Step 4 · Vintage Console EQ (sulle doppie)

Qui costruisci il timbro delle doppie differenziandolo dal main. L'obiettivo è che le doppie si sentano come un "corpo" che supporta il main, non come una seconda voce identica in competizione.

| Banda | Intervento |
|---|---|
| High-Pass | **120 Hz** (più alto del main: le doppie non devono aggiungere energia alle basse) |
| Low-Mid (~350–500 Hz) | **–3 dB, Q stretto**: scava le frequenze dove il main vive, per non creare addensamento |
| High (~10–12 kHz) | **+1 dB shelf**: aria, coerenza con il main ma senza competere |

### Fader e routing delle doppie
- Fader: **–6 / –8 dB** rispetto al main (le doppie supportano, non competono)
- Pan: **0° (centro)** — in Magma Vulcano le doppie sono prevalentemente centrali. Se vuoi un leggero allargamento, non superare **±12°**
- Output → **Bus 1 (Aux 1)**
- Send verso Aux 3 (riverbero): **–24 dB** (meno riverbero del main, devono restare più secche)
- Send verso Aux 4 (delay): **0 / nessun send** (le doppie generalmente non vanno nel delay)

---

## AUDIO 5 o 6 — Sporche / Ad-lib

Le tracce 5 e 6 sono tracce di riserva **senza plugin precaricati**. Costruirai la catena da zero. Le "sporche" nel sound di Gallagher/Traffik sono voci più grezze, spesso in primo piano durante le sezioni più aggressive, con una texture compressa e satura.

### Catena da costruire manualmente su Audio 5 (apri ogni slot con il "+" nel canale):

```
Channel EQ  →  Vintage FET Compressor  →  Bitcrusher  →  Limiter
```

**Channel EQ:**
| Banda | Intervento |
|---|---|
| High-Pass | **200 Hz, 12 dB/oct**: le sporche esistono solo nelle medie e alte. Taglia tutto il basso per non sporcare il mix |
| Mid-Presence (2–4 kHz) | **+2 / +3 dB, Q ampio**: le sporche devono "tagliare" e avere aggressività |
| Low-cut sulle alte (> 12 kHz) | **–2 dB shelf**: rimuove brillantezza eccessiva, le sporche sono grezze, non brillanti |

**Vintage FET Compressor (emulazione 1176):**
| Parametro | Valore |
|---|---|
| Ratio | **8:1** (aggressivo — intenzionale per le sporche) |
| Attack | **1–2 ms** (istantaneo: comprimi tutto) |
| Release | **Auto** |
| Gain Reduction | **8–12 dB**: la compressione aggressiva è parte dell'estetica. Le sporche devono suonare "schiacciate" |

**Bitcrusher (Logic nativo — inserisci dopo il compressore):**
- **Bit Depth: 12–14 bit** (abbassa da 24 bit per aggiungere grana digitale)
- **Downsampling: 1** (non modificare il sample rate, solo i bit)
- **Mix: 20–35%**: non full wet, solo una texture di fondo. In Magma Vulcano le sporche hanno una grana digitale quasi impercettibile ma presente

**Limiter:**
- Ceiling: **–1.0 dBFS**

### Routing delle sporche
- Fader: **–10 / –14 dB** (le sporche entrano ed escono, non sono costanti)
- Pan: **centro** o **±8°** massimo
- Output → **Bus 1 (Aux 1)** direttamente
- Riverbero (Aux 3): **0 / nessun send** — le sporche in Magma Vulcano sono secche
- Delay (Aux 4): **0 / nessun send**

---

## AUX 1 — Vocal Group Bus

Tutto il segnale vocale (Audio 2, 3, 5/6) confluisce qui. La catena presente nel template è:

```
Channel EQ  →  DeEsser (s)  →  [CLA-76 o Vintage FET]  →  [Vintage Opto]
```

Il tuo ProjectData mostra la presenza del **CLA-76 (m)** (Waves): verifica se è già caricato su Aux 1. Se sì, usalo al posto del Vintage FET Logic — ha un carattere più fedele all'originale hardware.

---

### Channel EQ su Aux 1 (globale)

Qui EQ-alizzi l'**insieme vocale**: ogni intervento colpisce main, doppie e sporche contemporaneamente.

| Banda | Intervento | Motivazione |
|---|---|---|
| < 80 Hz | **High-Pass 80 Hz, 24 dB/oct** | Seconda linea di taglio del basso sull'insieme |
| 200–400 Hz | **–2 dB, Q = 0.6** | Riduce l'addensamento delle voci che si sommano nelle basse-medie |
| 3–5 kHz | **+1.5 dB, Q ampio (0.3–0.4)** | Presenza e intelligibilità dell'insieme. In Magma Vulcano le voci "tagliano" nel beat |
| 10–12 kHz | **+1 dB shelf** | Aria complessiva — mantieni il sound aerato tipico della produzione di Youngotti |

---

### DeEsser (s) su Aux 1

Seconda linea di de-essing sull'insieme. Cattura ciò che sfugge ai de-esser sui singoli canali, specialmente quando le voci si sommano nella stessa regione di frequenza.

- Frequenza: **7.5–9 kHz**
- Threshold: conservativo — si deve attivare solo sulle sibilanti più evidenti dell'insieme, non continuamente
- Gain Reduction: **–2 / –3 dB** massimo

---

### CLA-76 (m) o Vintage FET Compressor — Compressore 1

Il compressore FET aggredisce i transienti con precisione. Nel sound di Magma Vulcano le voci hanno un "punch" in attacco molto caratteristico: l'1176 è in parte responsabile di questo effetto.

| Parametro | Valore |
|---|---|
| Ratio | **4:1** |
| Attack | **3 ms** |
| Release | **Auto** |
| Gain Reduction | **4–6 dB** in media — il meter deve muoversi costantemente sulle sillabe forti |
| Make-up Gain | Compensa la riduzione |

---

### Vintage Opto Compressor — Compressore 2 (in serie dopo il FET)

L'opto leviga il sustain dopo che il FET ha già attaccato i transienti. Deve essere quasi impercettibile: se lo senti lavorare in modo evidente, è troppo.

| Parametro | Valore |
|---|---|
| Mode | **Peak** |
| Gain Reduction | **2–3 dB** |
| Il suo ruolo | Colla e coesione timbrica del gruppo, non riduzione dinamica |

**A/B test obbligatorio:** bypassa l'intera catena Aux 1 e confronta. Le voci devono suonare più presenti, più coese e più "finite" con la catena attiva, senza che si percepisca il lavoro della compressione.

---

## AUX 3 — Quantec Room Simulator (Riverbero)

Il tuo ProjectData conferma: **Quantec Room Simulator** con preset **"Club.pst"** e presenza del preset **"Magma Lil T"** — quest'ultimo è un preset personalizzato già presente nel template, probabilmente ottimizzato per il sound di questo tipo di produzione.

**Operativamente:**
1. Apri Aux 3 e verifica quale preset è caricato
2. Se "Magma Lil T" è disponibile come preset selezionabile nel Quantec: **usalo come punto di partenza** — è già calibrato
3. Se non è selezionabile come preset del Quantec ma compare solo come nome di regione, usa "Club.pst"

**Parametri da verificare/aggiustare sul preset:**
| Parametro | Valore target per Magma Vulcano |
|---|---|
| Pre-Delay | **15–20 ms** (non troppo lungo: il riverbero deve essere percepito come spazio, non come eco) |
| Decay Time (RT60) | **0.8 – 1.2 secondi** (corto: ambiente club compresso, non sala aperta) |
| Mix sull'Aux | **100% Wet** (l'Aux è già in parallel send, non modificare) |
| Early Reflections | Riduci se il preset è troppo "aperto": Magma Vulcano ha un riverbero denso ma corto |

**Livelli dei send verso Aux 3:**
- Da Audio 2 (main): **–18 / –20 dB**
- Da Audio 3 (doppie): **–24 dB**
- Da Audio 5/6 (sporche): **0 / nessun send**
- Da Aux 1 (se vuoi mandare il bus intero): **–22 dB**

---

## AUX 4 — H-Delay (s) (Delay Stereo)

Il tuo ProjectData conferma **H-Delay (s)** — versione stereo dell'H-Delay Waves — su Aux 4. La cascata con "Club Rev" (il riverbero sull'Inst 1) proietta gli echi in un ambiente tridimensionale.

**Impostazioni operative:**
| Parametro | Valore |
|---|---|
| Mode | **Sync** attivo |
| Note | **1/8 puntato** (a 120 BPM = 562.5 ms) — l'eco rientra nel groove del beat di Youngotti |
| Feedback | **15–20%** (poche ripetizioni, non più di 2–3 echi udibili) |
| High-Cut | **5–6 kHz** (le ripetizioni devono essere significativamente più scure della voce dry) |
| Lo-Fi / Analog | Attivo se disponibile nel preset: aggiunge carattere vintage alle ripetizioni |
| Stereo Width | **60–70%**: le ripetizioni si allargano nel campo stereo, la voce dry rimane al centro |

**Send verso Aux 4:**
- Da Audio 2 (main): **–22 / –24 dB** — il delay è presente ma non evidente
- Da Audio 3 e 5/6: **0 / nessun send**

**Tecnica sidechain (opzionale ma consigliata):**
Inserisci un Compressor su Aux 4 con side-chain triggerato da Audio 2. Ratio massima, threshold basso. Quando la voce parla, il delay viene abbassato automaticamente; emerge solo nelle pause. In Magma Vulcano questo effetto è chiaramente presente nelle pause tra un verso e l'altro.

---

## AUX 2 — Bus Secondario

Il tuo template ha un secondo bus con channel strip Neve / Vintage Console EQ. Il preset **"Magma Lil Tube (s)"** trovato nel ProjectData è probabilmente caricato qui.

**Come usarlo per questo mix:**
Invia le **doppie** (Audio 3) su Aux 2 invece di Aux 1 — o in aggiunta, con send parallelo. Questo permette di processare le doppie con il colore del Neve indipendentemente dalla main.

Oppure usa Aux 2 come **bus del Master vocale** (Aux 1 → Aux 2) per aggiungere un secondo stadio di saturazione armonica sull'intero insieme. Youngotti usa spesso una leggera saturazione armonica sull'insieme vocale per farlo "incollare" sul beat.

---

## SEQUENZA OPERATIVA — In Che Ordine Fare Tutto

Questa è la sequenza esatta da seguire nel progetto, passo dopo passo:

1. **Bilanciamento a fader flat** (nessun plugin attivo): main 0 dB, doppie –7 dB, beat –6 dB. Ascolta 2 minuti e acquisisci la proporzione generale
2. **Audio 2 — Cnsl EQ**: high-pass a 80 Hz, scava a 300 Hz, boost a 3.5 kHz
3. **Audio 2 — Waves Tune**: imposta C minor, retune speed 30 ms. Poi confronta con la voce non corretta
4. **Audio 2 — Compressor**: regola threshold finché vedi 4–6 dB GR sulle sillabe forti
5. **Audio 2 — DeEsser 2**: individua la frequenza delle sibilanti, threshold conservativo
6. **Audio 3 — Waves Tune**: C minor, retune speed 60 ms (più lento del main)
7. **Audio 3 — Doubler4**: imposta delay e detune come indicato. Test in mono immediatamente dopo
8. **Audio 3 — Compressor + Vintage Console EQ**: leggera compressione, high-pass a 120 Hz, scava a 400 Hz
9. **Audio 5 — costruisci catena sporche**: Channel EQ + Vintage FET + Bitcrusher + Limiter
10. **Aux 1 — Channel EQ**: high-pass globale, scava a 300 Hz, boost a 4 kHz
11. **Aux 1 — DeEsser (s)**: threshold sull'insieme
12. **Aux 1 — CLA-76 / Vintage FET → Vintage Opto**: catena bus. A/B test obbligatorio
13. **Aux 3 — Quantec**: verifica preset "Magma Lil T" o "Club.pst", imposta pre-delay a 15–20 ms. Apri i send progressivamente
14. **Aux 4 — H-Delay**: sync 1/8 puntato, feedback 15%, high-cut 5 kHz. Apri send da main progressivamente
15. **A/B comparison con Magma Vulcano**: importa la reference su una traccia separate a –14 LUFS, alterna ogni 30 secondi
16. **Test in mono** (Stereo Out in mono): verifica che le doppie tengano e il main resti al centro
17. **Bounce rough mix**: WAV 24 bit 48 kHz, peak non oltre –3 dBFS

---

## Reference Sonora — Cosa Ascoltare in Magma Vulcano

Quando fai A/B con il reference, concentrati su questi elementi specifici:

- **0:00–0:20 (intro):** come il riverbero avvolge la voce senza coprire il beat — misura il decay con l'orecchio
- **0:30–1:00 (verso principale):** l'Auto-Tune si sente nelle note lunghe, specialmente sulle vocali "a" e "o". Calibra il tuo Waves Tune su questa sezione
- **1:00–1:20 (ritornello):** le doppie entrano — ascolta dove si collocano nel campo stereo e quanto sono basse nel mix rispetto al main
- **Pause tra i versi:** il delay emerge nelle pause. Se nel tuo mix non percepisci questo effetto, abbassa il threshold del sidechain o aumenta il send verso Aux 4

---

*Guida operativa generata da Claude · SEH × Magma Vulcano · Mix Engineer Workspace · Maggio 2026*
