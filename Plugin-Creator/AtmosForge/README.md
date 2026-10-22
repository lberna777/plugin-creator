# ATMOS FORGE

Suite di **generazione di ambienti sonori procedurali** (AU / VST3 / Standalone).
Non contiene nemmeno un sample: ogni suono è sintetizzato in tempo reale, quindi
non si ripete mai e puoi renderizzarlo lungo quanto vuoi.

## Identità (congelata — non si rinomina più)

| campo | valore |
|---|---|
| PRODUCT_NAME | ATMOS FORGE |
| PLUGIN_CODE | `Atfg` |
| MANUFACTURER | `Mypl` (MyPlugins) |
| BUNDLE_ID | `com.myplugins.atmosforge` |
| tipo | strumento (synth), riceve MIDI |
| versione | 1.0.0 (unica fonte di verità: `CMakeLists.txt`) |

## Le quattro scene

| scena | com'è fatta |
|---|---|
| **STRADA** | rombo di fondo + veicoli in transito con **Doppler** reale (posizione in metri → distanza, panning, brillantezza), motore ad armoniche + pneumatici, clacson occasionali |
| **CAMINO** | soffio della fiamma (brown noise filtrato, con respiro irregolare) + sibilo + crepitii risonanti; ogni tanto uno schiocco con corpo di legno |
| **PIOGGIA / NEVE** | letto di precipitazione + gocce singole sparse in stereo + vento che vaga + tuoni. Il **TIMBRO** fa da meteo: a sinistra nevica, a destra diluvia |
| **PASSI** | tallone + punta, con materiale (legno / ghiaia / neve / cemento / metallo); su ghiaia e neve il passo è una nuvola di decine di micro-grani |

## I comandi

Le sei manopole sono le stesse in tutte le scene, ma cambiano mestiere
(la riga sotto il meter dice sempre cosa fanno **in quella scena**):

- **DENSITÀ** — quanti eventi (traffico, crepitii, intensità della pioggia, peso del passo)
- **MOVIMENTO** — velocità (transiti, vento, andatura). Spenta nel camino.
- **TIMBRO** — scuro ↔ brillante, e in PIOGGIA/NEVE fa da neve ↔ pioggia
- **DISTANZA** — vicino e asciutto ↔ lontano e ovattato
- **AMPIEZZA** — larghezza stereo (0 % = mono)
- **LIVELLO** — uscita in dB

Più: **MATERIALE** (passi e pneumatici), **AUTO** (eventi automatici on/off),
**SEME** (stesso seme = stessa sequenza, riproducibile).

### MIDI

Ogni nota innesca un evento: **sotto il DO centrale** la versione grossa
(tuono, camion, ciocco che scoppia, passo pesante), sopra quella normale.
Con **AUTO** spento il plugin tace finché non suoni: utile per sincronizzare
i passi a un'immagine.

### Export

`GENERA WAV` renderizza 5 s … 3 min (24 bit, stereo, con un secondo di pre-roll
scartato e fade di 20 ms ai bordi), poi **trascini il file dal riquadro dentro al DAW**.
`SALVA CON NOME` per scegliere dove metterlo.

## Build

```bash
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

AU e VST3 vengono copiati in `~/Library/Audio/Plug-Ins/`.

## Verifica

```bash
./build/probe_artefacts/Release/probe out/    # rende le 4 scene + misura livelli, mono/stereo, rate
./build/shoot_artefacts/Release/shoot ui.png  # render headless dell'editor vero
auval -v aumu Atfg Mypl                       # validazione Apple
```

Stato: `probe` OK, `auval` **SUCCEEDED**, build senza warning.
Manca `pluginval --strictness-level 10` (non installato su questa macchina).

## Struttura

```
source/
├── engine/
│   ├── Dsp.h           mattoncini (RNG, rumori, SVF TPT, inviluppi, modi risonanti)
│   ├── Scene.h         interfaccia comune + parametri
│   ├── SceneRoad       strada
│   ├── SceneFire       camino
│   ├── SceneWeather    pioggia / neve
│   ├── SceneSteps      passi
│   └── Engine          scelta scena, seme, DC block
├── PluginProcessor     APVTS, MIDI, render offline WAV
├── PluginEditor        UI (geometria da assets/layout.json)
├── LookAndFeel         disegno vettoriale
└── assets/layout.json  TUTTA la geometria, in coordinate 0..1
```

Aggiungere una scena = una classe che implementa `af::Scene` + una voce in `Engine`.
