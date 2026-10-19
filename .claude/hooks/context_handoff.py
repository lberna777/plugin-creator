#!/usr/bin/env python3
"""Hook Stop: al 50% della finestra di contesto ordina a Claude il passaggio di consegne.

Copia identica in cockpit, Developing, plugin-creator, carguy, bibiciclo (.claude/hooks/).
L'originale è quello di cockpit: se lo cambi, ricopialo nelle altre repo.

Legge dalla trascrizione l'ultimo messaggio di Claude e somma i token in ingresso (input +
cache): è il contesto occupato. Oltre la soglia risponde con decision=block, così Claude non
chiude il turno ed esegue /handoff (procedura in .claude/commands/handoff.md). Scatta una sola
volta per sessione (file segnaposto nella cartella temporanea).

Variabili: HANDOFF_CONTESTO_MAX (default 1000000 token), HANDOFF_SOGLIA (default 0.5).
"""
import json
import os
import sys
import tempfile


def contesto_occupato(percorso):
    ultimo = 0
    try:
        with open(percorso, encoding="utf-8") as fh:
            for riga in fh:
                try:
                    voce = json.loads(riga)
                except ValueError:
                    continue
                if voce.get("type") != "assistant" or voce.get("isSidechain"):
                    continue
                uso = (voce.get("message") or {}).get("usage") or {}
                totale = (uso.get("input_tokens", 0) + uso.get("cache_read_input_tokens", 0)
                          + uso.get("cache_creation_input_tokens", 0))
                if totale:
                    ultimo = totale
    except OSError:
        return 0
    return ultimo


def main():
    try:
        dati = json.load(sys.stdin)
    except ValueError:
        return 0
    if dati.get("stop_hook_active"):
        return 0
    sessione = str(dati.get("session_id") or "ignota")
    segnaposto = os.path.join(tempfile.gettempdir(), f"claude-handoff-{sessione}")
    if os.path.exists(segnaposto):
        return 0

    massimo = int(os.environ.get("HANDOFF_CONTESTO_MAX", "1000000"))
    soglia = float(os.environ.get("HANDOFF_SOGLIA", "0.5"))
    usati = contesto_occupato(dati.get("transcript_path") or "")
    if usati < massimo * soglia:
        return 0

    open(segnaposto, "w").close()
    perc = round(100 * usati / massimo)
    motivo = (
        f"Contesto al {perc}% ({usati} token su {massimo}): soglia del passaggio di consegne "
        f"raggiunta. Prima di chiudere il turno esegui /handoff in modalità automatica: segui "
        f"alla lettera .claude/commands/handoff.md (scrivi e invia l'handoff, crea la sessione "
        f"che sostituisce questa e dai a Lorenzo il link). Non iniziare altro lavoro."
    )
    print(json.dumps({"decision": "block", "reason": motivo}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
