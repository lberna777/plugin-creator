# VOCAL FORGE — revisione tecnica ostile

Branch: `claude/vocal-chain-plugin-hlh15v` · revisione fatta leggendo il codice e **misurando il binario vero**
(gli oggetti già compilati di `vocalforge_selftest` linkati a piccoli programmi di prova fuori dal repo,
più Valgrind). Nessun file del progetto è stato modificato.

---

## Stato delle correzioni (aggiornato dopo l'intervento)

Tutti i rilievi B1–B12 sono stati corretti, e ognuno ha ora un controllo in
`plugin/tools/selftest_main.cpp` che **fallisce se il difetto torna**. Prova fatta: reintroducendo il solo
B1 il binario di test muore con `free(): invalid next size` invece di passare.

| # | difetto | correzione | controllo che lo prende |
|---|---|---|---|
| B1 | scrittura fuori dal buffer dell'host | la lunghezza vera del blocco passa esplicitamente a tutti gli stadi | blocchi di lunghezza casuale 1…512 |
| B2 | de-esser allpass, chiamato due volte | crossover a due uscite, **una** passata per campione, detector sulla banda alta vera | risposta misurata a 200 Hz vs 9 kHz |
| B3 | allocazioni sul thread audio | biquad a coefficienti propri + puntatori ai parametri risolti nel costruttore | contatore di `operator new` durante `processBlock` |
| B4 | limiter non era un tetto | mandate e output gain **prima**, limiter ultimo stadio | ceiling −1 dBFS con output +12 e riverbero acceso |
| B5 | ducking di delay e doubler morto | un solo detector per campione, condiviso dai tre bus | livello del delay con e senza duck, riverbero spento |
| B6 | polarità + mix = silenzio | il dry del mix si copia **dopo** trim e polarità | polarità invertita con mix al 50 % |
| B7 | latenza dichiarata inesistente | dichiarata zero, com'è il DSP | latenza misurata con impulso == dichiarata |
| B8 | drive diverso fra L e R | il drive si legge una volta per campione, fuori dal ciclo dei canali | ingresso mono deve restare mono durante la rampa |
| B9 | delay sempre 1/8 puntato | `dlyDivision` letto e mappato in battute | posizione dell'eco per 1/4 vs 1/8 puntato |
| B10 | sei parametri inerti | `intensity` rimosso, predelay collegato, varianti non più automatizzabili | ogni parametro deve cambiare il suono |
| B11 | `satTilt` non era un tilt | due shelf speculari a 700 Hz, attive anche a drive 0 | coperto dal controllo "nessun parametro finto" |
| B12 | etichette dei knob sfalsate | ogni controllo porta la sua etichetta, non ricostruita per tipo | — (visivo) |

I **RISCHI** elencati più sotto restano aperti e sono ancora validi come lettura.

---

## Verdetto

1. **Il plugin corrompe la memoria appena Logic gli passa un buffer più corto di quello dichiarato** in
   `prepareToPlay` — cioè quasi subito. `processSends` lavora su `dry.getNumSamples()` (= *maximumBlockSize*)
   invece che sul blocco reale, e scrive fuori dal buffer del DAW. Valgrind lo conferma con 6 righe di repro.
2. **I due de-esser fanno il contrario di un de-esser.** Il `LinkwitzRileyFilter` è impostato su `allpass`:
   misurato sul binario, il DS1 abbassa il corpo della voce di **-5,3 dB a 200 Hz** e **alza le sibilanti di
   +0,9 dB a 9 kHz**. Il DS2 in modo "wide" è un compressore a banda larga con 0,5 ms di attacco.
3. **Il limiter non è un tetto** (l'output gain e le tre mandate stanno *dopo*: misurato **+8,9 dBFS** in uscita
   con ceiling a -1 dBFS) e **il thread audio alloca**: 3 malloc per blocco sempre, 67 a ogni movimento di
   automazione — con in testata scritto "zero allocazioni in processBlock".
4. Contorno: latenza dichiarata di 2 ms che non esiste (Logic sposterà la traccia di 96 campioni), ducking di
   delay/doubler morto se il riverbero è spento (9 dB di salto misurati), polarità + mix 50 % = **silenzio**.
5. Il self-test passa tutto e non vede niente di tutto questo, perché usa sempre blocchi di dimensione fissa
   e non misura mai una risposta in frequenza.

---

## BUG

### B1 — `processSends` usa la lunghezza dello scratch, non quella del blocco: scrittura fuori dai limiti
`plugin/source/ChainDsp.cpp:382` (con `ChainDsp.cpp:629` che gli passa `dryScratch`)

```cpp
void ChainDsp::processSends (const juce::AudioBuffer<float>& dry, ...)
{
    const auto numSamples = dry.getNumSamples();   // <-- questo è maximumBlockSize, NON il blocco corrente
```

`dryScratch` è dimensionato una volta in `prepare` a `maximumBlockSize` (`ChainDsp.cpp:95`). In `process`
viene riempito solo per i primi `numSamples` campioni (`ChainDsp.cpp:628`) e poi passato a `processSends`,
che però cicla su *tutta* la sua lunghezza e fa `destination.addSample(ch, sample, ...)` fino a
`maximumBlockSize`. Se il blocco reale è più corto, si scrive oltre la fine del buffer del DAW.

**Come riprodurlo** (fatto):
```cpp
VocalForgeProcessor p;
p.prepareToPlay (48000.0, 512);        // il DAW dichiara 512
juce::AudioBuffer<float> b (2, 64);    // ...e poi consegna 64 campioni
p.processBlock (b, midi);
```
Valgrind: `Invalid write of size 4 ... in vf::ChainDsp::processSends`, indirizzo `0 bytes after a block`.
Con render veri: `prep=512 run=64` → SIGSEGV; `prep=512 run=128` → SIGSEGV; `prep=128 run=64` → *double free
or corruption* alla chiusura. Il caso opposto (blocco più lungo del massimo dichiarato) esplode invece nel
`memmove` di `dryScratch.copyFrom` a `ChainDsp.cpp:515`, sempre confermato da Valgrind.

**Perché è sbagliato**: il contratto di `processBlock` è "≤ maximumBlockSize", non "= maximumBlockSize".
Logic consegna slice parziali all'avvio del transport, ai confini di ciclo, in bounce e in freeze; la
Standalone lo fa a ogni cambio di device. È corruzione dell'heap, quindi crash a caso *anche molto dopo*,
in codice che non c'entra nulla.

**Correzione**: passare esplicitamente `numSamples` a `processSends` (o usare
`destination.getNumSamples()`), e in `process` clampare comunque tutte le copie su
`jmin (numSamples, dryScratch.getNumSamples())`. Aggiungere al self-test un giro con blocchi
di lunghezza variabile e casuale: è l'unico modo perché non ritorni.

---

### B2 — Il de-esser è un allpass: attenua la voce e lascia (o alza) le esse
`plugin/source/ChainDsp.cpp:17`, `ChainDsp.cpp:280-314`

`DeEsser::prepare` fa `splitter.setType (LinkwitzRileyFilterType::allpass)`. In JUCE
(`juce_LinkwitzRileyFilter.cpp:106`) il ramo allpass restituisce `yL - R2*yB + yH`, cioè un **passa-tutto**:
modulo unitario a ogni frequenza. Conseguenze, entrambe misurate:

* **Detector cieco** (`ChainDsp.cpp:293`): `highBand` è il segnale intero, non la banda alta. Il de-esser
  scatta su qualunque sillaba forte, non sulle sibilanti.
* **Percorso "split" invertito** (`ChainDsp.cpp:308-309`): `out = in - high*(1-g)`; siccome
  `AP = in - 2√2·BP`, l'uscita vale `g·in + (1-g)·2√2·BP`. Cioè: **abbassa tutto e rialza proprio la banda
  del de-esser**.

**Misura sul binario** (solo DS1 acceso, valori di default: 6650 Hz, soglia -20 dB, range 6 dB; sinusoidi a
-12 dBFS, rapporto RMS uscita/ingresso):

| 200 Hz | 800 Hz | 3000 Hz | 6650 Hz | 9000 Hz |
|---|---|---|---|---|
| **-5,33 dB** | -5,18 dB | -3,49 dB | -0,25 dB | **+0,90 dB** |

DS2 (modo "wide", che è il default di `ds2Mode`): -2,54 dB a 200 Hz e -2,55 dB a 6650 Hz. Identici: è un
compressore a banda larga con attacco 0,5 ms e rilascio 40 ms, cioè un modulatore d'ampiezza che segue la
forma d'onda su un fondamentale di 140 Hz. Su una voce vera si sente come distorsione ruvida e pompaggio.

**In più, il filtro viene chiamato due volte sullo stesso campione**: una nel ciclo del detector
(`ChainDsp.cpp:293`) e una nel ciclo split (`ChainDsp.cpp:308`). `LinkwitzRileyFilter::processSample` aggiorna
`s1..s4` a ogni chiamata: lo stato avanza al doppio della velocità, quindi la frequenza di taglio effettiva
non è quella impostata. Simulando esattamente la matematica JUCE, con 6 dB di riduzione richiesti la curva si
sposta di circa un'ottava (il picco di +3,5 dB passa da 6,65 kHz a 12 kHz). Nota che DS2 in modo "wide" fa
*una* chiamata sola: i due de-esser hanno quindi filtri tarati diversamente pur avendo la stessa frequenza.

**Correzione**: usare `setType(highpass)` e la overload a due uscite
`processSample (ch, in, outLow, outHigh)`, **una sola chiamata per campione per canale**, riusando i due
risultati sia per il detector sia per la ricomposizione (`out = low + high*gain`). Il detector deve essere
mono e calcolato *prima*, da una copia, non consumando lo stato del filtro di ricomposizione — servono due
filtri distinti (uno per il detector mono, uno per canale) oppure un solo passaggio con `outHigh` riusato.

---

### B3 — Il thread audio alloca: 3 malloc per blocco sempre, 67 a ogni movimento di parametro
`plugin/source/ChainDsp.cpp:266` · `plugin/source/PluginProcessor.cpp:232-301` e `PluginProcessor.cpp:314-315`

`ChainDsp.h:12` promette "zero allocazioni in processBlock". Misurato sostituendo `operator new` e contando:

```
blocco senza cambi di parametro:            3 allocazioni
blocco dopo il movimento di UN parametro:  67 allocazioni
```

Backtrace delle 3 fisse: `ChainDsp::processRoomTamer` → `IIR::Coefficients<float>::makePeakFilter` →
`operator new`. `juce::dsp::IIR::Coefficients` è un `ReferenceCountedObject`: **ogni banda del room tamer
alloca un oggetto nuovo a ogni blocco** (`ChainDsp.cpp:266`) e libera il precedente. A 64 campioni/48 kHz sono
2250 malloc/free al secondo sul thread audio, con il room tamer acceso di default.

Le altre 64 arrivano da `currentSettings()`, chiamato dentro `processBlock` (`PluginProcessor.cpp:315`):
costruisce 27 `juce::String` temporanee (`"room" + b + "Freq"` ecc., righe 252-261) e fa ~90 lookup per
stringa in `getRawParameterValue`, più i 7 `makePeakFilter`/`makeHighShelf` di `updateCoefficients`. Ogni
`juce::String` è un'allocazione (JUCE non ha short-string optimization).

**Perché è sbagliato**: malloc prende un lock globale. Su una traccia con automazione attiva questo è il
percorso normale, non l'eccezione: inversione di priorità e dropout, esattamente quello che il commento in
testa al file dichiara di evitare.

**Correzione**: (a) precalcolare in `prepare` i puntatori ai `std::atomic<float>` dei parametri (una sola
volta, con gli id costruiti lì), così `currentSettings()` diventa solo letture atomiche; (b) chiamare
`setSettings` dal listener dei parametri, non dal thread audio, scrivendo in una struttura double-buffered;
(c) per i filtri, usare coefficienti calcolati a mano su un `std::array<float,6>` e `IIR::Filter::coefficients`
preallocato, oppure passare a `StateVariableTPTFilter` (che non alloca) anche per le campane.

---

### B4 — Il limiter non limita: output gain e mandate sono a valle
`plugin/source/ChainDsp.cpp:600-630`

L'ordine in `process` è: limiter (riga 605) → mix + `outputGain` (righe 612-622) → mandate sommate al
risultato (righe 625-630). Quindi il "ceiling" non è un ceiling.

**Misure** (tutti i moduli spenti tranne quello in prova, sinusoide 300 Hz a -3 dBFS):
* `limOn=1`, `limCeiling=-1 dBFS`, `outGain=+12 dB` → **picco d'uscita +8,95 dBFS**.
* `limCeiling=-6 dBFS`, riverbero a -6 dB e delay a -8 dB con feedback 45 % → **picco d'uscita -0,87 dBFS**.

Nello stesso punto c'è un secondo problema: **il Mix non tocca le mandate**. Con `mix = 0 %` (catena
completamente bypassata in parallelo) riverbero, delay e doubler continuano a suonare a pieno livello.

**Correzione**: sommare le mandate *prima* del limiter (è la posizione giusta anche musicalmente: la coda
deve essere limitata insieme alla voce), applicare `outputGain` prima del limiter o spostare il limiter in
ultima posizione assoluta, e moltiplicare i send gain per `mixAmount`.

---

### B5 — Il ducking di delay e doubler è alimentato da un inviluppo che nessuno aggiorna
`plugin/source/ChainDsp.cpp:415` (riverbero, unico che lo aggiorna) · `ChainDsp.cpp:440` (delay) ·
`ChainDsp.cpp:470` (doubler)

`duckEnv` è un solo `Envelope` condiviso. Solo il ramo riverbero chiama `duckEnv.process(...)`. Il delay
legge `duckEnv.value` e per giunta calcola `dryLevel` per poi buttarlo via con `juce::ignoreUnused (dryLevel)`
(riga 443). Il doubler idem. Due conseguenze:

