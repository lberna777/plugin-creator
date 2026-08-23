# assets/src/ — sorgenti grezzi degli asset

Metti qui i **PNG con canale alpha**, @2x, sfondo trasparente — un file per elemento mobile:

- `knob_src.png`        → knob con indicatore a ore 12
- `fader_cap_src.png`   → cap del fader
- `switch_on_src.png` / `switch_off_src.png` → stati dello switch
- `background_src.png`  → piastra di sfondo (può essere opaca/JPEG: è inerte)

Regole (vedi CLAUDE.md):
- **NO** trasparenza appiattita (niente JPEG per gli elementi mobili: WhatsApp/JPEG distruggono l'alpha).
- **NO** testo/indicatori/scale/parti mobili cotti nello sfondo: quelli si disegnano in codice.

La pipeline `tools/build_assets.py` legge questi sorgenti, applica le operazioni del manifest
(scontorno/crop/rotazione/quadratura) e produce gli asset FINALI in `assets/` (embeddati in BinaryData).
Gli assert su alpha e dimensioni bloccano i regressi.
