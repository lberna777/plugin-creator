# VOCAL FORGE — revisione tecnica ostile

Branch: `claude/vocal-chain-plugin-hlh15v` · revisione fatta leggendo il codice e **misurando il binario vero**
(gli oggetti già compilati di `vocalforge_selftest` linkati a piccoli programmi di prova fuori dal repo,
più Valgrind). Nessun file del progetto è stato modificato.

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
