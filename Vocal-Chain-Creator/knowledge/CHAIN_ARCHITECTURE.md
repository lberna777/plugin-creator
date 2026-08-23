# Architettura della catena — 13 moduli, ordine fisso

L'ordine **non è configurabile**. Ogni posizione ha una ragione; spostare un modulo cambia il senso
del plugin, non il suo suono soltanto. I moduli si accendono e si tarano — non si riordinano.

```
in
 1 ── TRIM + POLARITY          porta il segnale al livello di lavoro (Scarlett gain sbagliato = caso normale)
 2 ── GATE (key-filtered)      taglia il noise floor della stanza PRIMA che qualsiasi cosa lo amplifichi
 3 ── HPF                      toglie plosive e modi bassi PRIMA dei detector dinamici
 4 ── ROOM-TAMER (3 notch din.) attenua le risonanze SOLO quando suonano, non sempre
 5 ── EQ SOTTRATTIVA (3 bande) toglie il resto (nasale, fango, flutter). Sottrarre prima di comprimere
 6 ── DE-ESSER 1               protegge il detector del compressore dalle sibilanti
 7 ── COMP 1 (veloce)          doma i picchi e i cambi di distanza dal mic. Max 6 dB
 8 ── COMP 2 (lento, glue)     livella la frase, dà densità. Max 6 dB
 9 ── SATURAZIONE              carattere e armoniche — DOPO la dinamica, così è prevedibile
10 ── EQ TONALE (3 bande + air) qui si AGGIUNGE: corpo, presenza, aria
11 ── DE-ESSER 2               ripulisce le sibilanti che saturazione e boost hanno rigenerato
12 ── LIMITER                  tetto di sicurezza, lookahead ≤ 2 ms, dichiarato
13 ── OUTPUT (+ MIX)           porta al target di loudness; MIX per compressione parallela
out
```

## Perché in quest'ordine

| # | Modulo | Perché proprio qui |
|---|---|---|
| 1 | Trim | tutto ciò che segue ha soglie in dB: se il livello d'ingresso non è normalizzato, ogni soglia è a caso. |
| 2 | Gate | se il gate stesse dopo la compressione, il rumore sarebbe già stato tirato su di 10 dB e non richiudibile. Il detector è **key-filtered** sulla banda della voce: altrimenti un ronzio a 50 Hz o una ventola a 8 kHz lo aprono. |
| 3 | HPF | i transienti di plosiva sotto i 100 Hz fanno lavorare il compressore su energia che non è voce. Vanno tolti prima. |
| 4 | Room-tamer | dinamico e non statico: la risonanza della stanza si eccita solo su certe note. Un notch fisso toglierebbe corpo a tutte le altre. Prima dell'EQ statica perché è correzione, non gusto. |
| 5 | EQ sottrattiva | "prima togli, poi aggiungi": ogni dB tolto qui è headroom guadagnato per i due compressori. |
| 6 | De-esser 1 | i compressori seguono l'energia: senza questo, ogni "s" innesca una riduzione di guadagno che abbassa la parola intera. |
| 7 | Comp 1 | attacco veloce, ratio medio: prende i picchi e le variazioni di distanza dal microfono (nessuno canta fermo). |
| 8 | Comp 2 | attacco lento, ratio basso: livella la frase. Due stadi leggeri > uno stadio pesante — meno pompaggio, più naturale. |
| 9 | Saturazione | dopo la dinamica il livello che la attacca è stabile, quindi la quantità di armoniche è prevedibile. Prima della dinamica, saturerebbe a caso. |
| 10 | EQ tonale | qui si aggiunge, su un segnale già pulito e già denso: i boost non riportano su fango o rumore. |
| 11 | De-esser 2 | saturazione e boost di presenza/aria **rigenerano** sibilanti. Un solo de-esser all'inizio non basta, e uno solo alla fine dovrebbe lavorare troppo. |
| 12 | Limiter | tetto, non effetto. Se il limiter lavora più di 2–3 dB, sono sbagliati gli stadi prima. |
| 13 | Output + Mix | il MIX è compressione parallela di tutta la catena: la via di fuga quando "troppo lavorato". |

## Cosa NON c'è dentro, e perché
- **Riverbero / delay**: la voce esce asciutta. In una stanza non trattata l'ambiente lo scegli tu,
  in mandata, dove puoi filtrarlo e temporizzarlo. Dentro la chain sarebbe ambiente su ambiente.
- **Tuning / pitch correction**: dominio diverso, latenza diversa, va prima o dopo secondo il gusto → resta fuori.
- **Width / stereo**: la voce è centrale, e su Scarlett a due ingressi lo "stereo" è quasi sempre dual-mono.
- **Riduzione di rumore spettrale**: rischio di artefatti alto e beneficio incerto su una fonte già gated. Fuori dalla v1.

## Punti di misura (per i test)
`M0` ingresso · `M1` post-trim · `M2` post-gate · `M3` post-EQ sottrattiva · `M4` post-comp1 ·
`M5` post-comp2 · `M6` post-saturazione · `M7` post-EQ tonale · `M8` uscita.
Su ognuno: picco, RMS, LUFS short-term. Nessun punto può superare 0 dBFS con ingresso a −6 dBFS.
