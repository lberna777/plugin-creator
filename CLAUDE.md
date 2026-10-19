# CLAUDE.md — plugin-creator (radice)

## Famiglia

**Padre** dei plugin audio JUCE di Lorenzo (gruppo di chat **Sviluppo plug-in** su claude.ai,
area lavoro): qui si opera.
- **Dentro questa repo**: `Plugin-Creator/` (scaffold, convenzioni e i plugin nuovi: `C60`,
  `RSU10`, `MikroBridge`) e `Vocal-Chain-Creator/` (VOCAL FORGE).
- **Figlie** (repo storiche, si consultano e non ci si lavora): `NEMO-StereoCompressor` (le regole
  di `Plugin-Creator/CLAUDE.md` nascono dalla sua autopsia), `StereoCompressor`, `FreakFM`,
  `acidmoog-synth`. Un plugin nuovo nasce qui dentro, non in una repo a parte.
- Mappa completa di tutte le repo: `CLAUDE.md` di `lberna777/cockpit`, sezione «Famiglia delle
  repository».

## Quale CLAUDE.md vale

- Lavoro su un plugin: `Plugin-Creator/CLAUDE.md` (convenzioni vincolanti su DSP, UI, asset,
  build e test).
- Lavoro su VOCAL FORGE: in più `Vocal-Chain-Creator/CLAUDE.md`, che aggiunge le regole del
  dominio voce; in conflitto vince la più restrittiva.

Le build AU e le prove in Logic e FL Studio si fanno sul **Mac mini**: quando un lavoro deve
passare lì, dirlo a Lorenzo.
