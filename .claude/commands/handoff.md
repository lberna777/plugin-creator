---
description: Passaggio di consegne — scrive l'handoff, crea la sessione che sostituisce questa e la collega. Parte da solo al 50% del contesto (hook), si può lanciare a mano.
---

<!-- Copia identica in cockpit, Developing, plugin-creator, carguy, bibiciclo, barca-halloween. L'originale è in cockpit. -->

Esegui il passaggio di consegne di questa sessione. Argomenti: `$ARGUMENTS` (vuoto o `auto` se
l'ha lanciato l'hook del contesto). Non iniziare altro lavoro.

1. **Chiudi il lavoro in corso.** Se stai a metà di una modifica, portala a uno stato coerente
   (o annullala) e fai commit e push secondo le regole del `CLAUDE.md` della repo. Annota il ramo
   corrente (`git branch --show-current`) e verifica che non restino commit non inviati.

2. **Scrivi l'handoff.** Cartella: quella indicata dal `CLAUDE.md` della repo (studio in cockpit:
   `Vault/UniCode/plans/handoffs/`, con le tre sezioni obbligatorie di `Vault/UniCode/CLAUDE.md` §6;
   carguy: `plans/handoffs/`); se non ne indica una, `.claude/handoffs/`. Nome:
   `HANDOFF_<argomento-breve>_<AAAA-MM-GG>.md`. Contenuto, in italiano, autosufficiente:
   - **Intestazione**: data, gruppo e titolo della chat, id della sessione (`get_session` senza
     id, se lo strumento c'è), repo e ramo;
   - **Obiettivo** della sessione, in due righe;
   - **Fatto**: risultati con file e commit, non il racconto;
   - **Decisioni prese** da Lorenzo, con il motivo;
   - **Ancora aperto / poco chiaro**;
   - **Prossimo passo esatto**: un'azione precisa e verificabile, non «continuare con X»;
   - **Da leggere per primi**: i file che la sessione nuova deve aprire, in ordine.
   Fai commit e push dell'handoff sullo stesso ramo.

3. **Crea la sessione che sostituisce questa** (nel cloud, se c'è lo strumento `create_session`):
   - leggi questa sessione con `get_session` senza id: titolo, etichette non `config:`, repo;
   - `create_session` con: `source_url` della repo padre, `source_revision` = il ramo del punto 1,
     `title` = stesso titolo (stesso prefisso di gruppo, es. «Studio · cockpit — …») con « (segue)»
     in fondo, `tags` = le stesse etichette, e come `prompt`:
     > Questa chat sostituisce la sessione `<id vecchia>`, chiusa al 50% del contesto. Leggi
     > `<percorso handoff>` e i file che indica, poi riassumimi in cinque righe dove eravamo e il
     > prossimo passo, e aspetta il mio via. Come ultima azione di questo primo turno archivia la
     > sessione `<id vecchia>` con `archive_session` e, se c'è `ArtifactData`, metti
     > `archiviata_vecchia: true` sul documento `handoff/…` della Plancia che nomina questa sessione.
   - Se la repo dà un ruolo a ogni chat (Developing: `/principale`, `/noone`, `/portfolio`…), il
     `prompt` comincia con il comando del ruolo di questa chat, seguito dal testo sopra.
   - L'archiviazione la fa la sessione nuova, non questa: così la vecchia chiude il turno prima.

4. **Ultimo messaggio a Lorenzo**, breve: percorso dell'handoff, link alla sessione nuova
   (`https://claude.ai/code/<id nuova>`), e che questa verrà archiviata da lì.
   Se `create_session` non c'è (sessione locale): niente sessione nuova né archiviazione; dagli il
   messaggio d'avvio da incollare in una chat nuova dello stesso gruppo.

Se la repo ha una Plancia o un registro di giornata da aggiornare (studio in cockpit: `Vault/UniCode/stato/giornata.md`),
l'handoff è un evento da registrare lì.

**Plancia** (se c'è `ArtifactData`; prima del messaggio del punto 4): un documento
`handoff/<AAAA-MM-GG>-<id nuova senza "session_">` con `data` (`AAAA-MM-GG`), `vecchia` e `nuova`
(id delle sessioni), `titolo` (della nuova), `gruppo`, `repo`, `ramo`, `file` (percorso dell'handoff),
`archiviata_vecchia: false` (lo mette a `true` la sessione nuova quando archivia la vecchia). La
sezione Claude della Plancia mostra da lì la catena dei passaggi. Schema completo in `meta/protocollo`.
