# VOCAL FORGE — sottoprogetto di Plugin-Creator

Plugin AU/VST3 che **compila una vocal chain a partire da una richiesta testuale**, tarato su una
sorgente nota — **stanza non trattata, microfono Focusrite, Scarlett a due ingressi** — e destinato a
girare **dentro Logic Pro**.

Idea completa e fasi: [`IDEA.md`](IDEA.md). Prompt del giorno 1: [`SEED_PROMPT.md`](SEED_PROMPT.md).

## Usarlo su Logic, subito

```bash
cd plugin && ./build_mac.sh --fast     # compila e installa AU + VST3 + Standalone, poi valida con auval
```
Guida passo passo: **[`plugin/INSTALL_MAC.md`](plugin/INSTALL_MAC.md)**.
Senza compilare niente: le ricette con i soli plugin stock di Logic sono già in [`examples/`](examples/).

## Come si avvia

1. Leggi `../Plugin-Creator/CLAUDE.md` (vincolante) e poi `CLAUDE.md` di qui (aggiunte di dominio).
2. Lo spec è già compilato e **congelato**: `PLUGIN_SPEC.md` (identità, parametri, catena).
3. Prova subito il compilatore, senza toccare C++:
   ```bash
   python3 tools/chain_compiler.py "voce trap aggressiva ma non stridula, stanza non trattata"
   python3 tools/chain_compiler.py --examples          # rigenera examples/
   python3 -m unittest discover -s tools/tests -v      # test delle regole
   ```
4. Costruisci il plugin seguendo il loop obbligatorio: **DSP → validazione → UI da `layout.json` → render headless**.
5. Valida con `CHECKLIST.md` (che estende quella del padre con i test specifici della voce).

## Struttura

```
Vocal-Chain-Creator/
├── IDEA.md                     ← l'idea, il perché, le fasi, i rischi
├── SEED_PROMPT.md              ← il prompt del "giorno 1"
├── CLAUDE.md                   ← convenzioni vincolanti aggiuntive (dominio voce)
├── PLUGIN_SPEC.md              ← scheda del plugin, identità congelata
├── CHECKLIST.md                ← validazione (DSP + voce + testo→chain + Logic)
├── knowledge/
│   ├── ROOM_MIC_PROFILE.md     ← stanza non trattata, mic Focusrite, Scarlett: cosa sappiamo già
│   ├── CHAIN_ARCHITECTURE.md   ← i 13 moduli, ordine e motivo di ognuno
│   ├── ARTIST_PROFILES.md      ← i riferimenti (trapper italiani): cosa sono e cosa non sono
│   ├── PROMPT_GRAMMAR.md       ← vocabolario IT/EN → assi di intento
│   └── LOGIC_INTEGRATION.md    ← come si entra in Logic (AU, ricetta stock, .cst)
├── schema/
│   ├── intent.schema.json      ← contratto del testo interpretato
│   └── chain_preset.schema.json← contratto del preset di catena
├── tools/
│   ├── chain_compiler.py       ← testo → intent → preset → ricetta Logic
│   ├── data/rules.json         ← TUTTE le regole, fuori dal codice
│   └── tests/                  ← test deterministici sulle regole
├── plugin/                     ← il plugin vero (JUCE): AU + VST3 + Standalone
│   ├── INSTALL_MAC.md          ← installazione veloce su Mac/Logic
│   ├── build_mac.sh            ← build + installazione + auval in un comando
│   ├── CMakeLists.txt
│   ├── source/                 ← RulesEngine (parità col Python), ChainDsp, Processor, Editor
│   └── tools/                  ← vocalforge_parity, vocalforge_selftest (headless)
├── examples/                   ← preset + ricette generati da prompt d'esempio
└── reference/                  ← foto di riferimento della UI
```

## Due modi di chiedere

- **Un riferimento**: menù ARTISTA (Sfera Ebbasta, Shiva, Tony Boy, Glockyy, Guè, Capo Plaza) — catena
  completa più riverbero, delay, doubler e note di produzione.
  Vedi [`knowledge/ARTIST_PROFILES.md`](knowledge/ARTIST_PROFILES.md).
- **A parole**: `voce trap aggressiva ma non stridula`. I due si combinano: `capo plaza, ma meno brillante`.

## Le regole che questo sottoprogetto aggiunge

1. **Niente valori magici nel DSP**: ogni dB e ogni Hz esce da `tools/data/rules.json`, con motivazione.
2. **Il testo non sceglie preset, li genera**: parsing deterministico → `intent.json` → regole → `chain_preset.json`.
3. **Il profilo di sorgente è un input, non un'ipotesi**: stanza/mic/interfaccia stanno in `knowledge/ROOM_MIC_PROFILE.md`
   e nel preset come `source_profile`, e cambiano il risultato.
