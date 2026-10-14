# reference/

Qui va la **foto di riferimento della UI** di VOCAL FORGE (come in `../../Plugin-Creator/reference/`).

Regole (da `Plugin-Creator/CLAUDE.md`):
- lo **sfondo è una piastra inerte**: nessun testo, valore, indicatore o parte mobile cotto dentro;
- le posizioni dei controlli si **misurano** da questa immagine (detection), non si stimano;
- il confronto finale si fa sul **render reale** dell'editor JUCE (`tools/shoot`), mai su un mock.

Elementi che in VOCAL FORGE **non possono** essere baked, perché cambiano:
prompt box e testo dell'utente, pannello "perché", meter GR dei cinque stadi dinamici,
curva EQ, valori numerici, LED di modulo attivo, badge "modificato", numero di versione.
