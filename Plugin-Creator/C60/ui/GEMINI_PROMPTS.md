# Prompt per Gemini — UI del C-60

Come usarli:
1. Una **sola chat** Gemini (generazione immagini), così lo stile resta coerente.
2. Incolla prima il **Prompt 0 (stile)**, poi il **Prompt 1 (vista completa)**: è la reference.
3. Quando la vista completa ti piace, nella stessa chat incolla il **Prompt 2 (piastra vuota)** e poi i
   **Prompt 3.x (asset)** uno alla volta. Ogni asset è un file a sé.
4. Salva ogni immagine col **nome file indicato** e mettila in `Plugin-Creator/C60/ui/src/`.
   La pipeline (`tools/build_assets.py`) toglie lo sfondo verde, ritaglia e controlla le misure.

Regole che i prompt fanno già rispettare (CLAUDE.md dello scaffold):
- la piastra di fondo è **inerte**: niente scritte, numeri, scale, indicatori, display accesi;
- ogni parte che si muove o cambia stato è un **file separato**, vista frontale, su verde pieno;
- le scritte (etichette, valori, contatore, display) le disegna il codice.

---

## Prompt 0 — stile (incollalo per primo)

```
You are the industrial designer of a high-end 1985 cassette deck, built as a modular "best of"
of the greatest decks ever made. We will produce a set of photorealistic images that must share
ONE consistent design language. Remember this style for every image in this chat:

- Faceplate: matte black anodized brushed aluminium, fine horizontal brushing, like a Nakamichi Dragon.
- Accents: thin champagne-gold trim lines and bevels (Nakamichi), never shiny chrome.
- Side cheeks: solid American walnut, satin oil finish, rounded edges (Sony ES style).
- Transport keys: flat, wide, soft-touch dark grey keys with a thin colored LED slit (Revox B215 style).
- Display: a deep smoked glass window for a vacuum fluorescent display (Pioneer / Sony ES era).
  The VFD glow color is cyan-turquoise with amber and red accents.
- Knobs: solid machined aluminium, black anodized with a knurled grip ring and a gold-filled
  indicator line, like high-end Japanese hi-fi.
- Lighting: soft studio light from top-left, realistic subtle reflections, no lens flare, no bokeh.
- Rendering: photorealistic product photography, orthographic straight-on front view, no perspective
  distortion, sharp focus everywhere.
- Absolutely NO brand names, NO logos, NO readable text unless I explicitly ask for it.

Reply only "OK" — do not generate an image yet.
```

## Prompt 1 — vista completa (la reference)

```
Generate the complete front panel of this cassette deck, straight-on orthographic front view,
aspect ratio 16:9, the panel fills the whole frame edge to edge (no table, no room, no background).

Layout, left to right, matching this map exactly (percentages of width/height):

Walnut side cheeks: left 0–4 % and right 96–100 % of the width, full height.

TOP BAND (from 6 % to 55 % of the height):
- 6–36 % width: CASSETTE COMPARTMENT inspired by Nakamichi RX-505: a recessed bay with a smoked
  glass door; behind the glass a cassette is loaded, label facing us, the two tape reels visible
  through its window. Thin gold trim around the bay.
- 38–80 % width: VFD DISPLAY WINDOW, smoked glass, lit. Inside: two horizontal peak meters (L and R,
  segmented bars, with a peak-hold segment) across the top half; below them a large 7-segment
  time counter on the left, a thin horizontal "tape timeline" bar spanning the remaining width with
  several colored blocks on it (a collage of recorded passes) and two small markers IN / OUT;
  small status indicators along the bottom edge.
- 82–94 % width: CALIBRATION FLAP inspired by Nakamichi 1000ZXL / Revox: a small hinged aluminium
  door, shown OPEN, revealing two small trimmer knobs, two small square push buttons and two tiny
  LEDs inside a recess.

BOTTOM BAND (from 60 % to 93 % of the height):
- 6–36 % width: a row of 7 identical flat Revox-style transport keys, each with a thin LED slit;
  the first key's LED is red, the others amber or off. Below them two smaller keys.
- 38–66 % width: one LARGE knob on the left (record level), then 5 SMALL knobs in a row.
- 68–94 % width: one LARGE knob (output), one SMALL knob, then three rotary selector knobs with
  pointer skirts (5-position, 3-position, 2-position) and three small chrome-free black bat-lever
  toggle switches.

Small engraved labels may be suggested as illegible fine lines, but no readable words.
Make it look like the most desirable cassette deck ever built.
```

## Prompt 2 — piastra di fondo vuota (`bg_plate.png`)

```
Now generate the SAME front panel, same camera, same framing, same 16:9 aspect ratio, same
proportions and positions as the previous image, but as an EMPTY CHASSIS:
- remove every knob, key, switch, selector and LED: leave only their mounting holes, recesses and
  gold trim rings, so that separate parts can be placed on top later;
- the cassette bay is EMPTY and the door glass is removed: show only the dark recessed bay;
- the VFD window is OFF: plain dark smoked glass, nothing lit, nothing visible behind it;
- the calibration flap area is an empty dark recess (the flap will be a separate part);
- no text, no labels, no numbers, no scales, no logos anywhere.
Keep the brushed black aluminium, the gold trim lines and the walnut side cheeks identical.
```

