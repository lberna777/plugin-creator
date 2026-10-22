# PLUGIN_SPEC — MIX COMPASS

> Contratto del plugin. Identità e ID dei parametri si congelano all'approvazione di questa scheda.
> Fase attuale: **MVP funzionale** (Fase 0 + Fase 2 del piano). La grafica finale è la Fase 4.

## 1. Identità (congelata al giorno 1)
- Nome prodotto (mostrato nel DAW): **MIX COMPASS**
- PLUGIN_CODE: **Mxcp** (libero: in uso ci sono Atfg, Mcps, Cs60, Rsu1, Vfrg, Seat)
- PLUGIN_MANUFACTURER_CODE: **Mypl** · COMPANY_NAME: **MyPlugins** (come tutti gli altri plugin)
- BUNDLE_ID: **com.myplugins.mixcompass**
- Versione iniziale: **0.1.0** (MVP; diventa 1.0.0 con la grafica finale). `VERSION` in CMake = unica fonte, mostrata in UI.

## 2. Cosa fa
- Categoria: **Analisi / Metering** — si mette **ultimo sul master** (dopo il limiter), in Logic come AU, in FL come VST3.
- In una riga: **carichi una traccia trap di riferimento e il plugin ti dice, in italiano chiaro, dove il tuo mix è diverso — e ti fa ascoltare l'A/B allo stesso volume.**
- Il suono del mix **non viene toccato**: in modalità *Mix* l'uscita è identica all'ingresso (null-test < −120 dBFS).

### Catena
```
                 ┌──────────────────────────────► (A) uscita = ingresso, bit-identico
ingresso (mix) ──┤
                 └─► FIFO lock-free ─► thread di analisi ─► risultati (atomic) ─► UI
file riferimento ─► thread di caricamento: decodifica + resample alla SR dell'host + analisi completa
                 └─► (B) uscita = riferimento in loop, al volume del tuo mix (level match)
```
- Lo switch **A/B** fa un crossfade di 20 ms (niente click).
- **Level match**: in B il riferimento suona al loudness del tuo mix (differenza tra i due integrated LUFS),
  così confronti il *suono* e non il *volume*. Disattivabile, per sentire anche la differenza di volume.

### Cosa misura (mix e riferimento, stessi algoritmi)
| misura | come | cosa ti dice |
|---|---|---|
| Loudness **integrated / short-term / momentary** | ITU-R BS.1770-4 / EBU R128 (K-weighting, gating −70 / −10) | quanto è forte, come lo misura Spotify |
| **True peak** | oversampling 4× | se il master rischia distorsione dopo la conversione mp3/aac |
| **PLR** (true peak − integrated) e **crest factor** | | quanto è schiacciato/compresso |
| **Spettro** a 1/6 d'ottava, 20 Hz–20 kHz | FFT, media a lungo termine, **normalizzato al loudness** | dove hai troppo/troppo poco, a parità di volume |
| **Larghezza stereo** per banda (bassi / medi / alti) | rapporto Side/Mid dall'FFT | se è troppo stretto o troppo largo |
| **Correlazione** per banda, con **mono sotto i 120 Hz** | | se i bassi sono mono (devono esserlo nella trap) |

### Le frasi ("cosa sta succedendo")
Il confronto produce al massimo **5 frasi**, ordinate per gravità, ognuna con il **consiglio pratico**. Esempi:
- «Il tuo **low-mid (200–400 Hz)** è **+3 dB** rispetto al riferimento → suono più impastato. Prova a togliere 2–3 dB in quella zona su beat o voce.»
- «Sei **4 LU più compresso** del riferimento (PLR 5 vs 9) → meno punch. Togli guadagno al limiter.»
- «I tuoi **bassi sotto i 120 Hz non sono mono** (correlazione 0.6) → in club e su mono si perdono. Metti il sub/808 in mono.»
- «Il tuo **sub (30–60 Hz)** è **−4 dB** → l'808 si sente meno. Alza l'808 o aggiungi saturazione.»
- «Sei **−3 LU più basso** del riferimento. Non è un difetto: su Spotify vengono portati tutti a −14 LUFS.»

