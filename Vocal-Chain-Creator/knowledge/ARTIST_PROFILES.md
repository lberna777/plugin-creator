# Profili artista — mock-up di un suono, non la catena di qualcun altro

## Cosa sono, e cosa non sono

Scegli un riferimento dal menù **ARTISTA** (o lo scrivi nel prompt) e il compilatore costruisce una
catena tarata per **avvicinare il risultato che si sente sui dischi**, partendo dal *tuo* segnale:
stanza non trattata, microfono Focusrite, Scarlett a due ingressi.

**Non sono le catene reali di questi artisti.** Nessuno le conosce fuori dai loro studi, e comunque
non servirebbero: quelle partono da una cabina trattata, un microfono da qualche migliaio di euro e un
preamp all'altezza. Copiare quei settaggi sul tuo segnale darebbe un risultato peggiore, non migliore.
Quello che si può ricostruire è il **risultato**: quanto è brillante, quanto è densa, quanta stanza si
sente, dove stanno le esse, quanto è larga, che tipo di ambiente ha. Quello si ottiene — partendo da
dove sei tu.

Il riferimento riempie il prompt come **testo**: dopo lo correggi a parole
(`sfera ebbasta, ma meno brillante`), perché sotto c'è lo stesso motore di sempre.

## I profili

| Profilo | Come si scrive | Registro / esecuzione | Target | Riverbero | Delay | Doubler |
|---|---|---|---|---|---|---|
| **Sfera Ebbasta** | sfera ebbasta, sfera, ebbasta | mid / sung | -9 LUFS | plate | eighth | sì |
| **Shiva** | shiva | mid / rapped | -9 LUFS | ambience | eighth | sì |
| **Tony Boy** | tony boy, tonyboy, tony | mid / sung | -10 LUFS | hall | eighth | sì |
| **Glockyy** | glockyy, glocky, glock | low / rapped | -9 LUFS | ambience | slap | sì |
| **Gue** | gue pequeno, gue, guè | low / rapped | -10 LUFS | room | slap | no |
| **Capo Plaza** | capo plaza, capoplaza, capo | mid / sung | -9 LUFS | plate | eighth | sì |

## Cosa decide ogni profilo

Un profilo artista è il preset di partenza **più forte** del vocabolario: in un colpo solo imposta

1. **gli assi** (brillantezza, corpo, aggressività, densità, carattere, intimità, pulizia, vintage, sibilanti),
2. **esecuzione e registro** (rappata/cantata, voce bassa/media/acuta),
3. **il target di loudness**,
4. **le mandate**, che vengono *imposte* invece di essere scelte per condizione — un profilo che vive
   sulla plate deve avere la plate, non "quello che capita";
5. le **note di produzione**: quello che il plugin **non** fa.

Gli aggettivi che scrivi dopo correggono il profilo, non lo sostituiscono.

## Le note di produzione: la metà che non sta nel plugin

Su questi suoni una parte del riconoscimento **non è la catena**: è l'intonazione, sono i doppiaggi,
sono gli ad-lib. Il plugin non fa nessuna delle tre, e fingere il contrario sarebbe la bugia più comoda
e più dannosa che potrei raccontarti. Quindi ogni profilo si porta dietro istruzioni esplicite, che
trovi nel pannello **PERCHÉ** e nella ricetta Logic:

- **Intonazione** — quanto e come usare Pitch Correction / Auto-Tune. Su alcuni profili l'autotune
  *è* il timbro (retune 0–5 ms); su altri è assente, e metterlo rovina il riferimento.
- **Doppiaggi** — quante voci, che panning, quanti dB sotto la main. Il **doubler** interno allarga,
  ma non sostituisce una doppia registrata davvero.
- **Ad-lib** — trattamento separato: di solito più delay, meno riverbero, spesso hard-panned.
- **Extra** — throw delay automatizzati, filtri di sezione, avvertenze su drive e sibilanti.

## Il doubler: il terzo bus

Oltre a riverbero e delay, i profili trap accendono un terzo bus parallelo: due copie ritardate di
19–32 ms, stonate di pochi cent da un LFO lento, filtrate e allargate ai lati. Serve a far sembrare la
voce "doppia" senza toccare il centro, dove restano la main e la 808. Anche lui è **duckato** dalla voce
e non supera mai il livello della main.

## Come si aggiunge un artista

Si tocca **solo** `tools/data/rules.json` → `lexicon.artists`:

```json
"nome_artista": {
  "aliases": ["nome artista", "soprannome"],
  "genre": "trap", "delivery": "sung", "pitch_class": "mid", "loudness_target_lufs": -9,
  "axes": { "brightness": 0.3, "density": 0.65, "character": 0.35 },
  "sends": { "reverb": "rev_plate", "delay": "dly_eighth", "fx": "fx_doubler" },
  "sound": "una riga che descrive il risultato",
  "production": { "tuning": "…", "doubles": "…", "adlib": "…", "extra": "…" }
}
```

Poi un caso in `tools/tests/test_artists.py` e `python3 tools/chain_compiler.py --examples`.
**Nessuna riga di C++ da scrivere**: il plugin legge lo stesso file, e il test di parità lo verifica.

I test impongono già che ogni profilo consegni una **catena completa** (EQ sottrattiva + due compressori +
saturazione + EQ tonale + due de-esser + limiter), **riverbero e delay dichiarati**, note di produzione su
intonazione/doppiaggi/ad-lib, e che **due profili non producano la stessa catena** — se succede, uno dei
due non serve.
