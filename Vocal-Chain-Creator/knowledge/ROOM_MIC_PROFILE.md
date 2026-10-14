# Profilo di sorgente — stanza non trattata + Focusrite + Scarlett

Questo file è la **costante del progetto**: quello che sappiamo del segnale *prima* di sentirlo.
Serve a giustificare scelte che un plugin generico non può permettersi. I numeri qui sono **stime di partenza**;
quando (F3) esisterà l'analisi del segnale, le misure le sovrascrivono e il `why` deve dichiararlo.

## 1. La stanza (non trattata, piccola, 3×4 m circa)
| Fenomeno | Dove | Effetto sul segnale | Contromossa nella chain |
|---|---|---|---|
| Modi assiali | 45–120 Hz | code lunghe, "rimbombo" | HPF 80–110 Hz + notch dinamico basso |
| Boxiness | 150–400 Hz | voce "in una scatola", fango | notch dinamico + EQ sottrattiva stretta |
| Riflessioni precoci (< 15 ms) | larga banda | comb filtering, immagine confusa | non correggibile a valle → ripresa ravvicinata + gate stretto |
| Flutter echo | 1–4 kHz | asprezza, "ping" sulle consonanti | EQ sottrattiva su 1 banda, Q medio |
| Noise floor | −55…−45 dBFS | ronzii, ventole, strada | gate key-filtered con hysteresis, mai downward expansion pesante |

**Conseguenza forte:** in una stanza così l'ambiente è **rumore**, non carattere. La voce esce **asciutta**
dalla catena e l'ambiente (riverbero/delay) si aggiunge dopo, in mandata. Vedi `LOGIC_INTEGRATION.md`.

## 2. Il microfono (Focusrite, cardioide, ripresa ravvicinata)
- **Effetto prossimità**: +3…+8 dB sotto i 200 Hz a 5–10 cm. → HPF non opzionale, e attenuazione 150–250 Hz.
- **Sibilanti**: capsula entry-level + ravvicinato → energia marcata **5–9 kHz** (voci maschili ~6–7 kHz,
  femminili ~7–9 kHz). → de-esser **sempre** presente, sdoppiato pre/post saturazione.
- **Plosive**: senza filtro anti-pop, transienti < 100 Hz da decine di dB. → HPF a slope ripida (24 dB/oct) come default.
- **Presenza**: molti mic economici hanno un picco 3–6 kHz già suo. → l'EQ tonale **non** somma boost lì
  senza motivo esplicito nel testo ("più avanti", "radio").
- Se il mic è **dinamico** (tipo broadcast): meno sibilanti, meno stanza, più necessità di gain e di presenza.
  Se **condensatore**: più aria, più stanza, più sibilanti. Il profilo distingue i due casi (`mic.type`).

## 3. L'interfaccia (Scarlett, due ingressi)
- **Preamp pulito**: nessun carattere da preservare. Tutta la "colorazione" la mette la saturazione della chain.
- **Headroom limitato + gain sbagliato dall'utente** è il caso normale, in due direzioni:
  - troppo basso → si alza il noise floor a valle (il gate diventa critico);
  - troppo alto → picchi vicini a 0 dBFS, rischio di clipping in ingresso (irrecuperabile: va detto all'utente).
- **Due ingressi** = due scenari da supportare:
  1. **una voce sola** (input 1): mono→stereo, caso di default;
  2. **doppio microfono / voce + strumento** su input 1+2: la traccia arriva stereo ma **non è** un'immagine stereo.
     → il plugin **non deve** trattarla come stereo: nessun width, nessuna elaborazione L/R differenziata.
     Il preset dichiara `io.stereo_is_dual_mono: true`.
- Livello di lavoro consigliato in registrazione: picchi **−12 dBFS**, medio ~−18 dBFS.
  L'`inTrim` della chain porta il segnale reale a quel target; il target è nel preset, non nel codice.

## 4. Codifica nel preset
```json
"source_profile": {
  "id": "untreated_room_focusrite_scarlett",
  "room": { "treated": false, "size": "small", "noise_floor_dbfs": -50,
            "modes_hz": [90, 180, 240], "boxy_band_hz": [150, 400], "flutter_band_hz": [1000, 4000] },
  "mic":  { "brand": "focusrite", "type": "condenser", "pattern": "cardioid",
            "distance_cm": 10, "proximity_boost_db": 5, "sibilance_center_hz": 7000 },
  "interface": { "brand": "focusrite", "model": "scarlett_2i2", "inputs": 2,
                 "preamp_character": "clean", "target_peak_dbfs": -12 },
  "io": { "stereo_is_dual_mono": true }
}
```

## 5. Cosa NON assumiamo
- Non assumiamo che l'utente abbia un filtro anti-pop, un'asta decente o una posizione fissa.
- Non assumiamo che il take sia uno solo: la chain deve reggere una voce che cambia distanza dal mic
  (→ due stadi di compressione, il primo veloce, sono lì per questo).
- Non assumiamo il genere della voce: il `pitch_class` (`low`/`mid`/`high`) viene dal testo o dalla misura,
  mai dal nome dell'utente o da un default implicito.