Le zone in parole: **sub** 20–60 · **bassi** 60–120 · **low-mid** 120–400 · **medi** 400–2k · **presenza** 2k–6k · **aria** 6k–20k.
Una differenza sotto **±1.5 dB** di zona non genera frasi (rumore di misura, non un problema).

## 3. Parametri (APVTS, ID congelati)
| id | nome UI | tipo | range | default | smussato |
|----|---------|------|-------|---------|----------|
| `listenRef`  | A / B         | bool | Mix / Riferimento | **Mix** | crossfade 20 ms |
| `levelMatch` | Level match   | bool | on/off | **on** | gain smussato |
| bypass       | (bypass JUCE) | –    | – | off | – |

Stato **non** parametro (salvato nel progetto): percorso del file di riferimento.
Comandi UI (non automatizzabili): **Carica riferimento**, **Reset misura** (riparte l'integrated del mix).

## 4. I/O
- Bus: **stereo** e **mono** (in mono: larghezza/correlazione mostrate come "mono").
- Sidechain: no. Latenza: **0** (l'analisi non sta nel percorso audio).
- Sample rate: 44.1 / 48 / 88.2 / 96 kHz; il riferimento viene ricampionato alla SR dell'host.
- File di riferimento: **WAV, AIFF, MP3** (+ M4A/AAC tramite CoreAudio su macOS). Il file non viene copiato nel progetto: si salva il percorso.

## 5. UI (MVP funzionale, non definitiva)
```
┌───────────────────────────────────────────────────────────────────────┐
│ MIX COMPASS 0.1.0   [Carica riferimento] nome_file.wav   [A|B] [Match]│
├─────────────────────────────────────────┬─────────────────────────────┤
│ SPETTRO: tuo mix vs riferimento         │ LOUDNESS      tuo    rif    │
│ (due curve + curva differenza ±dB,      │ Integrated   -8.1   -6.9    │
│  zone sub/bassi/low-mid/… in basso)     │ Short-term   ...            │
│                                         │ True peak    ...            │
│                                         │ PLR          ...            │
│                                         │ STEREO  bassi/medi/alti     │
├─────────────────────────────────────────┴─────────────────────────────┤
│ COSA STA SUCCEDENDO (max 5 frasi, aggiornate ogni ~1 s)  [Reset]      │
└───────────────────────────────────────────────────────────────────────┘
```
- Disegno vettoriale in codice (niente asset nell'MVP), coordinate in `layout.json`, niente numeri magici.
- UI ← DSP solo via atomic / snapshot lock-free, refresh 30 Hz.

## 6. Libreria di analisi (riusabile)
`MixCompass/source/analysis/` — **header C++17 senza dipendenze da JUCE**, così VOCAL FORGE potrà adottarla:
`KWeighting.h`, `Loudness.h`, `TruePeak.h`, `FFT.h`, `Spectrum.h` (1/6 ottava + stereo per banda), `Compare.h` (frasi).
Validata da `tests/analysis_test.cpp` **prima** di essere usata nel plugin:
- EBU Tech 3341: sinusoide 1 kHz a −23 / −33 dBFS → −23.0 / −33.0 LUFS (±0.1); sequenze di gating (casi 3–5) → −23.0;
- true peak: sinusoide a fs/4 con fase 45° (picco tra campioni) → 0.0 dBTP (±0.3) contro −3 dB di sample peak;
- rumore rosa → spettro piatto a 1/6 d'ottava (±1 dB); sinusoide → banda giusta;
- correlazione: L=R → +1, L=−R → −1, rumori indipendenti → ~0;
- tutto a 44.1 / 48 / 96 kHz.

## 7. Formati & piattaforme
- **AU + VST3 + Standalone**, macOS universal (arm64 + x86_64).

## 8. Validazione richiesta
null-test (Mix, B spento) · pluginval strictness 10 su AU e VST3 · auval `aufx Mxcp Mypl` ·
mono e stereo · 44.1 / 48 / 96 kHz · blocchi piccoli e grandi · caricamento file durante il playback senza glitch.

## 9. Fuori dall'MVP (dopo)
Grafica finale fotorealistica · più riferimenti salvati · analisi per sezione (strofa/ritornello) ·
confronto della sola voce · sincronizzazione del riferimento al tempo del progetto.