* **Riverbero spento → nessun ducking**, e i bus saltano su di colpo. Misurato: livello del solo bus delay
  (differenza fra render con e senza delay), `dlyDuck = 9 dB`:
  `riverbero ON → -40,5 dBFS` · `riverbero OFF → -31,5 dBFS`. Nove dB esatti, cioè tutto il ducking.
* **Riverbero acceso → un solo valore di duck per blocco**, quello dell'ultimo campione del ciclo del
  riverbero, applicato retroattivamente a tutto il blocco del delay. Il comportamento dipende quindi dalla
  dimensione del buffer del DAW.

**Correzione**: calcolare il detector di duck una volta sola all'inizio di `processSends`, per campione, in un
array preallocato (o tenere tre inviluppi separati, uno per bus, ciascuno aggiornato nel proprio ciclo).

---

### B6 — Polarità + Mix < 100 % = cancellazione totale
`plugin/source/ChainDsp.cpp:514-515` (dry copiato **prima** del trim) vs `ChainDsp.cpp:198`
(`trimGain` contiene il segno della polarità)

Il dry per il mix parallelo viene salvato prima dello stadio 1, mentre la polarità è cotta dentro
`trimGain`. Quindi con la polarità invertita il wet è in opposizione di fase col dry.

**Misura** (tutti i moduli spenti, sinusoide 300 Hz):

| | polarity OFF | polarity ON |
|---|---|---|
| mix 100 % | -10,97 dBFS | -10,97 dBFS |
| mix 50 %  | -10,97 dBFS | **-100 dBFS (silenzio)** |

Stesso punto: il dry non passa nemmeno dall'`inTrim`. Con `mix = 50 %` e `inTrim = +12 dB` l'uscita misura
-3,04 dBFS, cioè esattamente `0,5·g + 0,5` invece del `+12 dB` che l'utente si aspetta dal trim d'ingresso.

**Correzione**: copiare `dryScratch` **dopo** trim e polarità (spostare le righe 514-515 sotto il ciclo
517-523). Il trim d'ingresso è gain staging: deve stare davanti a tutto, dry compreso.

---

### B7 — Latenza dichiarata di 2 ms che il DSP non ha
`plugin/source/PluginProcessor.cpp:228-229` e `PluginProcessor.cpp:473-474`

`setLatencySamples (sampleRate * latencyMs * 0.001)` con `latencyMs` di default 2,0 e commento
«solo il lookahead del limiter». `juce::dsp::Limiter` **non ha lookahead**: è un limiter a ballistics,
latenza zero. Nessun altro stadio ritarda.

**Misura**: impulso a 48 kHz, tutti i moduli spenti → esce al campione 100 (entrato al 100), latenza reale
**0 campioni**, latenza dichiarata **96**.

**Perché è sbagliato**: Logic compensa PDC anticipando la traccia di 96 campioni. La voce finisce 2 ms
*avanti* rispetto al resto; se la si raddoppia con un'altra istanza o si fa parallel compression su un bus,
si ottiene comb filtering. `processBlockBypassed` (`PluginProcessor.cpp:326-331`) non ritarda nulla, quindi
il bypass sposta l'audio di 2 ms rispetto al non-bypass.

**Correzione**: dichiarare 0 finché non c'è vero lookahead; se si vuole un limiter con lookahead, aggiungerlo
davvero e ritardare anche il percorso di `processBlockBypassed` della stessa quantità.

---

### B8 — La saturazione consuma lo smoothing del drive per canale: L e R diversi
`plugin/source/ChainDsp.cpp:355-376`

`driveAmount.getNextValue()` sta dentro il ciclo dei campioni, **annidato nel ciclo dei canali**. Il canale 0
consuma `numSamples` valori della rampa, il canale 1 riparte da dove ha finito il canale 0. Durante ogni
rampa (20 ms, cioè a ogni FORGE e a ogni movimento del drive) i due canali ricevono drive diversi.

**Misura**: ingresso identico su L e R, `satDrive` da 0 a 100 → **differenza L-R fino a -21,6 dBFS**.
Un ingresso mono esce non-mono, e l'immagine stereo si sposta a ogni cambio.

Effetto secondario: la rampa si esaurisce in `20 ms / numCanali` invece che in 20 ms.

**Correzione**: leggere il drive una volta per campione fuori dal ciclo dei canali (come già fanno
correttamente `trimGain` a riga 520 e `mixAmount`/`outputGain` a righe 614-615), oppure `skip` esplicito.
`driveAmount.skip (0)` a riga 376 non fa nulla: `SmoothedValue::skip(0)` con `countdown > 0` è un no-op
(`juce_SmoothedValue.h:330`).

---

### B9 — In sync il delay è sempre 1/8 puntato, qualunque divisione chieda il preset
`plugin/source/ChainDsp.cpp:428-429` vs `tools/data/rules.json` (`dly_quarter`)

```cpp
if (settings.dlySync && bpm > 0.0)
    delayMs = static_cast<float> (60000.0 / bpm * 0.75);   // 1/8 puntato di default
```

