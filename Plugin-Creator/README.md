# Plugin-Creator

Scaffold per creare **plugin audio JUCE (AU/VST3)** funzionanti, funzionali e belli **al primo giro**,
distillato dalle lezioni del progetto NEMO (dove 6 revisioni grafiche sono state spese in debito di metodo evitabile).

## Come si usa

1. **Compila lo spec** → `PLUGIN_SPEC.md` (l'assistente te lo riempie facendoti domande).
2. *(Opzionale)* metti una **foto di riferimento** della UI in `reference/` e gli **asset PNG con alpha** in `template/source/assets/src/`.
3. L'assistente genera il plugin reale a partire dal template, seguendo `CLAUDE.md`.
4. Verifica con `CHECKLIST.md` (null-test, pluginval, auval, render headless).

## Struttura

```
Plugin-Creator/
├── README.md                 ← questo file
├── CLAUDE.md                 ← convenzioni vincolanti (DSP, UI, asset, build, test)
├── SEED_PROMPT.md            ← il prompt del "giorno 1" da dare all'assistente
├── PLUGIN_SPEC.md            ← scheda del plugin da sviluppare (si compila a domande)
├── CHECKLIST.md              ← validazione prima di dire "funziona"
├── reference/                ← qui la foto di riferimento della UI
├── tools/
│   ├── build_assets.py       ← pipeline asset deterministica (sorgente→PNG alpha→verifica)
│   ├── assets.manifest.example.json
│   └── shoot/main.cpp        ← render headless dell'editor (loop di feedback visivo)
└── template/                 ← scheletro copiato per ogni nuovo plugin
    ├── CMakeLists.template.txt
    ├── Layout.h              ← loader di layout in coordinate normalizzate (niente numeri magici)
    ├── layout.example.json
    └── source/assets/src/    ← qui i PNG-alpha sorgente degli elementi mobili
```

## Sottoprogetti

- [`../Vocal-Chain-Creator/`](../Vocal-Chain-Creator/) — **VOCAL FORGE**: plugin che compila una vocal chain
  da una richiesta testuale, tarata su stanza non trattata + microfono Focusrite + Scarlett a due ingressi,
  destinazione Logic Pro. Segue questo scaffold e ne estende `CLAUDE.md` con i vincoli di dominio.

## Le tre regole che evitano i cicli di NEMO

1. **Sfondo inerte**: niente testo/indicatori/scale/parti mobili "cotti" nello sfondo. Se può cambiare o muoversi → è codice o asset separato.
2. **Layout normalizzato**: posizioni in `layout.json` (0..1), mai pixel assoluti nel `.cpp`. Le posizioni si **misurano**, non si stimano.
3. **Verifica sul render vero**: loop `build → shoot (PNG reale dell'editor JUCE) → confronto`, non su mock.
