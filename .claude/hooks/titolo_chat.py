#!/usr/bin/env python3
"""Hook UserPromptSubmit: allinea titolo ed etichetta della chat al suo gruppo.

Copia identica in cockpit, Developing, plugin-creator, carguy, bibiciclo, barca-halloween
(.claude/hooks/).
L'originale è quello di cockpit: se lo cambi, ricopialo nelle altre repo. Gruppi e nomi:
cockpit, Vault/claude/come_lavorare.md.

Al primo messaggio di ogni sessione cloud ricava il gruppo dalla repo e dal comando con cui si
apre la chat, e chiede a Claude (che ha gli strumenti per farlo, l'hook no) di impostare
titolo «<Gruppo> · <repo> — <argomento>» ed etichetta. Una volta per sessione.
"""
import json
import os
import subprocess
import sys
import tempfile

STUDIO = ("/sessione", "/lab", "/lezione", "/appunti", "/ripassa", "/simula", "/flusso", "/stato",
          "/lacune", "/piano", "/pianifica", "/chiudi", "/verifica", "/pdf-batch")
# Progetti per divertimento (gruppo Fun): una repo autonoma per progetto.
FUN = ("barca-halloween",)


def repo_corrente():
    cartella = os.environ.get("CLAUDE_PROJECT_DIR") or os.getcwd()
    try:
        url = subprocess.run(["git", "-C", cartella, "remote", "get-url", "origin"],
                             capture_output=True, text=True, timeout=5).stdout.strip()
        nome = url.rstrip("/").split("/")[-1]
        nome = nome[:-4] if nome.endswith(".git") else nome
    except (OSError, subprocess.SubprocessError):
        nome = ""
    return (nome or os.path.basename(cartella)).lower()


def gruppo(repo, testo):
    primo = testo.strip().split()[0].lower() if testo.strip() else ""
    if repo == "cockpit":
        if primo in STUDIO:
            return "Studio · cockpit", "studio"
        return "Sistema · cockpit", "sistema"
    if repo == "developing":
        if primo == "/portfolio":
            return "Portfolio · Developing", "portfolio"
        return "Progetti · Developing", "progetti"
    if repo == "plugin-creator":
        return "Plugin · plugin-creator", "plugin"
    if repo == "carguy":
        return "CarGuy · carguy", "carguy"
    if repo == "bibiciclo":
        return "BibiCiclo · bibiciclo", "bibiciclo"
    if repo in FUN:
        return f"Fun · {repo}", "fun"
    return None, None


def main():
    if os.environ.get("CLAUDE_CODE_REMOTE") != "true":
        return 0
    try:
        dati = json.load(sys.stdin)
    except ValueError:
        return 0
    sessione = str(dati.get("session_id") or "ignota")
    segnaposto = os.path.join(tempfile.gettempdir(), f"claude-titolo-{sessione}")
    if os.path.exists(segnaposto):
        return 0
    nome, etichetta = gruppo(repo_corrente(), str(dati.get("prompt") or ""))
    if not nome:
        return 0
    open(segnaposto, "w").close()
    istruzione = (
        f"Classificazione della chat (hook): gruppo «{nome}», etichetta `{etichetta}`. In questo "
        f"turno, senza commentarlo, leggi la sessione con get_session (senza id) e, se il titolo "
        f"non comincia già con «{nome} —», impostalo con set_session_title a "
        f"«{nome} — <argomento in 3-6 parole, in italiano>»; aggiungi l'etichetta `{etichetta}` "
        f"con set_session_tags se manca. Se il titolo ha già il prefisso giusto (per esempio una "
        f"chat «(segue)»), non toccarlo. Se gli strumenti non ci sono, lascia perdere."
    )
    print(json.dumps({"hookSpecificOutput": {"hookEventName": "UserPromptSubmit",
                                             "additionalContext": istruzione}}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
