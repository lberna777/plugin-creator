# UI_DESIGN — VOCAL FORGE

Specifica visiva dell'interfaccia. Nasce dal design canvas in `reference/design/` e serve a due cose:
montare la UI da `layout.json` senza numeri magici nei `.cpp`, e sapere per ogni elemento se è
**baked** (piastra inerte) o **codice/asset mobile**.

Palette e tipografia non sono state inventate: sono quelle già usate da `plugin/source/PluginEditor.cpp`.

## Finestra

| | |
|---|---|
| Dimensione di riferimento | **1080 × 680 px** (aspect **1.588 : 1**) |
| Ridimensionabile | sì, da 900 × 560 a 1600 × 1100 |
| Regola di scala | tutte le posizioni sono normalizzate 0..1 sul rettangolo della finestra |

## Palette

| Ruolo | Hex | Dove |
|---|---|---|
| Piastra scura | `#14161a` | fondo finestra, incassi |
| Piastra media | `#1d2127` | pannelli, card dei moduli |
| Piastra chiara | `#262b33` | modulo selezionato, bordi interni |
| Inchiostro | `#d8dde5` | testo primario, serigrafia |
| Inchiostro tenue | `#8b94a3` | etichette, motivazioni, valori secondari |
| Ambra (accento) | `#e0a04a` | corona dei knob, bottone FORGE, titoli PERCHÉ |
| Azzurro (accento freddo) | `#4aa3e0` | meter di gain reduction, sezione SENDS |
| Verde LED | `#6fdc8c` | modulo attivo, meter di uscita in sicurezza |
| Rosso allarme | `#c9552f` | limiter che lavora, avvisi, uscita oltre −1 dBFS |

Gradienti della piastra: `linear-gradient(180deg, #23272e 0%, #16191d 100%)` per i pannelli in rilievo,
`inset 0 1px 0 rgba(255,255,255,.06)` come luce superiore. Gli incassi (meter, prompt box) usano
`inset 0 1px 2px rgba(0,0,0,.75)`.

## Tipografia

| Uso | Famiglia | Note |
|---|---|---|
| Serigrafia / etichette | **Barlow Condensed** 600–700 | maiuscolo, `letter-spacing` .12–.22em |
| Testo corrente (PERCHÉ, note) | **Barlow** 400–500 | 11–13 px, interlinea 1.5 |
| Valori numerici | **IBM Plex Mono** 400–500 | cifre tabulari: i valori non devono ballare |

Fallback: `Arial Narrow` / `system-ui` / `ui-monospace`. In JUCE si usano le corrispondenti famiglie
imbarcate; se mancano, la gerarchia regge lo stesso perché è data da peso e spaziatura, non dal disegno.

## Baked vs codice

**Baked — la piastra inerte** (un solo file di sfondo, nessun testo che cambia):
fondo e gradiente, cornici dei pannelli, separatori, incassi dei meter, texture, viti.

**Codice — mai baked** (cambia, si muove o mostra stato):
testo del prompt e placeholder · nome dell'artista selezionato · riga di stato (genere, registro,
esecuzione, target LUFS) · badge GENERATA / MODIFICATO · LED di modulo attivo · meter GR dei cinque
stadi dinamici · valori numerici dei knob · posizione dell'indicatore e corona di valore · pannello
PERCHÉ · note di produzione · schede SENDS · meter IN/OUT e LUFS · numero di versione e versione delle regole.

**Asset mobili** (PNG con alpha @2x, uno per elemento, in `plugin/source/assets/src/`):
knob grande 92 px · knob piccolo 68 px · switch on/off (due file) · LED tre stati (on, off, modificato) ·
cap del fader/meter · cornice del prompt box · bottone FORGE attivo e a riposo.
Il knob ha l'indicatore a ore 12 nell'asset; la **corona di valore è codice**, disegnata sopra.

## Mappa dei blocchi (coordinate normalizzate 0..1)

Pronte per `layout.json`. Origine in alto a sinistra, `x, y, w, h`.

| id | x | y | w | h | contenuto |
|---|---|---|---|---|---|
| `header` | 0.009 | 0.012 | 0.982 | 0.185 | piastra dell'intestazione |
| `title` | 0.024 | 0.022 | 0.222 | 0.044 | VOCAL FORGE + sottotitolo |
| `version` | 0.800 | 0.026 | 0.185 | 0.026 | versione plugin e regole |
| `promptBox` | 0.024 | 0.076 | 0.646 | 0.050 | campo di testo del prompt |
| `sendsMode` | 0.680 | 0.076 | 0.102 | 0.050 | menù internal / logic |
| `forgeButton` | 0.792 | 0.076 | 0.190 | 0.050 | bottone FORGE |
| `artistLabel` | 0.024 | 0.140 | 0.054 | 0.032 | etichetta ARTISTA |
| `artistBox` | 0.082 | 0.140 | 0.194 | 0.032 | menù dei riferimenti |
| `statusLine` | 0.288 | 0.140 | 0.560 | 0.032 | genere · registro · esecuzione · target |
| `stateBadge` | 0.870 | 0.140 | 0.112 | 0.032 | GENERATA / MODIFICATO |
| `rack` | 0.009 | 0.212 | 0.219 | 0.594 | rack dei 13 moduli |
| `rackRow[n]` | 0.017 | 0.223 + n·0.0456 | 0.202 | 0.041 | riga di modulo (n = 0…12) |
| `controls` | 0.237 | 0.212 | 0.437 | 0.594 | knob del modulo selezionato |
| `whyPanel` | 0.683 | 0.212 | 0.308 | 0.594 | pannello PERCHÉ |
| `sends` | 0.009 | 0.815 | 0.982 | 0.147 | tre schede delle mandate |
| `sendCard[n]` | 0.020 + n·0.325 | 0.845 | 0.305 | 0.105 | reverb / delay / fx |
| `meters` | 0.015 | 0.968 | 0.970 | 0.026 | IN, OUT, LUFS, ceiling |

La griglia dei controlli è **6 knob per riga** nel modulo selezionato: cella 86 × 92 px a 1080 px di
larghezza (0.080 × 0.135 normalizzati), etichetta sopra il valore, valore sopra il nome del parametro.

## Regole di composizione

- **Gruppi con `gap`, mai margini per elemento**: il rack, la griglia dei knob e le schede dei send
  sono flex/grid con spaziatura uniforme, così aggiungere o togliere un elemento non sposta il resto.
- **Il colore dice lo stato**: ambra = valore e comando, azzurro = misura (GR, meter, send),
  verde = attivo/in sicurezza, rosso = sta lavorando troppo. Nessun colore decorativo.
- **Ogni valore ha il suo perché a portata**: la colonna PERCHÉ è larga quanto un terzo della finestra,
  non è un tooltip. È la firma del prodotto.
- **Le mandate sono visivamente staccate**: banda bassa separata, accento freddo, bordo sinistro —
  devono leggersi come bus paralleli, non come l'ultimo anello della catena.

## Canvas

Il design completo (5 artboard: finestra generata, finestra vuota, zoom SENDS, zoom Comp 1, tavola
degli elementi) sta in `reference/design/`, file `.dc.html` più `canvas.json`.
