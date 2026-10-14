# VOCAL FORGE su Logic — installazione veloce

Tre comandi, dieci minuti la prima volta (quasi tutti di attesa), poi il plugin è dentro Logic.

---

## Il percorso veloce

Apri **Terminale** (⌘ Spazio → "Terminale") e incolla, una riga per volta:

```bash
# 1. prendi il progetto
git clone -b claude/vocal-chain-plugin-hlh15v https://github.com/lberna777/plugin-creator.git
cd plugin-creator/Vocal-Chain-Creator/plugin

# 2. compila e installa (la prima volta scarica JUCE: qualche minuto)
./build_mac.sh --fast

# 3. apri Logic
```

`--fast` compila **solo per il processore del tuo Mac**: circa metà del tempo. Se un giorno ti serve un
plugin che gira anche sui Mac Intel, rilancia `./build_mac.sh` senza `--fast`.

Alla fine lo script scrive:

```
FATTO.
  AU   ~/Library/Audio/Plug-Ins/Components/VOCAL FORGE.component
  VST3 ~/Library/Audio/Plug-Ins/VST3/VOCAL FORGE.vst3
```

E prima di dirtelo ha già eseguito **`auval`**, la stessa validazione che fa Logic all'avvio: se passa lì,
Logic lo carica.

---

## Cosa ti serve prima (una volta sola)

Lo script controlla tutto da solo e ti dice cosa manca. In pratica servono due cose:

| Cosa | Come si installa | Quanto ci vuole |
|---|---|---|
| **Command Line Tools di Xcode** | lo script lancia l'installer da solo, oppure `xcode-select --install` | 5–10 min |
| **CMake** | `brew install cmake` (Homebrew: [brew.sh](https://brew.sh)) | 1 min |

Non serve Xcode completo: bastano i Command Line Tools.

---

## Usarlo in Logic

1. Apri Logic e la tua traccia vocale.
2. Nella strip, slot **Audio FX** → **Audio Units → MyPlugins → VOCAL FORGE**.
3. **Scegli un riferimento** dal menù **ARTISTA** — Sfera Ebbasta, Shiva, Tony Boy, Glockyy, Guè,
   Capo Plaza — e la catena si costruisce da sola: main, EQ, compressione, saturazione, riverbero,
   delay e doubler. Nel pannello PERCHÉ trovi anche le **note di produzione** (autotune, doppiaggi,
   ad-lib): sono la metà del suono che il plugin non fa e che devi fare tu in Logic.
   Dettagli: [`../knowledge/ARTIST_PROFILES.md`](../knowledge/ARTIST_PROFILES.md).
4. Oppure scrivi che voce vuoi, in italiano o in inglese — e puoi correggere un riferimento a parole
   (`capo plaza, ma meno brillante`):
   > `voce trap aggressiva ma non stridula`
   > `podcast, voce parlata pulita, senza rumore di fondo`
   > `r&b intimo, voce calda e morbida`
5. Premi **FORGE** (o Invio). La catena si costruisce: 13 moduli, valori calcolati sapendo che il segnale
   arriva da **stanza non trattata + microfono Focusrite + Scarlett a due ingressi**.
6. A destra, il pannello **PERCHÉ** ti dice il motivo di ogni valore. Se un valore non ha un perché, è un bug.
7. In basso, la sezione **SENDS**: riverbero, delay e doubler scelti per te. Sono **bus paralleli**,
   non sono in serie sulla voce.

### I due modi delle mandate

Il menù accanto al prompt sceglie come vivono riverbero e delay:

- **`internal`** (default) — girano dentro il plugin. Una traccia sola, zero setup: è il modo per iniziare.
- **`logic`** — i bus interni si spengono e il plugin ti dà i valori esatti da mettere su due **bus aux**
  di Logic (ChromaVerb, Tape Delay…). Serve quando vuoi mandare più voci allo stesso ambiente, o
  metterci le mani.

### Se non lo vedi in Logic

```bash
killall -9 AudioComponentRegistrar   # svuota la cache delle Audio Unit
```
poi riapri Logic. Se ancora non c'è: Logic → Impostazioni → Plug-in Manager → **Reimposta e riscansiona**.

Al primo avvio macOS può dire che il plugin "non può essere aperto": è la quarantena di Gatekeeper su un
binario non firmato. Si toglie così:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"VOCAL FORGE.component"
```

---

## Senza Logic: la versione Standalone

Lo script compila anche `VOCAL FORGE.app`: si apre da sola, scegli ingresso e uscita audio e provi la
catena sul microfono in tempo reale. Comoda per tarare il gain della Scarlett **prima** di registrare.

---

## Senza compilare niente: la ricetta con i plugin stock

Se vuoi la chain **subito**, senza toccare il Terminale, il progetto genera la stessa catena come ricetta
per i **soli plugin stock di Logic** (Channel EQ, Compressor, DeEsser 2, Phat FX, ChromaVerb…), con tutti i
valori e i motivi:

```bash
python3 tools/chain_compiler.py "voce trap aggressiva ma non stridula" --format recipe
```

Le trovi già pronte in [`../examples/`](../examples/) — un file `.logic.md` per ogni prompt d'esempio.
Si montano a mano in meno di cinque minuti e non dipendono da nulla.

---

## Aggiornare

```bash
cd plugin-creator && git pull && cd Vocal-Chain-Creator/plugin && ./build_mac.sh --fast
```

Le impostazioni salvate nei progetti Logic restano valide: identità e id dei parametri sono congelati
dal giorno 1 (`../PLUGIN_SPEC.md`).

---

## Se qualcosa va storto

| Sintomo | Cosa fare |
|---|---|
| `cmake: command not found` | `brew install cmake` |
| `xcrun: error: invalid active developer path` | `xcode-select --install` |
| La compilazione si ferma senza spiegazioni | `./build_mac.sh --clean` e rilancia |
| `auval` fallisce | il log completo è in `build/auval.log`: mandamelo |
| Logic non lo vede | `killall -9 AudioComponentRegistrar`, poi riapri Logic |
| Il plugin c'è ma "non si apre" | il comando `xattr` qui sopra |

## Cosa è già verificato, e cosa no

Verificato in automatico a ogni build (`build/` → `vocalforge_selftest`, `vocalforge_parity`):

- **null-test** a moduli spenti: differenza sotto −120 dBFS;
- nessun NaN e **nessun clipping** con ingresso a −6 dBFS, a 44.1 / 48 / 96 kHz, in mono e in stereo;
- cambio di prompt **mentre l'audio gira**: nessun click;
- le mandate sono **parallele**, non in serie;
- il **prompt fa parte dello stato**: salvi il progetto Logic, lo riapri, ritrovi testo e catena;
- **latenza dichiarata** (Logic la compensa solo se il plugin gliela dice);
- **parità** tra il motore C++ e il compilatore Python: 1026 valori, zero differenze.

Non ancora fatto, e lo dico invece di far finta:

- `pluginval --strictness-level 10` (va lanciato sul Mac, non c'era in questo ambiente);
- la **grafica definitiva**: quella che vedi è funzionale e disegnata a codice, non ancora la pelle
  fotorealistica con gli asset;
- l'analisi del segnale reale (fase F3): oggi le regole partono dalle stime del profilo di stanza/microfono,
  non da una misura del tuo segnale.