## Prompt 3.x — asset separati

Regola comune: aggiungila in fondo a ogni prompt 3.x.

```
COMMON RULES: a single isolated object, centered, filling about 80 % of the frame, straight-on
orthographic view, on a perfectly flat solid pure green background (#00FF00), no shadow cast on
the background, no green reflections on the object, no text, no logos, same style and lighting
as the deck above. Square 1:1 image unless stated otherwise.
```

| # | File | Prompt (prima della regola comune) |
|---|---|---|
| 3.1 | `knob_large.png` | `The LARGE knob of the deck seen exactly from the front (top view of the knob face), its gold indicator line pointing straight UP at 12 o'clock.` |
| 3.2 | `knob_small.png` | `The SMALL knob of the deck seen exactly from the front, gold indicator line pointing straight UP at 12 o'clock.` |
| 3.3 | `selector.png` | `The rotary selector knob with a pointer skirt, seen exactly from the front, pointer pointing straight UP at 12 o'clock.` |
| 3.4 | `trimmer.png` | `A tiny calibration trimmer knob (slotted, gold-anodized), seen exactly from the front, slot vertical.` |
| 3.5 | `key_up.png` | `One transport key of the deck in its released (up) state, seen from the front, its LED slit OFF. Aspect ratio 2:1.` |
| 3.6 | `key_down.png` | `The same transport key in its pressed (down) state: slightly recessed, a bit darker, LED slit OFF. Aspect ratio 2:1.` |
| 3.7 | `key_small_up.png` / `key_small_down.png` | `The small key of the deck, released / pressed (two separate images), LED off. Aspect ratio 2:1.` |
| 3.8 | `toggle_up.png` / `toggle_down.png` | `The black bat-lever toggle switch seen from the front, lever pointing UP / DOWN (two separate images). Aspect ratio 1:2.` |
| 3.9 | `cal_flap.png` | `The closed calibration flap: a small brushed black aluminium door with a gold edge and a tiny finger notch, seen from the front. Aspect ratio 1:1.` |
| 3.10 | `cassette_type1.png` | `A blank compact cassette seen exactly from the front, label side, a plain cream paper label with no writing, the central window CLEAR so the hubs are visible, but WITHOUT tape reels and WITHOUT hubs inside (empty window). Ferric "normal" style. Aspect ratio 16:10.` |
| 3.11 | `cassette_type2.png` | `Same cassette, chrome style: smoky dark shell and a dark grey label, no writing, empty window. Aspect ratio 16:10.` |
| 3.12 | `cassette_type4.png` | `Same cassette, metal style: clear transparent shell with visible screws and a black-and-gold label, no writing, empty window. Aspect ratio 16:10.` |
| 3.13 | `reel_hub.png` | `A single white cassette tape hub (the small spool with six teeth in the center) seen exactly from the front, with NO tape wound on it.` |
| 3.15 | `button_sq_up.png` / `button_sq_down.png` | `The small square push button of the calibration panel, released / pressed (two separate images).` |
| 3.14 | `door_frame.png` | `The cassette bay door frame alone: the gold-trimmed frame of the glass door with the glass area cut out (that area must be pure green #00FF00). Aspect ratio 16:10.` |

Note per chi monta la UI (non per Gemini):
- il nastro avvolto sulle bobine, il vetro fumé, le luci del VFD, i LED accesi, le etichette e i
  valori li disegna il codice → restano corretti a ogni stato;
- i knob ruotano nel codice partendo dall'indicatore a ore 12;
- se un'immagine arriva con ombre sul verde, rigenerala: la pipeline scontorna solo il verde pieno.

Mappa controlli → funzioni (per il montaggio):

| Zona | Elemento | Funzione |
|---|---|---|
| Tasti trasporto (7) | key | REC · PUNCH IN · PUNCH OUT · AUTO PUNCH · REC MUTE · UNDO · MEMO |
| Tasti piccoli (2) | key_small | ERASE (cancella il nastro, con conferma) · EXPORT (WAV) |
| Knob grande sinistro | knob_large | RECORD LEVEL |
| 5 knob piccoli | knob_small | PITCH · WOW · FLUTTER · HISS · AGE |
| Knob grande destro + piccolo | knob_large / knob_small | OUTPUT · MIX |
| Selettori | selector | NR (OFF/B/C/S/DBX) · TAPE (I/II/IV) · SPEED (4.76/9.5) |
| Levette | toggle | HX PRO · AUTO GAIN · MONITOR (SOURCE/TAPE) |
| Sportello calibrazione | trimmer ×2, button_sq ×2, LED ×2 | BIAS · AZIMUTH · CAL · AUTO AZ |
| Display VFD | codice | meter L/R con picco, contatore, linea del nastro (collage), IN/OUT, indicatori |
| Vano cassetta | cassette_typeX + reel_hub + nastro disegnato | bobine che girano col DAW, nastro che passa da una bobina all'altra |
