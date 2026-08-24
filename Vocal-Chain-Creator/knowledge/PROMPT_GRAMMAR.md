# Grammatica del prompt — dal testo agli assi di intento

Il testo dell'utente non sceglie un preset: **muove degli assi numerici**. Le regole leggono solo gli assi.
Il vocabolario vero e proprio (con i pesi) sta in `tools/data/rules.json` → `lexicon`. Questo file spiega
il modello e come si estende. Lingue supportate: **italiano e inglese**, mescolabili nella stessa frase.

## Gli assi (schema/intent.schema.json)

| Asse | Range | Significato | Cosa muove nella chain |
|---|---|---|---|
| `brightness` | −1…+1 | scuro ↔ brillante | EQ tonale alta, air, tilt della saturazione |
| `weight` | −1…+1 | magro ↔ corposo | EQ tonale bassa, HPF, EQ sottrattiva 150–300 Hz |
| `aggression` | −1…+1 | morbido ↔ aggressivo | ratio e attacco dei compressori, drive |
| `density` | 0…1 | dinamica lasciata libera ↔ tutto livellato | soglie dei due compressori, MIX |
| `character` | 0…1 | trasparente ↔ colorato | drive e tipo di saturazione |
| `intimacy` | −1…+1 | distante ↔ vicino, sussurrato | prossimità, compressione, corpo, gate |
| `cleanliness` | 0…1 | tollerante ↔ chirurgico | gate, room-tamer, EQ sottrattiva |
| `vintage` | 0…1 | moderno ↔ vintage | tipo di saturazione, taglio dell'aria |
| `sibilance_control` | 0…1 | quanto togliere alle "s" | soglie e range dei due de-esser |
| `pitch_class` | low/mid/high | registro della voce | tutte le frequenze centrali si spostano |
| `delivery` | spoken/rapped/sung/screamed/whispered | tipo di esecuzione | gate, compressione, HPF |
| `genre` | (lista) | preset di partenza degli assi | sposta tutti gli assi in blocco, poi gli aggettivi correggono |
| `loudness_target` | LUFS | destinazione | output gain e limiter |

## Come si compone un intento
0. **Artista** (se riconosciuto): applica il suo vettore di assi, esecuzione, registro, target e mandate
   imposte. È il blocco più forte; gli aggettivi che seguono lo correggono. Vedi `ARTIST_PROFILES.md`.
1. **Base neutra**: tutti gli assi a 0 (o al default dichiarato in `rules.json` → `intent_defaults`).
2. **Genere** (se riconosciuto): applica il suo vettore di assi (`lexicon.genre`).
3. **Aggettivi e frasi**: ogni termine riconosciuto somma il proprio delta (`lexicon.descriptors`).
4. **Negazioni e mitigatori**: `non`, `poco`, `troppo`, `meno`, `senza`, `not`, `less`, `without`
   invertono o riducono il termine che segue (finestra di 3 parole).
5. **Intensificatori**: `molto`, `super`, `tanto`, `very`, `really`, `parecchio` moltiplicano (×1.5).
6. **Clamp** finale su tutti gli assi nel loro range.

Esempio — "*voce trap aggressiva ma non stridula*":
`genre=trap` (aggression +0.5, density +0.6, character +0.4, brightness +0.2)
→ `aggressiva` (+0.4 aggression) → `non stridula` (**negato**: −0.35 brightness, +0.25 sibilance_control).

## Termini non riconosciuti
Finiscono in `intent.unknown_terms[]` e la UI li mostra: *"non ho capito: 'nasale come Bowie'"*.
Non si inventa un intento. Se **nessun** termine è riconosciuto → profilo neutro dichiarato.

## Come si estende
1. Aggiungi il termine in `tools/data/rules.json` → `lexicon.descriptors`, con i delta sugli assi.
2. Aggiungi almeno un caso in `tools/tests/test_lexicon.py`.
3. Non toccare `chain_compiler.py`: se serve toccarlo, il modello è sbagliato, non il vocabolario.

## Vocabolario iniziale (estratto — la fonte di verità è `rules.json`)
- **Generi**: trap, drill, rap/hip-hop, pop, r&b, rock, metal/hardcore, indie, folk/cantautore,
  jazz/crooner, edm/dance, podcast, voiceover/speakeraggio, audiolibro.
- **Timbro**: caldo/warm, scuro/dark, brillante/bright, cristallina/crisp, ovattata, nasale, metallica,
  stridula/harsh, ariosa/airy, corposa/full, magra/thin, morbida/smooth, presente/upfront, radiofonica/radio.
- **Dinamica**: compressa, schiacciata, controllata, naturale, dinamica, punchy, densa, incollata/glued.
- **Carattere**: pulita/clean, calda a nastro/tape, valvolare/tube, sporca/dirty, saturata, analogica, digitale.
- **Esecuzione**: rappata, cantata, urlata/screamed, sussurrata/whispered, parlata/spoken, melodica.
- **Riferimenti d'uso**: da streaming, per YouTube, per podcast, demo, mix finale.
