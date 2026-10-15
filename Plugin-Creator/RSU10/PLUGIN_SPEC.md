# PLUGIN_SPEC — RSU-10

> Scheda decisa con Lorenzo il 2026-10-15. Identità e id dei parametri sono congelati.

## 1. Identità (congelata)
- Nome prodotto: **RSU-10** (Restomod Sampling Unit)
- PLUGIN_CODE: **Rsu1** · manufacturer: **Mypl** · COMPANY_NAME: **MyPlugins**
- BUNDLE_ID: **com.myplugins.rsu10** · versione iniziale **0.1.0**
- Formati: **AU + VST3** (+ Standalone), macOS universal; target **FL Studio sul Mac mini**

## 2. Cosa fa
Campionatore a 16 pad da suonare con la **Maschine Mikro MK2** (via Mikro Bridge) o con qualunque
controller: carichi o crei un kit trascinando i campioni sui pad, li modifichi e registri la batteria
in FL sopra la melodia. Estetica **restomod dello Yamaha SU10** (1995): carrozzeria originale,
anima moderna.

| Area | Funzioni |
|---|---|
| Pad | 4 banchi A–D × 16 = 64 pad; C1 (36) = A1 … D#6 (99) = D16, la Mikro cambia banco con SHIFT + pad 15/16 |
| Strati | fino a 4 campioni per pad scelti dalla velocity (soglie regolabili) |
| Taglio | inizio, fine, punto di loop, fade in/out, trascinabili sulla forma d'onda |
| Modi (SU10) | one-shot, gate, loop (anche da un punto), reverse e reverse-loop |
| Suono | volume, pan, intonazione ±24 st + fine, ADSR, filtro LP/HP/BP con risonanza, velocity → volume/filtro |
| Carattere | **grade** dell'SU10 (High 44,1 · Standard 32 · Long 22 · Extra Long 11,64 kHz + bit ridotti), saturazione, compressore a una manopola |
| Batteria | gruppi choke 1–8, MONO (il pad zittisce sé stesso) |
| Stretch | loop adattato al tempo di FL senza cambiare l'altezza (AUTO o 1–16 battute), tempo del loop mostrato in BPM |
| Uscite | MAIN + OUT 2–16 stereo verso il mixer di FL |
| Ribbon (SU10) | SCRATCH, PITCH (±1–24 st), FILTER, REV, X-FADE (campioni ↔ ingresso), HOLD; dalla Mikro con l'encoder (CC 14), da tastiera con la rotella (CC 1) |
| Scala (SU10) | un pad su tutte le note, C3 = altezza originale |
| Campionamento | REC dall'ingresso del plugin o dalla sua uscita (ricampionamento), parte da solo sul segnale, file in ~/Music/RSU-10/Registrazioni |
| Kit | salva/carica `.rsu10kit` con i campioni copiati accanto (kit portabili); kit di prova sintetizzato incluso |
| Browser | cartelle Musica/Scrivania/Download/RSU-10, ascolto al clic, trascinamento sui pad (cartella intera = kit), -1/+1 scorre i campioni della cartella del pad |

## 3. Parametri del DAW (automatizzabili, id congelati)
`master`, `ribbon`, `ribbonTouch`, `ribbonMode`, `ribbonHold`, `ribbonRange`, `scale`, `scaleRoot`.
I 29 parametri di ognuno dei 64 pad (`PadParams.h`) vivono nel kit e nel progetto, non
nell'automazione (sarebbero 1856).

## 4. UI
- Finestra 16:10, ridimensionabile (960×600 … 1920×1200), coordinate in `source/assets/layout.json`
  = le percentuali dei prompt Gemini (`ui/GEMINI_PROMPTS.md`).
- Senza asset il plugin si disegna da codice; ogni PNG di Gemini messo in `source/assets/` con il nome
  dei prompt prende il posto del disegno (`ui/Skin.cpp`).
- Codice, mai cotti nell'immagine: scritte, contenuti dei due schermi, anelli LED, luce del ribbon.
