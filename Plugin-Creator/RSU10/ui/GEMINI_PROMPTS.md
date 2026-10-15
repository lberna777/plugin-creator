# Prompt per Gemini — UI dell'RSU-10

L'RSU-10 è il campionatore a 16 pad da suonare con la Maschine Mikro MK2 su FL Studio.
L'idea è quella di un **restomod**: la carrozzeria è un campionatore portatile Yamaha del 1995
(riferimento: `reference/su10_photo.png`), l'anima è moderna. Ogni elemento è **o originale anni '90
o componente moderno di pregio**, mai una via di mezzo.

Come usarli:
1. Una **sola chat** Gemini (generazione immagini), così lo stile resta coerente.
2. Allega la foto dell'SU10 e incolla il **Prompt 0 (stile)**, poi il **Prompt 1 (vista completa)**:
   quella è la reference. Rigenera finché non ti piace, prima di andare avanti.
3. Nella stessa chat incolla il **Prompt 2 (piastra vuota)**, poi i **Prompt 3.x (asset)** uno alla volta.
4. Salva ogni immagine col **nome file indicato** in `Plugin-Creator/RSU10/ui/src/`.
   La pipeline (`tools/build_assets.py`) toglie lo sfondo verde, ritaglia e controlla le misure.

Cosa disegna il codice, e quindi NON deve stare nelle immagini: tutte le scritte (etichette dei tasti,
nome RSU-10, numeri dei pad), il contenuto dei due schermi (forma d'onda, browser, valori), gli anelli
LED delle manopole e dei pad, la luce del ribbon.

---

## Prompt 0 — stile (incollalo per primo, con la foto dell'SU10 allegata)

```
We are designing a "restomod" of the attached 1995 portable sampler: like a restomod classic car,
the original body and its design language are kept and respected, but the internals and a few
components are unmistakably modern and high-end. The two eras overlap on purpose. Remember this
style for every image in this chat.

ORIGINAL 1995 BODY (keep it faithful to the photo):
- Housing: slate blue-grey injection-moulded plastic with a fine speckled / granite fleck texture,
  softly rounded edges, slightly matte. Same colour and texture as the attached photo.
- Screen bezels: flat matte black frames, exactly like the LCD frame in the photo.
- Small function keys: light warm-grey rectangular plastic keys, slightly convex; one of them is a
  pale violet key (like EXIT in the photo).
- Transport keys: dark charcoal rectangular rubber keys; the record key is a round glossy red button.
- Pads: dark charcoal soft-touch rubber pads with rounded corners and a slightly domed top,
  like the 12 big keys in the photo.
- Ribbon window: a long glossy black glass window, like the dark rectangle top-right in the photo.

MODERN SOUL (new components, premium, clearly from today):
- Knobs: solid machined aluminium knobs, bead-blasted, dark titanium-grey anodized, with a fine
  knurled grip band and a thin engraved indicator line. Around each knob a slim recessed ring that
  will hold an LED value arc (shown unlit unless I ask).
- Inserts: two plates of CNC-machined dark anodized aluminium set flush into the plastic body,
  with crisp chamfered edges, like a modern billet part fitted into a classic car.
- Screens: behind the 1995 black bezels sit modern high-resolution displays (deep black when off,
  crisp and colourful when on — not a green LCD).
- Pads: around each rubber pad a thin translucent ring that can glow RGB (unlit unless I ask).

RENDERING: photorealistic product photography, orthographic straight-on top-down view, no
perspective distortion, soft even studio light from the top-left, subtle realistic reflections,
sharp focus everywhere, no lens flare, no bokeh, no hands, no cables.
Absolutely NO brand names, NO logos, NO readable text, NO numbers unless I explicitly ask.

Reply only "OK" — do not generate an image yet.
```

## Prompt 1 — vista completa (la reference, `reference_full.png`)

```
Generate the complete top panel of this restomod sampler, straight-on orthographic top-down view,
aspect ratio 16:10, the panel fills the whole frame edge to edge (no table, no background, the
rounded body edges just touch the frame).

Layout, matching this map exactly (percentages of the image width / height):

HEADER STRIP (3–10 % of the height):
- 26–50 % width: a small screen window in a black bezel (kit name display), lit, showing an
  abstract short line of light, no readable text.
- 52–60 % width: two small light-grey function keys side by side.
- 78–97 % width: a machined aluminium insert plate (badge area), empty, no text.

LEFT COLUMN (3–24 % width, 13–95 % height):
- a tall screen in a black bezel (sample browser), lit: a modern dark list with one highlighted row
  and small waveform thumbnails, abstract, no readable text.

CENTRE COLUMN (26–70 % width):
- 13–50 % height: the MAIN SCREEN in a black bezel exactly like the photo's LCD frame, with a thin
  red silkscreen line suggested along the bottom of the bezel. Lit: a crisp modern waveform in
  warm white and amber on deep black, with start/end markers, a soft fade curve and a small grid,
  abstract, no readable text.
- 54–70 % height: the machined aluminium insert plate, full width of the column, holding a row of
  8 identical aluminium knobs, evenly spaced, each with its LED arc lit in a soft amber at
  different values.
- 74–95 % height: left part, a 2x3 block of small light-grey keys (the top-right one pale violet);
  centre part, a row of 4 wide dark charcoal rubber keys; right part, one wide light-grey key on
  top, two pairs of tiny light-grey keys below it.

RIGHT COLUMN (72–97 % width):
- 13–25 % height: the long glossy black RIBBON window, with a soft modern LED glow under the glass
  in one spot, as if a finger had just slid across it.
- 28–35 % height: a row of 6 transport keys: first a round glossy red button, then 5 dark charcoal
  rectangular rubber keys.
- 39–95 % height: a 4x4 grid of 16 identical big rubber pads, square with rounded corners, evenly
  spaced; three of them with their thin RGB ring glowing (one amber, one cyan, one white).

Small printed labels may be suggested as illegible fine lines, but no readable words.
It must look like the original 1995 machine was lovingly rebuilt today with premium parts.
```

## Prompt 2 — piastra di fondo vuota (`bg_plate.png`)

```
Now generate the SAME panel, same camera, same framing, same 16:10 aspect ratio, same proportions
and positions as the previous image, but as an EMPTY CHASSIS:
- keep the speckled slate blue-grey body, the two aluminium insert plates and every black screen
  bezel exactly where they are;
- all screens OFF: deep black glass, no content, no glow;
- the ribbon window OFF: plain glossy black;
- REMOVE every knob (leave only the flat aluminium plate and the empty recessed rings where the
  knobs sat), every key and every pad: where they were, show only the flat plastic surface with
  the shallow recess or opening that holds them;
- no text, no labels, no printed lines, no LEDs lit.
```

## Prompt 3 — FOGLIO UNICO (consigliato: tutti i pezzi in una sola immagine)

Un'immagine sola con i 9 pezzi a riposo; lo script li ritaglia e crea da sé gli stati premuti.

```
Now generate ONE single image, a parts sheet: the 9 separate components below, each shown ALONE,
laid out in a neat 3x3 grid (3 rows, 3 columns), in exactly this order, read row by row:

Row 1: (1) one aluminium knob, engraved indicator line pointing straight up (12 o'clock), without
       its LED ring | (2) one big rubber pad from the 4x4 grid, at rest, without its RGB ring |
       (3) the round glossy red record button.
Row 2: (4) one small light-grey function key | (5) the pale violet function key |
       (6) the wide light-grey ENTER-style key.
Row 3: (7) one dark charcoal rectangular rubber transport key | (8) one wide dark charcoal rubber
       key from the row of 4 under the knobs | (9) one tiny light-grey key (for -1 / +1 and arrows).

All parts at rest (not pressed), same straight-on top-down orthographic view, same lighting and
materials as the panel, keep their real relative sizes. Every part fully separated from the others
by a wide empty gap (at least 1/10 of the image), nothing touching, nothing overlapping, nothing
cut off by the image border.
Perfectly flat pure green background (#00FF00), no shadows on the background, no gradient, no
reflections of green on the parts. No text, no numbers, no labels, no logo. Square 1:1 image.
```

Salva l'immagine come `ui/src/foglio.png`, poi da `RSU10/ui`:

```bash
python3 ../../tools/slice_sheet.py src/foglio.png --out src --pressed --preview src/foglio_check.png --names knob pad_up key_rec_up key_grey_up key_violet_up key_enter_up key_rubber_up key_wide_up key_tiny_up
```

Escono i 17 file della tabella sotto (i `_down` generati dallo script); quelli che hai già fatto
a mano non vengono toccati (`--force` per rifarli). Se lo script dice che ha trovato un numero
sbagliato di pezzi, guarda `src/foglio_check.png` e rigenera chiedendo più spazio tra i pezzi.
Se gli stati premuti generati non ti piacciono, stesso prompt con "all parts PRESSED DOWN" e
`--names knob pad_down key_rec_down …` (senza `--pressed`, il knob poi lo cancelli).

## Prompt 3 (vecchio metodo) — asset uno per immagine, sempre nella stessa chat

Regola comune: aggiungi in fondo a ogni prompt 3.x questo blocco.

```
Show ONLY this single object, centred, straight-on top-down orthographic view, same lighting and
materials as the panel, filling about 70 % of the frame, on a perfectly flat pure green background
(#00FF00) with no shadow on the background, no gradient, no reflections of green on the object.
No text, no numbers, no logo. Square 1:1 image.
```

| # | File | Prompt |
|---|------|--------|
| 3.1 | `knob.png` | `One aluminium knob from the panel, with its engraved indicator line pointing straight up (12 o'clock). Without the LED ring around it.` |
| 3.2 | `pad_up.png` | `One rubber pad from the 4x4 grid, at rest. Without the RGB ring around it.` |
| 3.3 | `pad_down.png` | `The same rubber pad, pressed down: slightly lower, the dome a little flattened, a bit darker in the centre.` |
| 3.4 | `key_grey_up.png` | `One small light-grey rectangular function key, at rest.` |
| 3.5 | `key_grey_down.png` | `The same light-grey key, pressed down, slightly sunk and a touch darker.` |
| 3.6 | `key_violet_up.png` | `The pale violet function key, at rest.` |
| 3.7 | `key_violet_down.png` | `The pale violet key, pressed down.` |
| 3.8 | `key_rubber_up.png` | `One dark charcoal rectangular rubber transport key, at rest.` |
| 3.9 | `key_rubber_down.png` | `The same rubber transport key, pressed down.` |
| 3.10 | `key_wide_up.png` | `One wide dark charcoal rubber key from the row of 4 under the knobs, at rest.` |
| 3.11 | `key_wide_down.png` | `The same wide rubber key, pressed down.` |
| 3.12 | `key_rec_up.png` | `The round glossy red record button, at rest.` |
| 3.13 | `key_rec_down.png` | `The round red record button, pressed down.` |
| 3.14 | `key_enter_up.png` | `The wide light-grey ENTER-style key (the single wide key above the tiny keys), at rest.` |
| 3.15 | `key_enter_down.png` | `The same wide light-grey key, pressed down.` |
| 3.16 | `key_tiny_up.png` | `One of the tiny light-grey keys (used for -1 / +1 and arrows), at rest.` |
| 3.17 | `key_tiny_down.png` | `The same tiny light-grey key, pressed down.` |

Per i tasti va bene anche il rapporto 2:1 invece di 1:1: la pipeline ritaglia comunque sull'oggetto.

---

## Controllo prima di consegnare gli asset

- [ ] `reference_full.png` ti piace così com'è (è il riferimento per misurare le posizioni)
- [ ] `bg_plate.png` combacia con la reference: stesse cornici negli stessi punti
- [ ] nessuna immagine ha scritte, numeri o loghi
- [ ] negli asset, sfondo verde pieno e senza ombre