Il parametro `dlyDivision` (`PluginProcessor.cpp:155`, valori `1/4`, `1/8 dotted`, `1/8`, `1/16`) viene
scritto da `writePresetToParameters` (riga 420) ma **non è mai letto** da `currentSettings()`. La regola
`dly_quarter` in `rules.json` chiede `"division": "1/4"` e il pannello "PERCHÉ" lo mostra all'utente: il
plugin suona un puntato. Prompt che la attivano: `brightness > 0.25 and density > 0.5` (es. "pop moderno,
voce brillante e presente").

**Correzione**: leggere `dlyDivision` in `currentSettings()` e mappare 1/4 = 1,0 · 1/8 dotted = 0,75 ·
1/8 = 0,5 · 1/16 = 0,25 battute.

---

### B10 — Sei parametri automatizzabili che non arrivano mai al DSP
`plugin/source/PluginProcessor.cpp`

| parametro | dichiarato | letto da `currentSettings()` | usato in `ChainDsp` |
|---|---|---|---|
| `intensity` | riga 45 | **no** | no |
| `revPredelay` | riga 143 | riga 286 | **no** (`preDelayScratch` è allocato e mai usato) |
| `revVariant` | riga 141 | **no** | no (l'algoritmo del riverbero non cambia mai) |
| `dlyVariant` | riga 152 | **no** | no |
| `fxVariant` | riga 163 | **no** | no |
| `dlyDivision` | riga 155 | **no** | no (vedi B9) |

Sono knob visibili, automatizzabili e salvati nello stato, che non fanno niente. `revVariant` in particolare
promette cinque riverberi diversi (`ambience/room/hall/plate`) mentre `juce::dsp::Reverb` riceve sempre gli
stessi tre parametri (`ChainDsp.cpp:188-196`); "plate" e "hall" differiscono solo per decay e size.

**Correzione**: o li si collega, o li si toglie dal layout. Un parametro finto in un plugin che si vende
sulla motivazione ("ogni valore ha una spiegazione") è peggio di un parametro mancante.

---

### B11 — `satTilt` non è un tilt, ed è inerte a drive 0
`plugin/source/ChainDsp.cpp:353` e `ChainDsp.cpp:373`

`tiltGain = decibelsToGain (satTilt * 0.5f)` viene moltiplicato solo per il segnale **wet**
(`jmap (drive, data[sample], wet * tiltGain)`). Non è un'inclinazione spettrale: è un guadagno piatto sul
solo percorso saturo, dimezzato rispetto al valore mostrato in dB, e a `satDrive = 0` non ha alcun effetto
perché `jmap(0, dry, ...)` restituisce il dry.

**Correzione**: implementare un vero tilt (low shelf e high shelf speculari attorno a ~700 Hz) applicato
dopo il waveshaper, oppure rinominare il parametro in "Sat Output".

---

### B12 — Le etichette dei knob sono sfalsate su tutti i moduli che hanno un menu
`plugin/source/PluginEditor.cpp:522-525` (con `PluginEditor.cpp:259-302`)

`rebuildControls` riempie `sliderLabels` **nell'ordine dei parametri** (i `bool` non producono etichetta).
`resized()` invece consuma le etichette prima per tutte le combo e poi per tutti gli slider, con un unico
`labelIndex`:

```cpp
for (auto* combo : combos)   place (*combo, sliderLabels[labelIndex++]);
for (auto* slider : sliders) place (*slider, sliderLabels[labelIndex++]);
```

**Come riprodurlo**: selezionare DE-ESS 1. I parametri sono `ds1On` (toggle), `ds1Freq`, `ds1Thresh`,
`ds1Range` (slider), `ds1Mode` (combo). Il menu si prende l'etichetta "DS1 Freq"; il knob della frequenza
diventa "DS1 Threshold", quello della soglia "DS1 Range", quello del range "DS1 Mode". Stesso problema su
HPF (`hpfFreq`/`hpfSlope`), SAT (`satDrive`/`satType`/`satTilt`) e DE-ESS 2.

**Correzione**: tenere una lista parallela `{componente, etichetta}` costruita in `rebuildControls` e
posizionarla in ordine, invece di ricostruire l'accoppiamento per tipo in `resized()`.

---

## RISCHI

### R1 — Il room tamer decide guardando tutto il blocco: l'ascolto e il bounce non coincidono
`plugin/source/ChainDsp.cpp:242-278`

Per ogni banda: prima si percorre **tutto il blocco** per calcolare `bandPeak` (righe 249-256), poi si
ricalcolano i coefficienti una volta sola e si filtra il blocco intero (righe 266-273). Il notch applicato ai
primi campioni dipende quindi da energia che arriva *dopo* di loro: un comportamento non causale dentro il
blocco, e quindi dipendente dalla dimensione del buffer.

**Misura**: stesso ingresso, stesso preset, blocchi da 64 e da 1024 campioni → differenza di picco
**-34,9 dBFS**. Non è enorme ma è udibile su una coda silenziosa, e soprattutto significa che il bounce
offline di Logic non è bit-identico a quello che si è ascoltato.

Secondo problema nello stesso ciclo: le tre bande sono in cascata, ma il detector della banda 2 legge il
buffer **già filtrato** dalla banda 1 (`buffer` viene modificato in place a riga 273 prima del giro
successivo). Con notch sovrapposti a Q 5-6 e frequenze 92/184/246 Hz, l'interazione è reale.

Terzo: `roomFilters` è `std::array<Filter,2>` e il ciclo è limitato a `jmin (2, channels)`; i coefficienti
vengono riassegnati a ogni blocco mentre lo stato del filtro persiste — con Q alto è un salto di coefficienti
non smussato (zipper) a ogni buffer.

**Correzione**: detector e filtro nello stesso ciclo per campione, con il guadagno del notch aggiornato per
campione via `SmoothedValue` (o interpolando i coefficienti sul blocco); e detector calcolato sul segnale
d'ingresso dello stadio, uguale per tutte e tre le bande.

### R2 — `ChainDsp` non è sicuro con più di 2 canali
`ChainDsp.h:167-168` (`std::array<Filter,2>`), `ChainDsp.cpp:547`, `578`, `586` (`jmin (2, channels)`)

Oggi `isBusesLayoutSupported` (`PluginProcessor.cpp:208-218`) accetta solo mono e stereo, quindi non esplode.
Ma la classe è scritta come se fosse generica: gate, de-esser, compressori e saturazione ciclano su
`buffer.getNumChannels()`, mentre EQ, room tamer e air si fermano a 2. Se qualcuno domani aggiunge un layout
surround o una sidechain, i canali oltre il secondo escono con un timbro diverso e
`deEsser.splitter.processSample (ch, ...)` indicizza `s1[(size_t) ch]` oltre la fine del vettore (in Release
non c'è controllo: `juce_LinkwitzRileyFilter.cpp:98`). Trappola latente, non bug attuale.

### R3 — `setLatencySamples` chiamato ad audio in corsa da `applyPrompt`
`plugin/source/PluginProcessor.cpp:473-474`

Premere FORGE mentre il transport gira cambia la latenza dichiarata. Logic ricostruisce il grafo PDC: buco
audio. E siccome la latenza vera è comunque 0 (B7), il cambio non serve a nulla.

### R4 — 60+ `setValueNotifyingHost` in raffica
`plugin/source/PluginProcessor.cpp:334-459`

Verificato che il thread è quello giusto (i listener APVTS sono chiamati **sincronamente** da
`ParameterAdapter::parameterValueChanged`, `juce_AudioProcessorValueTreeState.cpp:148-157`), quindi la
guardia `writingPreset` funziona e `touchedByHand` non si sporca. Restano due cose:
* con la traccia in Latch/Touch/Write, Logic registra **tutti** i parametri come automazione in un colpo solo;
* nessuno di questi valori è smussato nel DSP. Makeup dei compressori, depth dei notch, guadagni delle
  campane e soglie saltano di colpo al primo blocco successivo: coefficienti IIR nuovi su stato vecchio.
  Il self-test misura `maxJump` su un cambio di prompt e passa, ma lo fa su un segnale già gentile.

**Correzione**: `beginChangeGesture`/`endChangeGesture` attorno al blocco, e `SmoothedValue` (o interpolazione
dei coefficienti) almeno per makeup, guadagni EQ e depth dei notch.

### R5 — `onPresetGenerated` invocato da `setStateInformation`
`plugin/source/PluginProcessor.cpp:537` → `PluginEditor.cpp:204` → `refreshFromPreset()`

`refreshFromPreset` tocca `juce::Label`, `juce::TextEditor` e chiama `repaint()`. `setStateInformation` non ha
garanzie di thread: in diversi host (e in auval) arriva fuori dal message thread. Manca un
`MessageManagerLock` o, meglio, un `AsyncUpdater`. Nello stesso punto, `lastPreset = rulesEngine.compile(...)`
(riga 532) fa un parse completo delle regole dentro `setStateInformation`: lento e allocante, ma almeno non
sul thread audio.

### R6 — Il valutatore di espressioni fallisce in silenzio, e `not` ha la precedenza sbagliata
`plugin/source/RulesEngine.cpp:99`, `174-181`, `663`, `700`

* `evaluate()` viene chiamato quasi ovunque **senza** il flag `ok` (righe 657, 663, 700, 738). Un errore
  (variabile inesistente, parentesi non chiusa, funzione sconosciuta) restituisce `0.0` in silenzio. A riga
  700 questo significa **modulo disattivato senza che nessuno lo dica**. Il `jassert (! p.failed)` di riga 179
  sparisce in Release.
* `not` è dentro `parseUnary` (riga 99), quindi lega **più stretto** di `*`, `+` e dei confronti. In Python
  lega più lasco dei confronti. `not brightness > 0.5` qui vale `(not brightness) > 0.5`, in
  `chain_compiler.py` vale `not (brightness > 0.5)`. Oggi nessuna regola in `rules.json` usa `not` (verificato
  con una scansione di tutte le stringhe), quindi il test di parità passa: è una mina, non un incendio.
* `parseProduct` (riga 90) divide senza guardia: una regola con divisore nullo produce `inf`/`nan`, che
  `setValueNotifyingHost` → `convertTo0to1` propaga fino al DSP.

**Correzione**: propagare `ok` e mostrare l'errore nel pannello "PERCHÉ" o negli `warnings` del preset;
spostare `not` sopra `parseCompare`; guardia sulla divisione e `jassert`/clamp su non-finiti prima di
scrivere i parametri.

### R7 — Cambi di stato dei bus senza rampa
`plugin/source/ChainDsp.cpp:390`, `425`, `458`

`if (settings.revOn)` ecc. accendono e spengono il ramo di colpo: la coda del riverbero viene troncata
istantaneamente. Idem per `delayLine.setDelay` (riga 432), che in JUCE cambia il ritardo senza interpolazione:
un'automazione di tempo o un cambio di BPM produce un click/salto di intonazione. `getTailLengthSeconds()`
(`PluginProcessor.h:32`) dichiara 4 s, ma con decay 4 s + delay 1,5 s al 45 % di feedback la coda vera è più
lunga: Logic taglia la fine in bounce.

### R8 — Il gate può congelarsi a metà transizione
`plugin/source/ChainDsp.cpp:229`

```cpp
const auto targetDb = (levelDb > openDb) ? 0.0f : (levelDb < closeDb ? -rangeDb : gateGainDb);
```

Nella banda di isteresi il target è il valore corrente: il guadagno si blocca dove si trovava, anche a metà
apertura. Su un fiato o una coda di riverbero che stazionano fra `thresh-6` e `thresh` il gate resta a -4 o
-7 dB indefinitamente. Non è un crash, ma non è quello che l'utente si aspetta da un'isteresi (che dovrebbe
tenere il gate *aperto*, non congelarne il guadagno).

---

## DEBITO

* `ChainDsp.cpp:376` — `driveAmount.skip (0)` è un no-op (`juce_SmoothedValue.h:330-341`). Codice morto che
  suggerisce un'intenzione mai realizzata.
* `ChainDsp.h:186` — `dlyScratch` e `preDelayScratch` sono dimensionati in `prepare` (`ChainDsp.cpp:96-98`) e
  **mai usati**. `preDelayScratch` costa `sampleRate*0.1 + blockSize` campioni per canale di memoria inutile,
  ed è il fantasma del predelay mai implementato (B10).
* `PluginProcessor.h:20` / `PluginProcessor.cpp:220` — `bool isBusesLayoutSupported (const BusesProperties&) const`
  non esiste come virtuale in `AudioProcessor`: è un overload morto che non viene mai chiamato.
* `PluginProcessor.cpp:369-370` — ternario con i due rami identici:
  `const auto id = (param.id == "ds1Mode" || ...) ? param.id : param.id;`
* `ChainDsp.cpp:280-282` — `processDeEsser` riceve `freq` e lo scarta con `ignoreUnused`; la frequenza vera
  arriva da `updateCoefficients` (riga 175-176). Due sorgenti di verità per lo stesso valore.
* `ChainDsp.cpp:634` — `outLufs` è l'RMS **del solo canale 0** del blocco corrente, senza K-weighting, senza
  gating, con l'offset -0,691 dB copiato da BS.1770 come se bastasse. Non è LUFS. Ed è morto: nessuno in
  `PluginEditor.cpp` legge `outLufs`.
* `ChainDsp.cpp:607` — il meter del limiter confronta la magnitudine prima/dopo sull'intero blocco: legge
  qualunque variazione, non la gain reduction.
* Tutto il file usa `buffer.getSample`/`setSample` per campione (es. righe 185-198, 293-311, 343-344) invece
  dei write pointer già presi altrove: una doppia indirezione per campione per canale, moltiplicata per 13
  stadi.
* `tools/tests/test_gain_staging.py` stampa `nan` nella colonna `satEx` quando `drive == 0` (righe
  "podcast..." e "cantautore acustico..."). È solo la tabella, non il preset — ma un `nan` in un report di
  gain staging è esattamente il tipo di cosa che si smette di guardare.

---

## Controllato e sano

Cose che sembravano sbagliate e non lo sono, o che ho verificato apposta:

* **La formula del ginocchio del compressore è corretta** (`ChainDsp.cpp:28-37`). `-(1-1/R)·(over+K/2)²/(2K)`
  nella regione `|over| ≤ K/2` è la formula standard, ed è continua con il tratto duro: a `over = K/2`
  entrambe danno `-(1-1/R)·K/2`, a `over = -K/2` entrambe danno 0. Nessun salto.
* **Il detector del compressore** è un picco su tutti i canali (`ChainDsp.cpp:297-298`): scelta discutibile
  per un compressore lento da 30 ms, ma non è un bug, ed è link stereo corretto (un solo guadagno per
  entrambi i canali).
* **`eat()` funziona** (`RulesEngine.cpp:26-37`): il controllo di confine impedisce davvero di mangiare "or"
  dentro "orange", e il clamp dell'indice `after > text.length()-1` è neutralizzato dalla condizione
  `after < text.length()` nello stesso `&&`. Ho anche verificato che un input malformato (`"@@@"`, `"("`,
  `"+"` ripetuti) **non manda in loop** il parser: `parsePrimary` avanza di un carattere a riga 126.
* **`fxHp`/`fxLp` NON sono condivisi col percorso del delay**: sono oggetti distinti da `dlyHp`/`dlyLp`
  (`ChainDsp.h:183`), preparati sullo stesso spec ma con stato proprio. Il sospetto era fondato, il codice no.
* **La copia mono→stereo di `processBlock`** (`PluginProcessor.cpp:311-312`) non può produrre l'indice `-1`:
  `isBusesLayoutSupported` garantisce `in ∈ {mono, stereo}`, quindi `totalIn ≥ 1` e
  `jmin (ch-1, totalIn-1) ≥ 0`. Il caso `totalIn == 0` non è raggiungibile con i layout dichiarati.
* **I listener APVTS sono sincroni** (`juce_AudioProcessorValueTreeState.cpp:148-157`): la guardia
  `writingPreset` attorno a `writePresetToParameters` funziona davvero, il badge "MODIFICATO A MANO" non si
  accende da solo dopo un FORGE. `LockedListeners` prende un `CriticalSection`, ma non viene mai attraversato
  da `processBlock`: nessun lock sul percorso audio.
* **`ScopedNoDenormals`** è presente sia in `ChainDsp::process` (riga 506) sia in `processBlock` (riga 305).
* **Nessun NaN/Inf** in uscita a 44,1/48/96 kHz, mono e stereo (self-test, riverificato).
* **Null test a moduli spenti**: sotto -120 dBFS, corretto.
* **Stato salvato e riletto**: prompt e parametri tornano identici (self-test); `promptState` ha il tipo
  `"vfPrompt"` (`PluginProcessor.h:71`) coerente con `getChildWithName ("vfPrompt")` (riga 525).
* **Parità compilatore Python ↔ `RulesEngine.cpp`**: `vocalforge_parity tools/data/rules.json examples` →
  18 preset, 1668 valori confrontati, **0 differenze**. `test_chain.py`, `test_lexicon.py`, `test_artists.py`
  passano tutti.
* **Il doubler in mono** (`ChainDsp.cpp:495-498`) non scrive sul canale 1 inesistente: il ramo `channels >= 2`
  è guardato correttamente.

---

## Secondo giro

Stessa regola del primo: **misurato sul binario**, non dedotto. Gli oggetti già compilati di
`vocalforge_selftest` linkati a dieci programmi di prova fuori dal repo, più Valgrind. Nessun file del
progetto è stato modificato. `vocalforge_selftest` passa tutti i controlli (0 problemi): quello che segue
è ciò che passa **nonostante** i controlli.

## Correzioni verificate

### Chiusi davvero (7 su 12)

**B2 — de-esser.** Chiuso. Risposta misurata con il solo DS1 (6650 Hz), rapporto RMS uscita/ingresso su
sinusoide, a regime:

| | 100 Hz | 200 | 500 | 1k | 3k | 5k | 6650 | 9k | 14k |
|---|---|---|---|---|---|---|---|---|---|
| split, **senza** riduzione | 0,00 | -0,00 | -0,00 | 0,00 | -0,00 | -0,00 | 0,00 | 0,00 | 0,00 |
| split, con riduzione | 0,00 | -0,00 | -0,00 | 0,00 | -0,00 | -0,61 | **-2,49** | **-4,50** | **-5,86** |
| wide, con riduzione | 0,00 | -0,00 | -0,00 | 0,00 | -0,00 | -3,18 | **-6,00** | **-6,00** | **-6,00** |

La riga "senza riduzione" è la prova che serviva: `low + high*gain` con `gain = 1` restituisce **0,00 dB a
ogni frequenza**, quindi la ricomposizione LR4 è a fase corretta. Una sola passata del crossover per
campione e per canale. Resta un difetto minore sul detector (R9).

**B3 — allocazioni.** Chiuso, e non solo sul percorso che il test esercita. Contatore su `operator new`
armato **solo** attorno a `processBlock`, zero allocazioni in tutti questi casi:
regime a 44,1/48/96 kHz con blocchi da 32, 64 e 512 (9 combinazioni); primo blocco dopo un cambio di
sample rate a caldo; primo blocco dopo `applyPrompt`; primo blocco dopo `setStateInformation`;
`processBlockBypassed`; doubler + riverbero + delay accesi insieme; mono in / stereo out; mono puro.
`ParamCache` è costruita una volta nel costruttore e mai più toccata, e i puntatori atomici sopravvivono a
`apvts.replaceState`: verificato che il blocco dopo `setStateInformation` gira e non alloca. Costo residuo:
vedi R11.

**B5 — ducking.** Chiuso. Livello del solo bus delay (differenza fra render con e senza delay), riverbero
spento: `dlyDuck = 0` → **-18,57 dB**, `dlyDuck = 9` → **-27,57 dB**. Nove dB esatti, per campione.
L'inviluppo condiviso **non** introduce accoppiamento udibile: accendendo anche il riverbero (send -40 dB,
duck 9) il bus delay passa da -27,57 a -27,47 dB, cioè 0,1 dB. È la scelta giusta perché le ballistics dei
tre bus sono identiche (5 ms / 180 ms fissi): un inviluppo per bus darebbe lo stesso numero. Resta il knee
non dichiarato del detector (vedi DEBITO).

**B6 — polarità + mix.** Chiuso. Sinusoide a 300 Hz, ingresso -12,04 dBFS, tutti i moduli spenti:
mix 100 % → -12,04 · mix 50 % → -12,04 · mix 50 % con **polarità invertita** → -12,04 (prima: silenzio).
E il trim d'ingresso arriva anche al dry: mix 50 % con `inTrim = +12` → **-0,04 dBFS**.

**B7 — latenza.** Chiuso, e i due percorsi sono allineati. Latenza dichiarata **0**. Impulso entrato al
campione 100: esce al campione 100 sia con `processBlock` sia con `processBlockBypassed`.

**B8 — drive per canale.** Chiuso. Ingresso identico su L e R, `satDrive` 0 → 100 → 0 con rampa:
differenza L-R **-200 dBFS**, cioè bit-identici.

**B9 — divisione del delay.** Chiuso su tutti e quattro i valori. Impulso, 120 bpm, sync attivo:

| divisione | 1/4 | 1/8 puntato | 1/8 | 1/16 |
|---|---|---|---|---|
| eco misurata | 24002 campioni (500,0 ms) | 18002 (375,0) | 12002 (250,0) | 6002 (125,0) |

**B12 — etichette.** Chiuso. `rebuildControls` costruisce una lista parallela `placement {controllo,
etichetta}` (`PluginEditor.cpp:264-305`) e `resized()` la percorre in ordine
(`PluginEditor.cpp:525-526`): l'accoppiamento non viene più ricostruito per tipo.

### Chiusi male (4 su 12)

**B1 — la lunghezza del blocco.** La corruzione di memoria è chiusa: Valgrind pulito su blocchi da 1
campione (4096 di fila), su lunghezze casuali, e su `prepare(64)` seguito da blocchi da 128, 512 e 2048.
Ma `ChainDsp.cpp:589` risolve il problema **clampando**:

```cpp
const auto numSamples = juce::jmin (buffer.getNumSamples(), dryScratch.getNumSamples());
```

quindi un blocco **più lungo** del massimo dichiarato viene processato solo per i primi
`maximumBlockSize` campioni e **il resto esce crudo**. Misura: `prepareToPlay(48000, 256)`, blocco da 1024,
`outGain = -24 dB`, catena "sfera ebbasta" completa:

| campioni 0…255 | campioni 256…1023 | differenza coda − ingresso |
|---|---|---|
| picco **-57,7 dBFS** | picco **-6,02 dBFS** | **-200 dBFS (bit-identici)** |

Cioè: gate, HPF, room tamer, EQ, compressori, saturazione, de-esser, mix, mandate, output gain e limiter
**non esistono** su tre quarti del blocco, e quella parte esce 51 dB più forte del resto. Con
`prepare(64)` il primo campione identico all'ingresso è sempre l'indice 64, per blocchi da 128, 512 e 2048.
Non è un caso di laboratorio: la Standalone consegna blocchi più lunghi a ogni cambio di device, e un host
che aumenta il buffer senza ri-preparare fa esattamente questo. Vedi B13.

**B4 — il limiter.** L'ordine è corretto (mandate e output gain prima, limiter ultimo), ma il **ceiling
continua a non essere un ceiling**, per una ragione diversa da prima: vedi B14. E il **Mix non scala le
mandate**, come chiedeva la correzione proposta: coda dei soli bus, mix 100 % → **-9,45 dBFS**,
mix 0 % → **-9,45 dBFS**. Identici. Con la catena bypassata in parallelo i tre bus suonano a pieno livello.

**B10 — parametri finti.** Tre sono ancora **completamente inerti**, misurati con un'istanza nuova per
ogni render (48 blocchi, doubler acceso), confrontando il minimo e il massimo con il riferimento:

| parametro | a valore minimo | a valore massimo |
|---|---|---|
| `revVariant` | **-200 dB (zero esatto)** | **-200 dB** |
| `dlyVariant` | **-200 dB** | **-200 dB** |
| `fxVariant`  | **-200 dB** | **-200 dB** |

Scritti da `writePresetToParameters` (`PluginProcessor.cpp:426`, `446`, `466`), mai letti da
`currentSettings()`. Averli resi non automatizzabili non li rende veri: restano nello stato salvato, nel
menù a tendina della UI, e `revVariant` continua a promettere *ambience / room / hall / plate* mentre
`juce::dsp::Reverb` riceve sempre gli stessi tre parametri. Il controllo che dovrebbe prenderli non prova
niente: vedi "Test deboli" §1.

**B11 — `satTilt`.** È diventato un tilt vero (due shelf speculari a 700 Hz, `ChainDsp.cpp:244-245`,
-11,8 dB / -16,4 dB di differenza misurata agli estremi), ma le shelf stanno **dentro
`processSaturation`** (`ChainDsp.cpp:448`), che gira solo `if (settings.satOn)`. Con il modulo SAT spento
la manopola "Sat Tilt" non fa niente — e non c'è niente nella UI che lo dica. In più la condizione
`if (settings.satTilt != 0.0f)` fa sì che lo stato delle shelf non avanzi quando il tilt è a 0: passando
da 0 a un valore diverso il filtro riparte con stato vecchio.

### Non chiusi

Nessuno dei dodici è rimasto aperto com'era. Quattro sono chiusi male e uno (B1) ha sostituito un crash
con un difetto udibile.

---

## Nuovi rilievi

### BUG

#### B13 — Un blocco più lungo del massimo dichiarato esce crudo
`plugin/source/ChainDsp.cpp:589`

Riproduzione e misura sopra, in B1. Il `jmin` protegge lo scratch ma non l'audio: nessuno stadio tocca i
campioni oltre `maximumBlockSize`, e nessuno se ne accorge (niente NaN, niente crash, il self-test passa).

**Perché è sbagliato**: il contratto è `<= maximumBlockSize`, ma quando un host lo viola la reazione giusta
non è "processo metà blocco". Un burst di segnale non limitato e non gainstage-ato in uscita è peggio di un
assert.

**Correzione**: ciclare sul blocco a fette da `maximumBlockSize`
(`for (int offset = 0; offset < n; offset += maxBlock) process(sotto-blocco)`), oppure ridimensionare gli
scratch in `process` quando serve — allocazione sul thread audio una volta sola, meglio dell'audio crudo —
e in ogni caso aggiungere un `jassert`. Il controllo deve provare blocchi da `2×` e `4×` il dichiarato e
verificare che **tutta** la lunghezza sia stata processata, non solo che sia finita.

#### B14 — Il "Ceiling" non è un tetto, e abbassandolo il plugin **alza** l'uscita e clippa
`plugin/source/ChainDsp.cpp:709-719` · `juce_Limiter.h` (`process`)

`juce::dsp::Limiter` non è un brickwall: sono due compressori in serie, poi
`outputBlock.multiplyBy (outputVolume)` con `outputVolume = -threshold`, poi
`FloatVectorOperations::clip (…, -1.0, 1.0)`. Quindi il tetto vero è **sempre 0 dBFS**, e il parametro
`limCeiling` funziona da *drive*: più lo si abbassa, più il segnale viene spinto contro un clipper duro.

**Misura** (tutti i moduli spenti tranne il limiter, sinusoide 300 Hz a -6 dBFS, a regime):

| ceiling | out 0 dB | out +6 dB | out +12 dB |
|---|---|---|---|
| 0 dBFS | -5,06 dBFS | -3,56 | -2,06 |
| -1 dBFS | -4,06 | -2,56 | **-1,06** |
| -3 dBFS | -2,06 | -0,56 | **0,00 dBFS, 30 % dei campioni a fondo scala** |
| -6 dBFS | **0,00 dBFS, 30 %** | **0,00, 46 %** | **0,00, 56 %** |

Con delay a feedback 45 %, riverbero decay 4 s size 100 %, doubler acceso e `outGain +12`, ceiling -1:
picco d'uscita **0,00 dBFS**.

Il meter non aiuta: nel caso ceiling -6 / out +12, con il 56 % dei campioni incollati a fondo scala,
`meters.limGr` legge **5,98 dB** — che è il make-up, non la riduzione, e non segnala il clipping.

**Perché è sbagliato**: l'utente vede "Ceiling -1 dBFS" e riceve 0 dBFS. Un preset che abbassa il ceiling
per "essere prudente" ottiene una distorsione da clipping su metà dei campioni. È il difetto B4 di nuovo,
per una via diversa: allora il limiter non era ultimo, adesso è ultimo ma non è un limiter.

**Correzione**: non usare `juce::dsp::Limiter` per un ceiling. O si scrive un vero brickwall
(lookahead + hold + release, con la latenza **dichiarata**, e allora anche `processBlockBypassed` va
ritardato), oppure si applica un gain di `-limCeiling` prima e `+limCeiling` dopo il `juce::dsp::Limiter`
lasciandogli soglia 0, e si rinomina il parametro in "Drive". Il controllo deve girare su tutti i valori di
ceiling, non su uno solo.

#### B15 — `prepareToPlay` non riporta il plugin in uno stato riproducibile: il bounce non è il mixdown
`plugin/source/ChainDsp.cpp:159-169`

In fondo a `ChainDsp::prepare` c'è `updateCoefficients()`, che legge il membro `settings` — cioè **la
configurazione della sessione precedente**, non i parametri correnti. I `SmoothedValue` (trim, output, mix,
drive, tre send) partono quindi dai valori vecchi e ci mettono 20 ms a raggiungere quelli veri, e quella
rampa cambia la storia di compressori, riverbero e delay per tutto il render.

**Misura** (stesso segnale, stesso prompt, 16 blocchi da 512, confronto sull'ultimo blocco):

| | differenza |
|---|---|
| istanza nuova vs istanza nuova | **-200 dB (identici)** |
| stessa istanza, render 1 vs render 2 | **-50,0 dB** |
| stessa istanza, render 2 vs render 3 | -80,8 dB |
| istanza nuova vs stessa istanza al terzo render | -50,2 dB |

**Perché è sbagliato**: bounce offline e ascolto in tempo reale non coincidono, e due bounce di fila non
coincidono fra loro. È anche la ragione per cui il controllo "nessun parametro finto" non prova niente
(§1 dei test deboli): il pavimento di rumore della misura sta 50 dB sopra la sua soglia.

**Correzione**: in fondo a `prepare`, azzerare i `SmoothedValue` con `setCurrentAndTargetValue` invece di
lasciarli ramp-are, e far leggere al processore i parametri veri prima del primo blocco (`settingsDirty`
è già `true`: basta chiamare `setSettings` con `currentSettings()` da `prepareToPlay`, prima di
`chain.prepare`, o snappare i valori nel primo blocco). Il controllo: due render identici della stessa
istanza devono differire di **zero**.

#### B16 — `ChainDsp::reset()` non viene mai chiamata: la coda sopravvive al reset dell'host
`plugin/source/ChainDsp.cpp:172-205` · `plugin/source/PluginProcessor.h` (manca l'override di `reset()`)

`VocalForgeProcessor` non fa override di `juce::AudioProcessor::reset()`, quindi le 34 righe di
`ChainDsp::reset()` sono codice morto.

**Misura**: 40 blocchi di sinusoide, poi `processor.reset()`, poi 20 blocchi di **silenzio puro** in
ingresso → picco d'uscita **-9,1 dBFS**. Con `prepareToPlay` al posto di `reset()`: -200,0 dBFS.

**Perché è sbagliato**: Logic chiama `reset()` quando si sposta la testina, fra un take e l'altro, e
all'inizio di un ciclo. La coda del riverbero e il contenuto del delay del passaggio precedente rientrano
sopra il nuovo. In `auval`/`pluginval` il test "reset clears tail" fallisce.

**Correzione**: `void reset() override { chain.reset(); }` in `VocalForgeProcessor`.

#### B17 — La soglia del room tamer dipende dal Q: con Q stretti il notch è sempre acceso
`plugin/source/ChainDsp.cpp:236` (detector) · `ChainDsp.cpp:331-335` (confronto con la soglia)

Il detector è l'uscita **bandpass** di uno `StateVariableTPTFilter`, che a risonanza ha guadagno `1/R2 = Q`
(`juce_StateVariableTPTFilter.cpp:113-116`). Il livello di quell'uscita viene confrontato direttamente con
`roomThresh`, che l'utente legge in dBFS. Quindi la soglia effettiva è `roomThresh - 20·log10(Q)`.

**Misura** (sinusoide a 92 Hz, `roomThresh = -28 dBFS`, `room1Depth = -12 dB`, attenuazione a regime):

| | ingresso -40 dBFS (12 dB **sotto** la soglia) | ingresso -20 dBFS |
|---|---|---|
| Q = 1 | 0,00 dB | -7,02 dB |
| Q = 3 | 0,00 dB | -12,00 dB |
| Q = 6 | **-2,59 dB** | -12,00 dB |
| Q = 12 | **-8,61 dB** | -12,00 dB |

A Q = 12 l'errore è 21,6 dB: il notch morde a pieno su una risonanza che l'utente ha detto di ignorare.

**Perché è sbagliato**: `CLAUDE.md` prescrive "Q stretti" **e** "attenuazione dinamica (agiscono solo
quando la risonanza supera la soglia)". Con i Q stretti che il progetto impone, la parte dinamica non
esiste: è un notch fisso. E la stessa manopola Q cambia due cose insieme (larghezza del notch e soglia
d'intervento), il che rende impossibile tarare l'una senza rompere l'altra.

**Correzione**: normalizzare l'uscita del detector dividendo per Q (`detected / resonance`), oppure usare
un vero band-pass a guadagno unitario a risonanza. Il controllo: stessa attenuazione misurata a Q 1, 3, 6 e
12 con lo stesso livello d'ingresso.

### RISCHI

#### R9 — Il detector del de-esser è la media sui canali: il materiale panoramizzato è de-essato in ritardo
`plugin/source/ChainDsp.cpp:369-371`

`detector += std::abs(high[ch])` poi `detector /= used`. Una sibilante identica, soglia -30 dB, range 12 dB:
al centro → **-4,15 dB** di riduzione; la stessa sibilante **solo su L** → **-1,03 dB**. Tre dB di
differenza per un'immagine stereo, con un plugin che è dichiaratamente dual-mono
(`Preset::stereoIsDualMono = true`). Il compressore (`ChainDsp.cpp:404-405`) usa invece il **massimo** sui
canali: due detector nello stesso file con due convenzioni diverse. Il massimo è quello giusto per un
de-esser.

#### R10 — Cambio di preset ad audio in corsa: nessun crash, ma un click a ogni FORGE
`plugin/source/PluginProcessor.cpp:367-492` · `PluginProcessor.cpp:347`

400 `applyPrompt` dal thread messaggi (quattro prompt a rotazione, uno ogni 500 µs) mentre un thread audio
gira a blocchi da 64: **54169 blocchi, 0 campioni non finiti**. La struttura regge. Ma:

| | salto campione-campione peggiore |
|---|---|
| senza cambi di prompt | 0,011 (**-38,8 dBFS**) |
| con i cambi di prompt | 0,169 (**-15,4 dBFS**) |

23 dB di differenza: è un click udibile a ogni FORGE. La causa è quella già scritta in R4 del primo giro
(makeup, guadagni EQ, depth dei notch e soglie non smussati), più il fatto che `settingsDirty` viene
alzato 60+ volte durante la raffica e `processBlock` può fotografare l'insieme **a metà**: soglia nuova con
makeup vecchio, divisione del delay nuova con tempo vecchio. Il controllo che dovrebbe prenderlo accetta
salti fino a 0,5 (§4 dei test deboli).

**Correzione**: costruire il `ChainSettings` completo **fuori** dal thread audio, in un doppio buffer
scambiato con un solo `std::atomic` (così la fotografia è coerente), e smussare makeup/guadagni/depth.

#### R11 — `ParamCache`: ricerca lineare per stringa, 18,9 µs a blocco durante l'automazione
`plugin/source/PluginProcessor.h:73-79` · `PluginProcessor.cpp:186-193`

Thread-safe lo è: `entries` si costruisce nel costruttore e non viene più toccata, e i puntatori
`std::atomic<float>*` sopravvivono a `apvts.replaceState` (verificato). Ma `get()` è un `for` su 105
elementi con `juce::String == const char*`, e `currentSettings()` lo chiama ~110 volte.

**Misura** (blocchi da 64 a 48 kHz, catena completa): 30,5 µs senza automazione, **49,4 µs** con un
parametro che si muove a ogni blocco → **18,9 µs** per `currentSettings()`, cioè l'1,4 % del budget di un
blocco da 64 a 48 kHz e il **5,7 %** di un blocco da 32 a 96 kHz. Non è un dropout, ma è cento volte il
costo di 110 letture atomiche.

**Correzione**: risolvere gli id in indici una volta sola (un `enum` o uno `std::array<std::atomic<float>*,
N>` indicizzato per posizione), non per stringa.

#### R12 — R1 confermato e peggiorato: il room tamer dipende ancora dalla dimensione del blocco
Stesso transiente (burst di 92 Hz), stesso preset, blocchi da 32 e da 1024 campioni: differenza
**-28,5 dBFS** (il primo giro misurava -34,9 su un altro segnale). A regime la differenza è zero — è solo
sui transienti, cioè esattamente dove si sente. La causa è invariata: detector su tutto il blocco, poi un
solo aggiornamento dei coefficienti per blocco (`ChainDsp.cpp:326-347`).

#### R13 — Predelay del riverbero e doubler: `setDelay` per blocco senza rampa
`plugin/source/ChainDsp.cpp:481` · `ChainDsp.cpp:561-563`

Automatizzando `revPredelay` da 0 a 80 ms in un colpo, il salto campione-campione peggiore in uscita è
0,008 (**-42,0 dBFS**): piccolo perché il segnale è già filtrato e attenuato dal send, ma su una coda
silenziosa è un click. Il doubler invece è sano: a 48 e 96 kHz con blocchi da 32 campioni, 3000 blocchi,
uscita finita, picco -7,96 dBFS, componente side -24,99 dBFS identica alle due frequenze di
campionamento.

#### R14 — Il prompt non ha limite di lunghezza: 500 kB bloccano la UI per 2,4 s
`plugin/source/PluginEditor.cpp:130` (`promptBox` senza `setInputRestrictions`)

`applyPrompt` con 500 kB di testo impiega **2423 ms** sul thread messaggi (con 100 000 lettere: 78 ms;
con un prompt normale: 3 ms). Nessun crash, preset valido, ma l'interfaccia si pianta. Un incolla
accidentale basta.

---

## Controllato e sano

* **Il biquad scritto a mano è stabile.** Nessun ciclo limite, nessun denormale che si accende.
  Una campana sola a 60 e 100 Hz, Q 4 e 8, guadagno -12 dB, a 48 e 96 kHz: dopo 2,6 s di silenzio in
  ingresso il picco d'uscita è **sotto -200 dBFS** (residui nell'ordine di 1e-38, cioè già a zero).
  120 combinazioni casuali di tutti i parametri di EQ, room tamer, air, tilt e drive agli estremi del loro
  range, a 44,1/48/96 kHz, con 400 blocchi ciascuna: **nessun NaN, nessuna divergenza**. Il clamp di
  `setPeak`/`setShelf` (`freq` in `[10, sr·0,49]`, `q ≥ 0,05`) regge alle frequenze estreme. La `Q` alta a
  bassa frequenza non perde precisione in `float` in modo misurabile.
* **`RulesEngine` regge il testo cattivo.** Tredici casi limite — prompt vuoto, soli spazi, accenti,
  emoji, cirillico, cinese, un NUL in mezzo alla stringa, sola punteggiatura, 10 000 parentesi aperte,
  100 000 lettere, 500 kB di testo, numeri assurdi (`1e999`), prompt contraddittori
  ("molto brillante ma per niente brillante, tantissimo riverbero senza riverbero") — danno tutti un preset
  valido con 13 moduli, **tutti i parametri finiti e dentro `[0,1]` normalizzato**, audio finito, nessun
  crash, nessun loop. E la pipeline è deterministica: ricompilando lo stesso prompt dopo un altro prompt,
  **0 parametri diversi**.
* **Zero allocazioni sul thread audio** in tutti e otto i percorsi provati (elenco in B3).
* **`ParamCache` è thread-safe** rispetto a `apvts`: costruita una volta, mai mutata, e i puntatori
  restano validi dopo `replaceState`.
* **96 kHz e blocchi da 32 campioni**: catena completa, doubler compreso, nessun NaN, comportamento
  identico a 48 kHz sui numeri che devono esserlo (room tamer -12,00 dB a 44,1/48/96 kHz).
* **Il doubler** non collassa e non si sfasa in mono; a 96 kHz si comporta come a 48.
* **La memoria è pulita**: Valgrind senza un solo errore su blocchi da 1 campione, lunghezze casuali,
  `prepare` piccolo seguito da blocchi grandi.

---

## Test deboli

Ordine di gravità. Il primo è il caso peggiore: un controllo che **non può fallire**.

1. **`selftest_main.cpp:891` — "nessun parametro finto" non misura niente.**
   Due difetti sommati: (a) `render()` riusa **una sola istanza** del processore, e
   `writePresetToParameters` riscrive solo i parametri che il preset contiene, quindi ogni iterazione
   eredita il residuo del probe precedente; (b) `prepareToPlay` non riporta lo stato a un punto fisso
   (B15). Risultato misurato: **due render identici di fila differiscono già di -50,0 dB**, cioè il
   pavimento della misura sta 50 dB sopra la soglia di -100 dB. Prova che è così: `revVariant`,
   `dlyVariant`, `fxVariant`, `dlyTime`, `dlyFeedback` e `sendsMode` riportano **tutti lo stesso identico
   numero, -36,2 dB**, e `ds1Range`, `ds1Mode`, `ds2Range`, `ds2Mode`, `tone1Freq`, `tone1Q` e
   `limRelease` riportano **tutti -50,2 dB**. Numeri uguali fra parametri che non c'entrano niente fra
   loro: non è la loro differenza.
   **Come renderlo vero**: un'istanza nuova per ogni render (con l'istanza nuova i tre `*Variant` misurano
   **-200 dB esatti**, e il controllo li prende); provare **minimo e massimo**, non un flip normalizzato
   0/1; e prima di tutto misurare il riferimento contro sé stesso e pretendere zero — se non è zero, il
   test è rotto e va detto.

2. **`selftest_main.cpp:591-601` — blocchi di lunghezza variabile.**
   `lengths (1, blockSize)` prova solo blocchi **più corti**, e controlla solo `isFinite`. Non copre il
   caso che oggi è rotto (B13: blocco più lungo → coda cruda), non gira sotto un allocatore di guardia, e
   non verifica che il blocco sia stato **processato**.
   **Come renderlo vero**: aggiungere `2×` e `4×` la lunghezza dichiarata; con tutti i moduli spenti
   pretendere il null-test su **tutta** la lunghezza, e con i moduli accesi pretendere che
   `|uscita − ingresso| > soglia` su **ogni** campione del blocco; girare l'intero giro sotto Valgrind in CI.

3. **`selftest_main.cpp:697` — "il ceiling tiene".**
   Un solo valore di ceiling (-1 dB), un solo output gain, e un segnale che si ferma mezzo dB sotto la
   soglia del controllo (`peakDb <= -0.5f`). Basta spostare il ceiling a -3 o -6 e il plugin sfonda a
   0 dBFS con il 30-56 % dei campioni a fondo scala (B14), senza che nessuno se ne accorga.
   **Come renderlo vero**: ciclare su `limCeiling ∈ {0, -1, -3, -6}` con ingressi fino a 12 dB sopra,
   pretendere `picco ≤ ceiling + 0,1 dB`, e aggiungere un conteggio dei campioni a `|x| ≥ 0,999` che deve
   essere **zero**.

4. **`selftest_main.cpp:274` — "cambio di prompt in corsa senza click".**
   `worstJump < 0.5f` su un segnale il cui picco è 0,4: solo un mute istantaneo può fallire. Il salto vero
   misurato è 0,169 contro 0,011 di riferimento (R10), e passa.
   **Come renderlo vero**: misurare il salto peggiore **dello stesso render senza cambio di prompt** e
   pretendere che il cambio non lo peggiori più di 2×.

5. **Il controllo del de-esser (`selftest_main.cpp:604-644`)** guarda due frequenze (200 Hz e 9 kHz) con
   il modulo da solo e la riduzione attiva. Non prenderebbe una ricomposizione fuori fase (che si vede come
   un buco **nella zona di incrocio**, non agli estremi) né un modo "wide" che non è wide.
   **Come renderlo vero**: nove frequenze, e soprattutto la riga **senza riduzione**, pretendendo
   ±0,1 dB di piattezza — è la misura che chiude B2 davvero, ed è quella che oggi manca.

6. **Il controllo delle allocazioni (`selftest_main.cpp:646-673`)** arma il contatore per 10 blocchi a
   regime più 1 dopo un movimento di parametro. Gli altri percorsi (primo blocco dopo `prepare`,
   `setStateInformation`, `applyPrompt`, bypass, mono, 96 kHz con blocchi da 32) non sono coperti: li ho
   misurati io e sono puliti, ma il test non lo sa e non se ne accorgerebbe se smettessero di esserlo.

7. **Manca del tutto un controllo di riproducibilità.** Nessun test rende due volte lo stesso materiale
   con la stessa istanza e pretende bit-identità. È il controllo che avrebbe preso B15 — e, di rimbalzo,
   avrebbe reso onesto il §1.

---

## DEBITO (secondo giro)

* **Numeri DSP scritti a mano in `ChainDsp.cpp`**, contro la regola di `CLAUDE.md` ("un numero DSP
  hardcoded in un `.cpp` è un bug"): il `×4` del detector di duck (riga 473), le sue ballistics 5/180 ms
  (465-466), l'attacco/rilascio del de-esser 0,5/40 ms e il rapporto 0,7 (356-357, 375), le ballistics del
  room tamer 10/120 ms e il `/12` della rampa di soglia (319-320, 336), i 6 dB di isteresi del gate (286),
  i 700 Hz del tilt (244-245), i 12 kHz dell'air (241), gli 0,7 Hz e gli 0,004 s del doubler (541-542).
  Nessuno di questi è in `rules.json`, e nessuno è documentato in UI.
* **Il knee del ducking non è dichiarato.** `duckEnv.process(...) * 4.0f` clampato a 1 significa che il
  duck arriva al massimo con la voce a ~-13 dBFS e non esiste sotto -40. Misura: voce a -60 dBFS → bus a
  -12,59 dB sotto; -40 → -12,89; -30 → -13,62; -20 → -15,94; -12 → -21,06; -6 → -21,55. Proporzionale
  quindi lo è, ma la curva è fissa e dipende dal livello **assoluto** d'ingresso: la stessa catena su una
  voce registrata 10 dB più piano ducka la metà.
* **Il meter del limiter legge il make-up, non la riduzione** (`ChainDsp.cpp:715-718`): con ceiling -6 e
  out +12, e il 56 % dei campioni a fondo scala, riporta 5,98 dB — cioè esattamente `-limCeiling`.
* Restano tutti i punti di debito del primo giro non toccati: il ternario con i due rami identici
  (`PluginProcessor.cpp:402-403`), l'overload morto `isBusesLayoutSupported (const BusesProperties&)`
  (`PluginProcessor.h:21`), `outLufs` che non è LUFS e non lo legge nessuno, `getSample`/`setSample` per
  campione in tutti gli stadi.


---

## Terzo passaggio — correzioni del secondo giro

Rilievi del "Secondo giro" chiusi, ognuno con il controllo che lo prende in `plugin/tools/selftest_main.cpp`:

| rilievo | correzione | controllo |
|---|---|---|
| **B14** limiter di JUCE che riamplifica di −threshold (tetto reale 0 dBFS, abbassare il ceiling alzava l'uscita) | limiter proprio: riduzione verso il tetto + clamp di sicurezza sul campione | ceiling a −1/−2/−4/−6: picco sotto il tetto, zero campioni a fondo scala, e abbassarlo abbassa l'uscita |
| **B1 chiuso male** un blocco più lungo del dichiarato usciva crudo | il blocco si spezza in tratti della dimensione preparata: esce processato per intero | `prepare(256)` + blocco da 1024 |
| **B4 chiuso male** il MIX non scalava le mandate | le tre mandate sono moltiplicate per il MIX | coda del riverbero a mix 0 % vs 100 % |
| **B10 chiuso male** `revVariant`/`dlyVariant`/`fxVariant` inerti | tolte dai parametri: erano etichette del preset, non controlli | "nessun parametro finto", ora severo (−80 dB) |
| **B11 chiuso male** il tilt viveva dentro la saturazione | stadio proprio, attivo anche a SAT spento | bilanciamento 200 Hz / 8 kHz con SAT spento |
| **B15** due render della stessa istanza differivano di 50 dB | `prepareToPlay` azzera lo stato della catena | due render identici < −120 dBFS di scarto |
| **B16** `ChainDsp::reset()` mai chiamata | `AudioProcessor::reset()` implementata | coperto dal controllo di riproducibilità |
| **B17** la soglia del room tamer dipendeva dal Q | il detector bandpass è normalizzato per il suo guadagno Q | sotto soglia non morde né a Q 1 né a Q 12 |

Sul rilievo "il test dei parametri finti non può fallire": era vero, e la causa era **B15**. Con lo stato
azzerato due render coincidono sotto i −120 dBFS, quindi la soglia è stata portata a −80 dB e il controllo
adesso distingue davvero. Ha già trovato una cosa: `sendsMode` restava su *logic* da un test precedente e
spegneva i bus interni — la preparazione dei test ora lo riporta a *internal*.

Controlli headless totali: **126**.


---

## Quarto passaggio — rischi e debito

Chiusi tutti i RISCHI R1–R8 del primo giro e le voci di debito che avevano un effetto reale.

| # | cosa era | correzione | controllo |
|---|---|---|---|
| **R1** | il room tamer guardava tutto il blocco prima di filtrare: ascolto ≠ bounce, e la banda 2 leggeva il segnale già filtrato dalla banda 1 | detector e filtro camminano insieme per campione, detector sull'ingresso dello stadio, profondità smussata, coefficienti a control rate indipendente dal blocco | stesso ingresso a blocchi da 64 e da 1024: differenza sotto −60 dBFS |
| **R2** | filtri dimensionati a 2 canali mentre altri stadi ciclavano su tutti | tutti gli stadi coprono `kChannels = 8` | quattro canali identici escono identici |
| **R3** | `setLatencySamples` ad audio in corsa a ogni FORGE | la latenza la fissa solo `prepareToPlay` | coperto dal controllo di latenza |
| **R4** | 60 `setValueNotifyingHost` senza gesto e senza smoothing | `beginChangeGesture`/`endChangeGesture` per parametro, makeup smussati, valori non finiti scartati | click a cambio di prompt, misurato **contro il salto normale della catena** |
| **R5** | `onPresetGenerated` toccava la UI dal thread di `setStateInformation` | `AsyncUpdater`: la UI si aggiorna sul message thread | — |
| **R6** | il valutatore falliva in silenzio; `not` legava più stretto dei confronti (diverso da Python); divisione senza guardia | errori propagati e trasformati in avvisi del preset (stesso testo nei due motori), `not` spostato sopra i confronti, guardia su divisione e non-finiti | 15 espressioni di riferimento valutate in Python e riverificate in C++, più un test che valuta **ogni** espressione di `rules.json` in tutti gli angoli del dominio |
| **R7** | spegnere una mandata troncava la coda; il tempo del delay saltava; `getTailLengthSeconds` mentiva | i bus continuano finché la mandata sta scendendo, tempo del delay interpolato, coda dichiarata calcolata da decay e feedback | la coda continua dopo lo spegnimento; coda dichiarata ≥ tempo del delay |
| **R8** | nella banda di isteresi il gate congelava il guadagno a metà corsa | isteresi vera: sopra apre, sotto chiude, in mezzo resta com'era | segnale dentro la banda: il gate resta aperto |

Debito chiuso: meter `outLufs` che non era LUFS e non leggeva nessuno (rimosso), overload morto di
`isBusesLayoutSupported`, ternario con i due rami identici, scratch mai usati.

**Due difetti trovati dai test nuovi, non dalla revisione:**

1. I valori smussati partivano dal residuo del render precedente, quindi il primo blocco dopo
   `prepareToPlay` rampava invece di partire già a destinazione: due render identici differivano di
   −21 dBFS sul solo stadio di saturazione. Ora alla partenza si agganciano al valore di destinazione.
2. Il controllo "nessun parametro finto" chiedeva che ogni parametro cambiasse **il suono** su un
   segnale solo — cosa falsa per una soglia di gate su un segnale sempre sopra soglia. Ora verifica che
   il parametro **arrivi alle impostazioni che il DSP riceve**: è esattamente il difetto B10, senza
   dipendere dalle condizioni del segnale.

Controlli headless: **124**. Test Python: **79**. Parità: 18 preset, 1683 valori, 0 differenze.


---

## Terzo giro

Tutto quello che segue è misurato sul binario compilato da `a5a7f98`
(`cmake --build build --target vocalforge_selftest vocalforge_parity`), con programmi di prova
linkati agli stessi oggetti fuori dal repo, Valgrind/Helgrind, e — per i test — reintroducendo il
difetto in una copia dei sorgenti e rilanciando la suite. Stato di partenza: 229 controlli headless
verdi, 84 test Python verdi, parità 18 preset / 1683 valori / 0 differenze.

### Correzioni verificate

| # | esito | misura |
|---|---|---|
| **R1** room tamer indipendente dal buffer | **chiuso** | stesso ingresso, stesso preset, blocchi da 1024 / 512 / 100 / 64 / 37 / **1**, e `prepare(256)` con blocchi da 1024: differenza **−300 dBFS (bit per bit)**, a Q 1, 5 e 12 con profondità −12 dB. Vale anche sulla catena completa. Il contatore che persiste fra i blocchi è davvero l'unica cosa che serviva. |
| **R1** zipper con Q alti | **chiuso** | a Q 12 l'uscita è identica a Q 1 e Q 5 quanto a continuità; la curvatura massima campione‑per‑campione col tamer acceso resta nell'ordine del segnale stesso. L'interpolazione della profondità a 0,03 s basta: fra due aggiornamenti (16 campioni a 44,1 kHz) la profondità si muove al massimo di ~0,05 dB. |
| **R6** `not` sopra i confronti | **chiuso per `not`, aperto per il resto** — vedi B18/B19. `not a and b`, `a and not b or c`, `not not a`, `not (a and b)`, `not a > b`: i due motori danno lo stesso valore su tutti. Ma esistono espressioni che divergono, e le fixture non le prendono. |
| **R7** delay interpolato | **chiuso** | automazione del tempo del delay a ogni blocco fra 100 e 1200 ms: curvatura massima **×1,0** rispetto al segnale fermo. Nessun salto. |
| **R7** i bus continuano dopo lo spegnimento | **chiuso male** — vedi B21. |
| **R7** coda dichiarata all'host | **chiuso male** — vedi B22. |
| **R8** isteresi booleana | **chiuso a metà** — la banda funziona (dentro la banda il gate resta aperto, 0,00 dB di attenuazione), ma il gate **parte aperto** anche se `gateOpen` parte falso: vedi B24. |
| **snap dei valori smussati** | **chiuso male** — vedi B23 (il flag viene armato *dopo* l'aggiornamento che dovrebbe consumarlo) e B20 (`delayTimeSamples` non è nella lista). |
| **limiter** | **chiuso** | ceiling −1 e −6, release 10/100/500 ms, ingresso 0 e −3 dBFS: picco d'uscita esattamente al tetto, THD **0,04 ÷ 1,11 %** (l'1,11 % solo a release 10 ms, che è il caso in cui un limiter *deve* distorcere). Nessun incollamento: dopo 2 s di rumore denso a +12 dB sopra il tetto, il rientro a livello è 21 / 229 / 1179 ms per release 10 / 100 / 500 ms, cioè proporzionale al release e non di più. Il clamp finale non è la parte che lavora. |
| **`ParamCache`** | **debito reale, misurato** — vedi D1. |

### Nuovi rilievi

#### BUG

**B17 — `float copies[2]` scritto fino all'indice 7: stack smashing.**
`plugin/source/ChainDsp.cpp:641` dichiara `float copies[2]`, `:652` ci scrive dentro con
`ch` che arriva a `jmin (kMaxChannels, channels) - 1 = 7`.
Riproduzione (crash immediato, non teorico):
```cpp
vf::ChainDsp chain;
chain.prepare (48000.0, 256, 4);           // 4 canali: dentro il contratto kChannels = 8
vf::ChainSettings s; s.fxOn = true; s.revOn = false; s.dlyOn = false;
chain.setSettings (s);
juce::AudioBuffer<float> buf (4, 256); /* riempi */ chain.process (buf, 120.0);
// *** stack smashing detected ***: terminated
```
Perché è sbagliato: R2 ha allargato la classe a 8 canali (`kChannels = 8`, `kMaxChannels = 8`,
`processChunk` che cicla fino a 8) ma il doubler è rimasto a due. Il controllo R2 della suite
(`selftest_main.cpp`, "con quattro canali identici tutti escono uguali") **usa esattamente 4 canali** e
non esplode solo perché `fxOn` è falso di default: il crash è a un parametro di distanza dal test
che dovrebbe coprirlo.
Correzione: `float copies[kChannels]`, e il mid/side solo sui primi due (`copies[0]`, `copies[1]`),
lasciando gli altri canali in copia diretta.

**B18 — `processChunk` limita i canali a `kMaxChannels` invece che ai canali *preparati*: lettura e scrittura fuori dall'heap.**
`plugin/source/ChainDsp.cpp:675` — `juce::jmin (kMaxChannels, full.getNumChannels())`. I biquad sono
array da 8, ma i filtri JUCE (`hpf1`, `keyHp`, `revHp`…) e gli scratch (`dryScratch`,
`revScratch`) sono dimensionati da `prepare()` sul numero di canali dichiarato.
Riproduzione: `chain.prepare (48000.0, 256, 2)` e poi `chain.process()` su un buffer da 4 canali.
Valgrind:
```
Invalid read of size 4 / Invalid write of size 4
   at juce::dsp::StateVariableTPTFilter<float>::processSample(int, float)
   by vf::ChainDsp::processChunk(...)
   Address ... is 0 bytes after a block of size 8 alloc'd  (prepare → StateVariableTPTFilter::prepare)
```
Stessa cosa più avanti: il MIX legge `dryScratch.getSample (ch, …)` fino a `channels`
(`ChainDsp.cpp:779-780`) senza il `jmin` che la copia del dry a `:694` invece ha, e `processSends`
scrive `revScratch` su `destination.getNumChannels()` canali.
Perché è sbagliato: la classe promette 8 canali ma ne alloca `numChannels`. Nel plugin spedito
`isBusesLayoutSupported` tiene i bus a mono/stereo, quindi è latente; ma è latente per una guardia
che sta in un *altro* file, non per costruzione.
Correzione: `const auto channels = juce::jmin (numChannels, full.getNumChannels());` e lo stesso
`jmin` sul MIX; oppure allocare sempre `kChannels` in `prepare()`.

**B19 — il compilatore Python non protegge le espressioni dei *parametri*: una regola rotta è un traceback, non un avviso.**
`tools/chain_compiler.py:348-366` (`_resolve_param`) e `:330` (`build_context`) chiamano `evaluate()`
nudo. La funzione `checked()` — quella che trasforma l'errore in `EXPRESSION_WARNING`, e il cui
docstring dice "Un'espressione che non si valuta diventa un avviso, non uno zero silenzioso" — è
usata **solo** su `spec["enabled"]` dei moduli e delle mandate (`:394`, `:414`).
Riproduzione: nel `rules.json`, `chain/hpf/params/hpfFreq/expr` sostituita con una delle seguenti,
e poi `compile_preset("voce trap aggressiva", ...)`:

| espressione | Python | C++ |
|---|---|---|
| `clamp(80 * nonexistent, 70, 140)` | **ExprError non catturato** (traceback) | avviso + `hpfFreq = 70` |
| `clamp(80 / (weight - weight), 70, 140)` | **ExprError non catturato** | avviso + `hpfFreq = 70` |
| `clamp(80 * pitch_factor, 70 140)` (virgola dimenticata) | **ExprError non catturato** | avviso + `hpfFreq = 0` |
| `clamp((80 + 4 * proximity_boost) * pitch_factor, 70)` (arità sbagliata) | **TypeError non catturato** | **nessun avviso**, `hpfFreq = 0` |

Perché è sbagliato: R6 dichiarava "errori propagati e trasformati in avvisi del preset (stesso testo
nei due motori)". Il testo è lo stesso, il comportamento no: per i parametri — cioè per il 95 % delle
espressioni di `rules.json` — Python muore e il C++ avvisa. E l'ultima riga è peggio: **nessuno dei
due avvisa** e i due motori producono valori diversi.
Da notare anche che il C++ scrive **0**, non "il valore neutro" che l'avviso promette: 0 viene poi
clampato al minimo del range del parametro (`hpfFreq` finisce a 40 Hz), che è un estremo, non un
neutro.
Correzione: passare `_resolve_param` e `build_context` attraverso `checked()`; catturare anche
`TypeError` in `evaluate()` e rilanciarla come `ExprError`; nel C++, in caso di fallimento non
scrivere 0 ma lasciare il parametro al suo valore corrente e dirlo nell'avviso.

**B20 — `delayTimeSamples` non è nella lista dello snap: il primo eco dei delay corti si perde, sempre.**
`plugin/source/ChainDsp.cpp:172` lo prepara con una rampa di 0,15 s, ma `:296-303` (lo snap
introdotto per il "difetto trovato dai test nuovi") copre solo i nove guadagni. Il valore parte
quindi da **0 campioni** e sale al target in 0,15 s: se il tempo di delay è più corto di ~150 ms, il
puntatore di lettura non raggiunge mai il campione scritto all'inizio.
Riproduzione: impulso singolo dopo `prepareToPlay`, tutti i moduli spenti tranne il delay,
`dlySync = 0`, feedback 45 %, send −8 dB, 48 kHz:

| `dlyTime` | echi trovati |
|---|---|
| 100 ms | **nessuno** (solo 0,03 di sporcizia a 0,23 e 0,69 ms) |
| 200 ms | 200,0 ms (0,084) · 400,1 ms (0,022) |
| 375 ms | 375,0 ms (0,085) · 750,1 ms (0,022) |
| 700 ms | 700,0 ms (0,085) · 1400,1 ms (0,022) |

`dlyTime` arriva fino a 60 ms, e 1/16 a 120 BPM è 125 ms: sono impostazioni normali per la trap, non
angoli. Succede a ogni `prepareToPlay` (avvio, cambio di sample rate, cambio di buffer).
Correzione: mettere `delayTimeSamples` nella lista dello snap dopo averne fissato il target, o
chiamare `setCurrentAndTargetValue()` sul primo blocco.

**B21 — le mandate non "continuano finché la mandata scende": si spengono dopo 20 ms e restano congelate.**
`plugin/source/ChainDsp.cpp:563`, `:598`, `:627`, `:785-787`: il bus resta attivo finché
`revSendGain.getCurrentValue() > 1e-5`. Quel valore è una `SmoothedValue` con rampa **0,02 s**: venti
millisecondi dopo lo spegnimento arriva esattamente a zero e il bus smette di girare.
Riproduzione (48 kHz, blocchi da 64, riverbero solo, `revDecay = 4 s`, `revSend = −6 dB`): 0,5 s di
rumore, poi `revOn = 0` e solo silenzio:
```
coda dopo revOn=0 : 20,00 ms   (decay dichiarato 4,00 s)
   t=  0,00 ms  −11,19 dBFS
   t=  6,67 ms  −15,18 dBFS
   t= 13,33 ms  −22,21 dBFS
   t= 20,00 ms  −300   dBFS
```
E poi il seguito, che è peggio: **riaccendendo la mandata 3 s dopo, con l'ingresso muto, esce subito
un picco a −5,73 dBFS.** Il riverbero non è finito, è stato messo in pausa: i comb di
`juce::dsp::Reverb`, `delayLine`, `revPredelayLine`, `doublerLine` e i filtri dei bus non vengono né
azzerati né fatti girare mentre il bus è inattivo, quindi conservano il contenuto e lo risputano al
riaccendersi. Spegnere e riaccendere una mandata in fretta — un gesto di automazione qualsiasi —
produce un ritorno di coda vecchia più forte del segnale.
Correzione: due condizioni distinte. Il bus deve girare finché **la sua coda** è sopra una soglia
(un follower sull'uscita del bus, o semplicemente un contatore di `getTailSeconds()` campioni dopo lo
spegnimento); e quando si ferma davvero, deve azzerare il suo stato (`reverb.reset()`,
`delayLine.reset()`, …) invece di congelarlo.

**B22 — `getTailSeconds()` ignora il sync: con un delay sincronizzato l'host tronca il bounce.**
`plugin/source/ChainDsp.cpp:818` usa `settings.dlyTime` anche quando `dlySync` è acceso, cioè quando
il tempo vero è `60000 / bpm * dlyDivisionBeats`.
Riproduzione (48 kHz, delay solo, `dlyTime = 375 ms`, feedback 45 %, `dlySync` acceso, impulso e poi
silenzio; coda vera = ultimo campione sopra −80 dBFS):

| BPM | divisione | tempo vero | coda vera | coda dichiarata |
|---|---|---|---|---|
| 60 | 1/4 | 1000 ms | **7,00 s** | 3,74 s |
| 60 | 1/8 punt. | 750 ms | **5,25 s** | 3,74 s |
| 90 | 1/4 | 667 ms | **4,67 s** | 3,74 s |
| 120 | 1/8 punt. | 375 ms | 2,63 s | 3,74 s |

A 60 BPM con la divisione 1/4 il bounce perde **3,3 secondi** di coda. A 120 BPM il difetto è
invisibile perché il default `dlyTime = 375 ms` è per coincidenza il valore sincronizzato di
1/8 puntato a 120 BPM — ed è esattamente la condizione in cui gira il controllo della suite.
Correzione: calcolare il tempo del delay in `getTailSeconds()` con la stessa formula di
`processSends`, prendendo il BPM dall'ultimo blocco (memorizzato) e ricadendo su 120 se non c'è.

**B23 — lo snap viene armato *dopo* l'aggiornamento che dovrebbe consumarlo: `reset()` lascia una trappola che scatta sul primo knob.**
`plugin/source/ChainDsp.cpp:221-222` — `reset()` chiama `updateCoefficients()` e **poi** mette
`snapSmoothedOnNextUpdate = true`. `VocalForgeProcessor::reset()` non tocca `settingsDirty`, quindi
`setSettings()` non viene richiamata: il flag resta armato finché l'utente non muove qualcosa, e a
quel punto il movimento **non rampa, salta**.
Riproduzione (48 kHz, blocchi da 128, sinusoide 220 Hz, tutti i moduli spenti, `outGain` da 0 a
−24 dB a metà render): salto massimo fra campioni consecutivi attorno al movimento

* senza `reset()` prima: **0,0144** (uguale al regime, 0,0144 → nessun click)
* con `reset()` prima: **0,2130** — quindici volte il normale, cioè un click

È lo scenario del transport fermo mentre si gira una manopola: il caso più comune in cui l'host
chiama `reset()`.
Correzione: mettere `snapSmoothedOnNextUpdate = true` **prima** di `updateCoefficients()` in `reset()`
e in `prepare()` (in `prepare()` funziona solo perché `prepareToPlay` alza `settingsDirty`), o
semplicemente far eseguire lo snap direttamente dentro `reset()`.

**B24 — `ChainDsp::reset()` non azzera tutto: due render con `reset()` in mezzo differiscono di 38 dB.**
`plugin/source/ChainDsp.cpp:188-223` non tocca `roomCoeffCountdown`, `roomDepthSmoothed`,
`doublerPhase`, `gateGainDb`, `gateOpen`, né `delayTimeSamples`.
Riproduzione: stesso ingresso di 1 s, `p.reset()` prima di ciascuno dei due render, catena di
default a 48 kHz, blocchi da 512 → scarto **−38,42 dBFS**. Con `prepareToPlay()` al posto di
`reset()`: −300 dBFS. Un modulo alla volta:

| modulo acceso | scarto con `reset()` |
|---|---|
| room tamer | **−38,51 dBFS** (`roomCoeffCountdown` e `roomDepthSmoothed`) |
| doubler | **−27,33 dBFS** (`doublerPhase`) |
| tutti gli altri | −300 dBFS |

Perché è sbagliato: B15/B16 dicevano "due render della stessa istanza devono coincidere" e
`AudioProcessor::reset()` è stata implementata apposta. Il controllo di riproducibilità della suite
passa perché usa `prepareToPlay`, non `reset()` — cioè copre la strada che non è quella che l'host
percorre al ritorno del transport.
**Sottocaso, il gate parte aperto:** `:181` mette `gateGainDb = 0.0f` (= gate spalancato) mentre
`ChainDsp.h:205` mette `gateOpen = false`. Le due variabili si contraddicono, e vince il guadagno.
Misura (48 kHz, gate a −30 dBFS, range 40 dB, release 150 ms, rumore d'ingresso a −50 dBFS, cioè
20 dB sotto soglia): uscita **−50,4 dBFS al primo blocco**, −53,6 a 13 ms, −64,6 a 67 ms, −79,9 a
200 ms. Il rumore di stanza che il gate esiste per togliere passa intero per i primi ~200 ms di ogni
take. L'attacco invece non si perde: al primo suono forte il gate risale a −18,5 / −6,5 / −4,5 dB nei
primi tre blocchi e a −3,1 (= l'ingresso) entro 3 ms.
Correzione: in `reset()` azzerare anche quei cinque stati; e inizializzare
`gateGainDb = -settings.gateRange` quando `gateOpen` è falso.

#### RISCHI

**R9 — nessun coefficiente di filtro è smussato: ogni automazione di EQ, HPF, predelay e doubler schiocca.**
`ChainDsp.h:12-14` dichiara "tutti i continui passano da `SmoothedValue`". In pratica ci passano
nove guadagni; i ~40 parametri che finiscono in coefficienti di biquad o in un `setDelay()` vengono
scritti di colpo a ogni blocco.
Misura (48 kHz, blocchi da 128, sinusoide 220 Hz, parametro alternato fra due estremi ogni 4
blocchi; metrica = curvatura massima |x[i] − 2x[i−1] + x[i−2]|, che per una sinusoide pura vale
3·10⁻⁴ e per una discontinuità esplode):

| parametro | fermo | in automazione | rapporto |
|---|---|---|---|
| `tone1Gain` −9 ↔ +9 dB | 0,0001 | 0,0248 | **×179** |
| `fxTimeL` 8 ↔ 45 ms | 0,0003 | 0,0306 | ×118 |
| `fxDetune` 0 ↔ 18 | 0,0003 | 0,0282 | ×106 |
| `fxWidth` 0 ↔ 100 | 0,0003 | 0,0291 | ×102 |
| `revPredelay` 0 ↔ 80 ms | 0,0005 | 0,0302 | ×61 |
| `revPredelay` 18 ↔ 22 ms | 0,0005 | 0,0116 | ×24 |
| `hpfFreq` 40 ↔ 200 Hz | 0,0003 | 0,0107 | ×32 |
| `room1Freq` 80 ↔ 800 Hz | 0,0003 | 0,0027 | ×10 |
| `satDrive` 0 ↔ 100 (smussato) | 0,0003 | 0,0006 | ×1,7 |
| `dlyTime` 100 ↔ 1200 ms (smussato) | 0,0003 | 0,0003 | ×1,0 |

Le ultime due righe sono la prova che il metodo funziona dove è stato applicato. Le prime otto sono
quello che manca. Il caso peggiore — i guadagni dell'EQ — è anche quello che un'automazione tocca
davvero.
Correzione: almeno i **guadagni** (sub, tone, air, tilt, profondità room) vanno su `SmoothedValue` e
i coefficienti si ricalcolano al control rate già esistente del room tamer; per predelay e tempi del
doubler, la stessa rampa che si è già data al delay.

**R10 — `RulesEngine::compile()` è `const` ma non è rientrante.**
`plugin/source/RulesEngine.h:105` — `mutable juce::StringArray failedExpressions`, che `compile()`
azzera all'inizio (`RulesEngine.cpp:725`) e riempie durante. `compile()` è chiamata da
`applyPrompt()` (message thread) e da `setStateInformation()` (`PluginProcessor.cpp:572`), che in
VST3 può arrivare da un altro thread.
Riproduzione: due thread che chiamano `compile()` 400 volte ciascuno. Helgrind:
```
Possible data race during read/write of size 4
   by vf::RulesEngine::compile(juce::String const&, juce::String const&) const
```
Un `juce::StringArray` mutato da due thread è una `ReferenceCountedArray`: nel caso peggiore è una
doppia liberazione, non un avviso sbagliato.
Correzione: `failedExpressions` deve essere una variabile locale di `compile()` passata per
riferimento a `resolveParam`, non uno stato della classe.

**R11 — `onPresetGenerated` è una `std::function` letta e scritta da due thread.**
`PluginProcessor.cpp:515` e `:577` fanno `if (onPresetGenerated) onPresetGenerated();`,
`PluginEditor.cpp:204` la assegna e `:221` (distruttore dell'editor) la azzera, tutti senza
sincronizzazione. La chiamata da `setStateInformation` può quindi essere in corso mentre l'editor si
distrugge. L'`AsyncUpdater` di R5 protegge il *contenuto* della callback, non la callback stessa.
Correzione: un `std::atomic<bool>` non basta; serve un `juce::CriticalSection` attorno
all'assegnazione e alla chiamata, o far dipendere il processore da un `ChangeBroadcaster` invece che
da una `std::function`.

**R12 — divergenze di semantica fra i due motori che le fixture non prendono.**
Le 15 fixture di `examples/expression_fixtures.json` non contengono `**`, `%`, notazione
esponenziale, né una chiamata di funzione con arità sbagliata. Differenziale sui due valutatori con
lo stesso contesto delle fixture:

| espressione | C++ | Python | classe |
|---|---|---|---|
| `min(1, 2, 0)` | **1** (ok) | **0** (ok) | **divergenza silenziosa: nessuno dei due segnala** |
| `max(0, 1, 2)` | **1** (ok) | **2** (ok) | idem |
| `round(brightness, 1)` | **0** (ok) | **0,5** (ok) | idem |
| `round(2.345, 2)` | **2** (ok) | **2,35** (ok) | idem |
| `1.2.3` | **1,2** (ok) | errore | idem, al contrario |
| `2 ** 3` | errore | 8 | il C++ avvisa, Python no |
| `7 % 3` | errore | 1 | idem |
| `1e-3`, `1E3` | errore | 0,001 / 1000 | idem |
| `clamp(5, 0)`, `abs(-3, 9)`, `lerp(0,10,0.5,99)` | valore (ok) | **TypeError** | vedi B19 |

La riga peggiore è la prima: `RulesEngine.cpp:170-183` (`callFunction`) legge gli argomenti con
`arg(i)`, che ritorna 0 per quelli mancanti e **ignora quelli in più**, senza mai alzare `failed`.
Un `min(a, b, c)` scritto in `rules.json` compila in Python e nel plugin dà un altro numero, senza un
avviso da nessuna delle due parti — e la parità (che confronta i preset generati da Python con quelli
del C++) lo prenderebbe solo se un preset di `examples/` usasse quella funzione.
Correzione: in `callFunction`, controllare l'arità esatta e alzare `failed` se non torna; aggiungere
`**` e `%` (o rifiutarli anche in Python); rifiutare i numeri con più di un punto; e mettere queste
righe nelle fixture.

**R13 — l'avviso di regola non valutabile arriva all'utente, ma in fondo a un muro di testo.**
`PluginEditor.cpp:370-374` accoda gli avvisi in coda al `whyBox`, dopo l'elenco dei moduli e le note
di produzione, senza colore, badge o contatore. Un preset con un parametro finito a zero per una
regola rotta si presenta come un preset normale. Correzione: un contatore di avvisi visibile accanto
a FORGE, e il nome del parametro colpito nell'avviso.

#### DEBITO

**D1 — `ParamCache::get` costa più del DSP.**
`PluginProcessor.cpp:183-190`: ricerca lineare con confronto `juce::String == const char*` su ~90
voci, ripetuta ~90 volte per ogni `currentSettings()`, cioè circa 4000 confronti di stringa.
Misura (media su 3000 chiamate, `-O2`):

| sample rate | blocco | budget | blocco DSP | `currentSettings()` | % del budget | % del DSP |
|---|---|---|---|---|---|---|
| 96 kHz | 32 | 333 µs | 14,0 µs | **15,3 µs** | **4,6 %** | **110 %** |
| 96 kHz | 64 | 667 µs | 46,5 µs | 14,6 µs | 2,2 % | 31 % |
| 48 kHz | 32 | 667 µs | 14,1 µs | 15,8 µs | 2,4 % | 112 % |
| 48 kHz | 512 | 10667 µs | 219,2 µs | 15,8 µs | 0,15 % | 7 % |

Il costo è costante (non dipende dal blocco), quindi pesa esattamente dove fa male. Va detto che
`currentSettings()` gira solo quando `settingsDirty` è alzato — cioè **a ogni blocco durante
un'automazione**, che è il momento in cui il budget è già impegnato: a 32 campioni / 96 kHz muovere
un knob **più che raddoppia** il lavoro del plugin.
Correzione: risolvere gli id una volta sola in una struct di puntatori (`struct { std::atomic<float>*
inTrim, *outGain, …; }`), oppure ordinare `entries` e cercare in binario. La prima è banale e toglie
il costo del tutto.

**D2 — il ceiling non si applica al secondo canale con il clamp giusto.** Nessun difetto trovato:
verificato, `jlimit (-ceilingGain, ceilingGain, …)` è simmetrico e per‑canale. Segnalo solo che il
clamp è puro hard‑clip: sui primi ~6 campioni di un transiente (coefficiente d'attacco 0,66 a 48 kHz)
è il clamp e non l'inviluppo a tenere il tetto. Sui numeri misurati (THD ≤ 1,11 %) non è udibile, ma
è un clipper d'emergenza dichiarato come limiter.

**D3 — resta tutto il debito dei giri precedenti** che il quarto passaggio non ha toccato: i numeri
DSP scritti a mano in `ChainDsp.cpp` (il `×4` del duck a `:557`, le ballistics 5/180 ms a `:549-550`,
0,5/40 ms del de‑esser a `:406-407`, 10/120 ms del room tamer a `:356-357`, i 6 dB di isteresi del
gate a `:312`, i 700 Hz del tilt a `:262-263`, i 12 kHz dell'air a `:259`, gli 0,7 Hz e 0,004 s del
doubler a `:629-630`), il knee non dichiarato del ducking, e `getSample`/`setSample` campione per
campione in tutti gli stadi.

### Test deboli

Ognuno provato reintroducendo il difetto in una copia dei sorgenti fuori dal repo e rilanciando la
suite intera.

**T1 — "cambio di prompt in corsa senza click" (`selftest_main.cpp:344`) non prova niente.**
La metrica è `maxJump`, il massimo salto fra campioni consecutivi (`:220-228`), che su una sinusoide
di prova a 220 Hz vale già ~0,05: il segnale copre qualsiasi click.
Prova: cambiata `s->reset (sampleRate, 0.02)` in `s->reset (sampleRate, 0.0)` in `prepare()`, cioè
**tolto tutto lo smoothing** dei nove guadagni — il difetto esatto di R4.
```
corretto : ok  cambio di prompt in corsa senza click  (salto normale 0.0515, col cambio 0.0620)
R4 rotto : ok  cambio di prompt in corsa senza click  (salto normale 0.0515, col cambio 0.0620)
```
Numeri **identici**. Come renderlo vero: usare la curvatura (differenza seconda) invece della
differenza prima, e come segnale una sinusoide a bassa frequenza; oppure confrontare l'uscita col
cambio contro l'uscita senza cambio, che è quello che si vuole davvero misurare. Con la curvatura, il
cambio di prompt si stacca dal fondo di un fattore 100 (vedi R9).

**T2 — "il risultato non dipende dalla dimensione del buffer" (`selftest_main.cpp:1193-1201`) è cieco per costruzione.**
Confronta solo blocchi da **64 e 1024**: entrambi multipli di 16, cioè del periodo di aggiornamento
dei coefficienti del room tamer.
Prova: reintrodotto il difetto R1 nella forma più naturale — `roomCoeffCountdown = 0;` all'inizio di
`processRoomTamer`, cioè il contatore che riparte a ogni blocco invece di persistere.
```
suite intera:  ok  il risultato non dipende dalla dimensione del buffer  (blocchi 64 vs 1024: -200.0 dBFS)
               OK  tutti i controlli passati   (0 problemi)
sonda esterna: 1024 vs 64  = -300 dBFS   (nemmeno lei lo vede)
               1024 vs 37  = -60,9 / -71,0 / -74,3 dBFS  (Q 1 / 5 / 12)
               1024 vs 100 = -62,4 / -73,8 / -77,2 dBFS
               1024 vs 1   = -51,6 / -57,6 / -60,3 dBFS
```
Il difetto passa **tutti i 229 controlli**. Come renderlo vero: aggiungere almeno una dimensione che
non sia multipla di 16 — 37 e 100 bastano — e un caso con blocchi di dimensione variabile.

**T3 — "processBlock non alloca" (`selftest_main.cpp:22-28`, `:946-960`) non vede le allocazioni di JUCE.**
Il contatore sostituisce solo `operator new (std::size_t)`. Non sostituisce `operator new[]`, le
varianti `nothrow` e allineate, e soprattutto non intercetta `std::malloc` — che è quello che usa
`juce::HeapBlock`, cioè `AudioBuffer`, `dsp::DelayLine`, `dsp::StateVariableTPTFilter` e tutto il
resto del framework.
Prova: inserito in `processChunk`, subito dopo la costruzione della vista,
`juce::AudioBuffer<float> sprecato (1, 64); sprecato.clear();` — una `malloc` per ogni tratto, sul
thread audio.
```
ok    processBlock non alloca (regime)  (0 allocazioni in 10 blocchi)
ok    processBlock non alloca dopo un movimento di automazione  (0 allocazioni)
OK    tutti i controlli passati   (0 problemi)
```
Come renderlo vero: intercettare `malloc`/`calloc`/`realloc`/`free` (con `-Wl,--wrap=malloc` sul
binario di test), o almeno aggiungere `operator new[]` e le varianti allineate — ma senza `malloc` il
controllo continuerebbe a non vedere il caso più probabile.

**T4 — "spegnere il riverbero non tronca la coda di colpo" (`selftest_main.cpp:1267`) misura la rampa della mandata, non la coda.**
Guarda **il solo blocco successivo** allo spegnimento, cioè ~10,7 ms a 48 kHz con blocchi da 512:
tutto dentro i 20 ms di rampa di `revSendGain`. Passa (−21,3 dBFS) con una coda reale di 20 ms su un
riverbero da 3 s, cioè il difetto B21. Che stia guardando solo la rampa lo conferma la prova di T1:
togliendo lo smoothing dei guadagni **questo** controllo fallisce (−120 dBFS), pur essendo un
controllo sul riverbero.
Come renderlo vero: misurare per quanti secondi la coda resta sopra −60 dBFS dopo lo spegnimento e
pretendere che sia almeno una frazione dichiarata di `revDecay`; e aggiungere il caso spegni‑e‑
riaccendi, che oggi produce un picco a −5,73 dBFS con l'ingresso muto.

**T5 — "la coda dichiarata all'host contiene davvero il delay" (`selftest_main.cpp:1275`) non tocca il caso rotto.**
Mette `dlySync = 0` prima di misurare, cioè evita esattamente il ramo con il difetto B22, e confronta
contro una soglia al minimo: dichiara 9,55 s e chiede `>= 1,2`. Otto volte di margine.
Come renderlo vero: misurare la coda **vera** (impulso, poi silenzio, ultimo campione sopra −80 dBFS)
e pretendere `coda dichiarata >= coda vera`, con `dlySync` acceso e almeno due BPM diversi da 120 —
a 60 BPM e divisione 1/4 la vera è 7,00 s contro 3,74 dichiarati.

**T6 — `test_not_binds_looser_than_comparison` (`tools/tests/test_rules_integrity.py:71-74`) non può fallire per un difetto del C++.**
Valuta `not brightness > 0.5` solo con `evaluate()` di Python, cioè con l'AST di Python: è
tautologico. Il difetto che dovrebbe prendere sta in `RulesEngine.cpp`. L'unico controllo che
confronta davvero i due motori è `compareExpressions` in `parity_main.cpp`, guidato dalle 15 fixture.
Come renderlo vero: spostare i casi di precedenza nelle fixture (dove il C++ li rivaluta) e
aggiungerci le righe di R12; `test_fixtures_cover_the_precedence_traps` dovrebbe pretendere le
espressioni esatte, non la presenza delle sottostringhe `"not "`, `" and "`, `"/"`, `"-"`.

**T7 — `test_no_preset_carries_an_expression_warning` non può prendere B19.** Verifica che i preset
generati dalle regole *valide* non abbiano avvisi: per definizione non arriva mai al ramo d'errore.
Come renderlo vero: un test che inietta una `rules.json` con un'espressione rotta di ciascuna delle
quattro classi (variabile ignota, divisione per zero, sintassi, arità) e pretende un
`EXPRESSION_WARNING`, non un'eccezione — e lo stesso caso passato al C++ deve dare lo stesso avviso e
lo stesso valore.

**Controllo che invece ha i denti:** "nessun parametro finto: ogni controllo arriva al DSP".
Reintrodotto B10 (`s.satDrive = 30.0f;` fisso in `currentSettings()`):
`FAIL  nessun parametro finto: ogni controllo arriva al DSP  (inerti: satDrive)`. Funziona.

**Valgrind sulla suite intera**: zero letture/scritture non valide. Gli unici messaggi sono
`Mismatched free() / delete []` dovuti alla sostituzione parziale di `operator new` nel binario di
test (vedi T3), non al plugin.


---

## Quinto passaggio — correzioni del terzo giro

Chiusi B17–B24 e R9–R13. Ogni voce ha il controllo che la prende.

| # | cosa era | correzione | controllo |
|---|---|---|---|
| **B17** | `float copies[2]` scritto fino all'indice 7: stack smashing con 4 canali e doubler acceso | il doubler è stereo per costruzione: due copie, due indici | il controllo a quattro canali che già esisteva, ora con `fxOn` |
| **B18** | `processChunk` limitava i canali a 8 invece che a quelli *preparati* | i canali sono quelli preparati, scratch e filtri sono dimensionati su quelli | quattro canali + blocchi variabili |
| **B19** | in Python una regola rotta in un *parametro* era un traceback, non un avviso | `_safe_eval` copre condizioni, parametri e mandate; arità sbagliata → errore di regola | tre test: condizione, parametro, mandata rotti → avviso |
| **B20** | `delayTimeSamples` fuori dallo snap: il primo eco dei delay corti spariva | è nella lista dello snap | posizione dell'eco per divisione |
| **B21** | i bus si fermavano dopo la rampa (20 ms) e restavano **congelati**: riaccendendoli uscivano code vecchie a −5,7 dBFS | ogni bus gira per tutta la sua coda dopo lo spegnimento, poi **si azzera** | riaccensione a ingresso muto: niente ritorno di coda |
| **B22** | `getTailSeconds()` ignorava il sync: a 60 BPM 1/4 il bounce perdeva 3,3 s | la coda usa la stessa formula del bus, col BPM dell'ultimo blocco | coda dichiarata con 1/4 sincronizzato |
| **B23** | lo snap era armato *dopo* l'aggiornamento: il primo knob dopo `reset()` saltava (×15) | le impostazioni vere si applicano **prima** del reset, che snappa subito | click misurato su tono fermo, anche dopo `reset()` |
| **B24** | `reset()` non azzerava `roomCoeffCountdown`, `roomDepthSmoothed`, `doublerPhase`, `gateGainDb`, `gateOpen`; e il gate partiva **aperto** | tutti azzerati; il gate parte chiuso sul range corrente | riproducibilità e indipendenza dal buffer |
| **R9** | nessun coefficiente smussato: ogni automazione di EQ schioccava | guadagni di EQ sottrattiva, tonale, aria e tilt a rampa, coefficienti riscritti a control rate dentro gli stadi | click su tono fermo con EQ e output in movimento |
| **R10** | `compile()` `const` ma non rientrante (stato mutabile condiviso) | gli errori viaggiano per parametro, niente stato mutabile | — |
| **R11** | `onPresetGenerated` scritta e letta da due thread | contatore atomico che la UI segue dal suo timer | — |
| **R12** | `min(a,b,c)`, `max`, `round(x,n)` davano numeri diversi nei due motori; `**`, `%`, `1e-3` fallivano solo in C++ | arità vera, potenza, modulo col segno di Python, notazione scientifica; `round(x,n)` arrotonda la rappresentazione decimale | 27 espressioni di riferimento: **ha subito preso** `round(0.45, 1)` (0,5 in Python, 0,4 in C++) |
| **R13** | l'avviso di regola non valutabile finiva in fondo a un muro di testo | gli avvisi stanno in cima al pannello PERCHÉ | — |

### I test che non provavano niente

| # | perché era cieco | adesso |
|---|---|---|
| T1 | misurava il salto su un segnale rumoroso: dava lo stesso numero con e senza smoothing | tono fermo a 220 Hz, `outGain` in movimento, confronto col salto naturale del seno — e la stessa prova **dopo un `reset()`** |
| T2 | 64 e 1024 sono entrambi multipli del control rate: un contatore azzerato per blocco passava | blocchi 64, **37**, **100**, 512 e **1** contro il riferimento, avanzo compreso |
| T3 | contava solo `operator new`: una `malloc` vera era invisibile | `operator new[]` e `mallinfo2()`: si guarda l'heap, non solo l'operatore |
| T4 | misurava la rampa della mandata (20 ms) e la chiamava coda | la coda si misura a ingresso finito; e si verifica che riaccendere **non** faccia tornare la coda congelata |
| T5 | girava a 120 BPM, dove `dlyTime` coincide per caso col tempo sincronizzato | 1/4 sincronizzato con `dlyTime` volutamente diverso |
| T6/T7 | non potevano fallire per un difetto del C++ né prendere B19 | le 27 fixture verificano il C++; tre test verificano che una regola rotta diventi un avviso |

Due difetti erano **dei test, non del codice**: il render a blocchi lasciava crudo l'avanzo quando la
lunghezza non era multipla del blocco, e il controllo del click leggeva come discontinuità l'aumento
di pendenza legittimo di un boost di 9 dB.

Controlli headless: **231**. Test Python: **87**. Parità: 18 preset, 1695 valori, 0 differenze.
