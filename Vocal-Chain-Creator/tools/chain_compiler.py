#!/usr/bin/env python3
"""VOCAL FORGE — compilatore di vocal chain.

    testo utente  ->  INTENT (assi)  ->  REGOLE (data/rules.json)  ->  CHAIN PRESET  ->  ricetta Logic

Specifica ESEGUIBILE del motore C++ del plugin: il C++ deve riprodurre lo stesso output
sui preset di examples/ (vedi CHECKLIST.md, voce "parità Python <-> C++").

Vincolo di CLAUDE.md: qui NON ci sono numeri di dominio. Ogni dB, Hz, ms, ratio sta in data/rules.json.

Uso:
    python3 chain_compiler.py "voce trap aggressiva ma non stridula"
    python3 chain_compiler.py "podcast, voce parlata pulita" --profile untreated_room_focusrite_scarlett --format recipe
    python3 chain_compiler.py --examples          # (ri)genera examples/
    python3 chain_compiler.py --examples --check  # verifica che examples/ sia allineato (CI)
"""
from __future__ import annotations

import argparse
import ast
import json
import operator
import os
import re
import sys
import unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
RULES_PATH = os.path.join(HERE, "data", "rules.json")
EXAMPLES_DIR = os.path.join(ROOT, "examples")
SCHEMA_VERSION = "1.0.0"

EXAMPLE_PROMPTS = [
    "voce trap aggressiva ma non stridula",
    "pop moderno, voce brillante e presente, da streaming",
    "r&b intimo, voce calda e morbida",
    "rap italiano, flow serrato, voce in faccia",
    "podcast, voce parlata pulita, senza rumore di fondo",
    "voiceover per audiolibro, voce bassa e corposa",
    "indie lo-fi, voce vintage a nastro, poco brillante",
    "metal, voce urlata molto aggressiva",
    "cantautore acustico, voce naturale e dinamica",
    "voce femminile acuta, cristallina, con molte esse da controllare",
    "voce sussurrata intima per un pezzo lento",
    "edm, voce compressa e luminosa per il club",
]

# --------------------------------------------------------------------------------------
# Valutatore di espressioni (sandbox su AST): serve a tenere i numeri nel JSON, non nel codice.
# --------------------------------------------------------------------------------------

_BIN_OPS = {
    ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul,
    ast.Div: operator.truediv, ast.Pow: operator.pow, ast.Mod: operator.mod,
}
_CMP_OPS = {
    ast.Gt: operator.gt, ast.GtE: operator.ge, ast.Lt: operator.lt,
    ast.LtE: operator.le, ast.Eq: operator.eq, ast.NotEq: operator.ne,
}


def _clamp(x, lo, hi):
    return lo if x < lo else (hi if x > hi else x)


_FUNCS = {
    "clamp": _clamp,
    "min": min,
    "max": max,
    "abs": abs,
    "round": round,
    "pos": lambda x: max(0.0, x),          # solo la parte positiva dell'asse
    "neg": lambda x: max(0.0, -x),         # solo la parte negativa dell'asse
    "lerp": lambda a, b, t: a + (b - a) * _clamp(t, 0.0, 1.0),
}


class ExprError(ValueError):
    pass


def evaluate(expr, ctx):
    """Valuta un'espressione aritmetica/booleana su un contesto piatto di variabili."""
    try:
        node = ast.parse(str(expr), mode="eval").body
    except SyntaxError as exc:                                   # pragma: no cover
        raise ExprError(f"espressione non valida: {expr!r} ({exc})") from exc
    return _eval(node, ctx, expr)


def _eval(node, ctx, src):
    if isinstance(node, ast.Constant):
        return node.value
    if isinstance(node, ast.Name):
        if node.id in ctx:
            return ctx[node.id]
        if node.id in ("True", "true"):
            return True
        if node.id in ("False", "false"):
            return False
        raise ExprError(f"variabile sconosciuta {node.id!r} in {src!r}")
    if isinstance(node, ast.BinOp) and type(node.op) in _BIN_OPS:
        return _BIN_OPS[type(node.op)](_eval(node.left, ctx, src), _eval(node.right, ctx, src))
    if isinstance(node, ast.UnaryOp):
        if isinstance(node.op, ast.USub):
            return -_eval(node.operand, ctx, src)
        if isinstance(node.op, ast.UAdd):
            return +_eval(node.operand, ctx, src)
        if isinstance(node.op, ast.Not):
            return not _eval(node.operand, ctx, src)
    if isinstance(node, ast.BoolOp):
        vals = [_eval(v, ctx, src) for v in node.values]
        return all(vals) if isinstance(node.op, ast.And) else any(vals)
    if isinstance(node, ast.Compare) and len(node.ops) == 1 and type(node.ops[0]) in _CMP_OPS:
        return _CMP_OPS[type(node.ops[0])](_eval(node.left, ctx, src), _eval(node.comparators[0], ctx, src))
    if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id in _FUNCS:
        return _FUNCS[node.func.id](*[_eval(a, ctx, src) for a in node.args])
    raise ExprError(f"costrutto non permesso in {src!r}")


# --------------------------------------------------------------------------------------
# Parsing del testo -> INTENT
# --------------------------------------------------------------------------------------

def load_rules(path=RULES_PATH):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def normalize(text):
    """minuscolo, senza accenti, punteggiatura -> spazi. Deterministico."""
    text = unicodedata.normalize("NFKD", text.lower())
    text = "".join(c for c in text if not unicodedata.combining(c))
    text = text.replace("&", " & ")
    return re.sub(r"[^a-z0-9&/' ]+", " ", text)


def _alias_table(section):
    """alias (normalizzato) -> (chiave, definizione). Alias piu' lunghi hanno precedenza."""
    table = {}
    for key, definition in section.items():
        for alias in definition.get("aliases", [key]):
            table[normalize(alias).strip()] = (key, definition)
    return table


def _find_aliases(tokens, table):
    """Trova gli alias (anche multi-parola) nel testo tokenizzato. Ritorna (chiave, def, indice)."""
    hits, i = [], 0
    max_len = max((len(a.split()) for a in table), default=1)
    while i < len(tokens):
        for span in range(min(max_len, len(tokens) - i), 0, -1):
            phrase = " ".join(tokens[i:i + span])
            if phrase in table:
                key, definition = table[phrase]
                hits.append((key, definition, i, phrase))
                i += span
                break
        else:
            i += 1
    return hits


def _modifier_scale(tokens, index, mods):
    """Guarda le N parole prima del termine: negazione / intensificatore / mitigatore."""
    window = tokens[max(0, index - mods["window"]):index]
    scale, negated = 1.0, False
    for word in window:
        if word in mods["negation"]:
            negated = True
        elif word in mods["intensifier"]["terms"]:
            scale *= mods["intensifier"]["scale"]
        elif word in mods["diminisher"]["terms"]:
            scale *= mods["diminisher"]["scale"]
    return (-scale if negated else scale), negated


def parse_intent(prompt, rules):
    lex = rules["lexicon"]
    mods = rules["modifiers"]
    defaults = rules["intent_defaults"]

    axes = dict(defaults["axes"])
    pitch_class = defaults["pitch_class"]
    delivery = defaults["delivery"]
    loudness = float(defaults["loudness_target_lufs"])

    tokens = [t for t in normalize(prompt).split() if t]
    matched, consumed = [], set()

    def record(kind, phrase, index, span_axes, scale, negated):
        applied = {}
        for axis, delta in (span_axes or {}).items():
            if axis not in axes:
                continue
            applied[axis] = round(delta * scale, 6)
            axes[axis] += delta * scale
        entry = {"term": phrase, "kind": kind}
        if negated:
            entry["negated"] = True
        if abs(scale) != 1.0:
            entry["scale"] = round(abs(scale), 3)
        if applied:
            entry["applied"] = applied
        matched.append(entry)
        for k in range(index, index + len(phrase.split())):
            consumed.add(k)

    # 1) genere: sposta gli assi in blocco
    genre = None
    for key, definition, idx, phrase in _find_aliases(tokens, _alias_table(lex["genre"])):
        first = genre is None
        if first:
            genre = key
            loudness = float(definition.get("loudness_target_lufs", loudness))
            if "delivery" in definition:
                delivery = definition["delivery"]
        # Solo il primo genere sposta gli assi: due alias dello stesso genere
        # ("cantautore acustico") sono una descrizione sola, non due richieste.
        record("genre", phrase, idx, definition.get("axes") if first else None, 1.0, False)

    # 2) esecuzione, registro, destinazione d'uso
    for key, definition, idx, phrase in _find_aliases(tokens, _alias_table(lex["delivery"])):
        delivery = key
        record("delivery", phrase, idx, definition.get("axes"), 1.0, False)
    for key, definition, idx, phrase in _find_aliases(tokens, _alias_table(lex["pitch"])):
        pitch_class = key
        record("pitch", phrase, idx, definition.get("axes"), 1.0, False)
    for key, definition, idx, phrase in _find_aliases(tokens, _alias_table(lex["usage"])):
        loudness = float(definition.get("loudness_target_lufs", loudness))
        record("usage", phrase, idx, definition.get("axes"), 1.0, False)

    # 3) aggettivi, con negazioni e intensificatori
    for key, definition, idx, phrase in _find_aliases(tokens, _alias_table(lex["descriptors"])):
        scale, negated = _modifier_scale(tokens, idx, mods)
        record("descriptor", phrase, idx, definition.get("axes"), scale, negated)

    # 4) clamp finale
    for axis, (lo, hi) in rules["axis_ranges"].items():
        axes[axis] = round(_clamp(axes[axis], lo, hi), 4)

    stop = set(mods["negation"]) | set(mods["intensifier"]["terms"]) | set(mods["diminisher"]["terms"])
    stop |= {"voce", "vocale", "una", "un", "e", "ed", "con", "per", "di", "da", "il", "la", "lo",
             "the", "a", "and", "with", "for", "in", "ma", "but", "molto", "che", "come", "su", "tipo", "stile"}
    unknown = [t for i, t in enumerate(tokens) if i not in consumed and t not in stop and len(t) > 2]

    has_it = any(t in {"voce", "vocale", "molto", "poco", "per", "con"} for t in tokens)
    has_en = any(t in {"voice", "vocal", "very", "with", "for"} for t in tokens)
    language = "mixed" if has_it and has_en else ("it" if has_it else ("en" if has_en else "unknown"))

    return {
        "schema_version": SCHEMA_VERSION,
        "prompt": prompt,
        "language_hint": language,
        "genre": genre,
        "axes": axes,
        "pitch_class": pitch_class,
        "delivery": delivery,
        "loudness_target_lufs": loudness,
        "matched_terms": matched,
        "unknown_terms": unknown,
    }


# --------------------------------------------------------------------------------------
# INTENT + profilo -> contesto -> CHAIN PRESET
# --------------------------------------------------------------------------------------

RHYTHMIC_GENRES = ("trap", "rap", "edm", "metal")


def build_context(intent, profile, rules):
    room, mic, iface = profile["room"], profile["mic"], profile["interface"]
    ctx = dict(intent["axes"])
    ctx.update({
        "pitch_factor": rules["pitch_factors"][intent["pitch_class"]],
        "loudness_target": intent["loudness_target_lufs"],
        "treated": 1 if room["treated"] else 0,
        "noise_floor": room["noise_floor_dbfs"],
        "mode1": room["modes_hz"][0], "mode2": room["modes_hz"][1], "mode3": room["modes_hz"][2],
        "boxy_lo": room["boxy_band_hz"][0], "boxy_hi": room["boxy_band_hz"][1],
        "flutter_lo": room["flutter_band_hz"][0], "flutter_hi": room["flutter_band_hz"][1],
        "proximity_boost": mic["proximity_boost_db"],
        "sib_center": mic["sibilance_center_hz"],
        "presence_peak": mic.get("presence_peak", 0),
        "target_peak": iface["target_peak_dbfs"],
        "assumed_peak": iface["assumed_peak_dbfs"],
        "genre_is_rhythmic": 1 if (intent["genre"] in RHYTHMIC_GENRES) else 0,
    })
    for name in ("spoken", "rapped", "sung", "screamed", "whispered"):
        ctx[f"delivery_is_{name}"] = 1 if intent["delivery"] == name else 0
    # variabili solo testuali, per i template delle motivazioni
    ctx_text = {"pitch_class": intent["pitch_class"], "delivery": intent["delivery"], "genre": intent["genre"]}
    return ctx, ctx_text


def _fmt_why(template, ctx, ctx_text, value=None):
    values = {k: (round(v, 2) if isinstance(v, float) else v) for k, v in ctx.items()}
    values.update(ctx_text)
    if value is not None:
        values["value"] = value
    try:
        return template.format(**values)
    except (KeyError, IndexError):                                # pragma: no cover
        return template


def _resolve_param(spec, ctx, ctx_text):
    if "value" in spec:
        value = spec["value"]
    elif "choices" in spec:
        value = None
        for choice in spec["choices"]:
            if "when" not in choice or evaluate(choice["when"], ctx):
                value = choice["value"]
                break
    else:
        value = evaluate(spec["expr"], ctx)
        if isinstance(value, bool):
            pass
        elif "round" in spec:
            value = round(float(value), spec["round"])
            if spec["round"] == 0:
                value = int(value)
        else:
            value = round(float(value), 4)
    out = {"value": value, "why": _fmt_why(spec["why"], ctx, ctx_text, value)}
    if "unit" in spec:
        out["unit"] = spec["unit"]
    return out


def compile_preset(prompt, profile_id="untreated_room_focusrite_scarlett", rules=None):
    rules = rules or load_rules()
    if profile_id not in rules["source_profiles"]:
        raise ValueError(f"profilo sconosciuto: {profile_id}")
    profile = rules["source_profiles"][profile_id]
    intent = parse_intent(prompt, rules)
    ctx, ctx_text = build_context(intent, profile, rules)

    modules = []
    for spec in rules["chain"]:
        enabled = bool(evaluate(spec["enabled"], ctx))
        why_key = "why_on" if enabled else "why_off"
        why = _fmt_why(spec.get(why_key, spec.get("why_on", "")), ctx, ctx_text)
        params = {}
        if enabled:
            for pid, pspec in spec["params"].items():
                params[pid] = _resolve_param(pspec, ctx, ctx_text)
        modules.append({"id": spec["id"], "label": spec["label"], "enabled": enabled,
                        "why": why, "params": params})

    sends, chosen_groups = [], set()
    for spec in sorted(rules["sends"], key=lambda s: (s["group"], s["priority"])):
        if spec["group"] in chosen_groups or not evaluate(spec["enabled"], ctx):
            continue
        chosen_groups.add(spec["group"])
        if spec.get("skip"):          # il gruppo si chiude senza mandata: è una scelta, non un buco
            continue
        settings = {pid: _resolve_param(dict(p, why=p.get("why", spec["why"])), ctx, ctx_text)["value"]
                    for pid, p in spec["settings"].items()}
        sends.append({"id": spec["id"], "group": spec["group"], "label": spec["label"],
                      "plugin_logic": spec["plugin_logic"], "settings": settings,
                      "why": _fmt_why(spec["why"], ctx, ctx_text)})

    warnings = []
    if not intent["matched_terms"]:
        warnings.append("Nessun termine riconosciuto: applicato il profilo neutro dichiarato in rules.json.")
    if intent["unknown_terms"]:
        warnings.append("Termini non nel vocabolario (ignorati): " + ", ".join(intent["unknown_terms"]))
    if not profile["room"]["treated"]:
        warnings.append("Stanza non trattata: le riflessioni precoci non sono correggibili a valle. "
                        "Avvicinati al microfono e metti qualcosa di morbido dietro di te.")

    return {
        "schema_version": SCHEMA_VERSION,
        "rules_version": rules["version"],
        "generated_by": "chain_compiler.py",
        "prompt": prompt,
        "intent": intent,
        "source_profile": profile,
        "io": {"stereo_is_dual_mono": profile["io"]["stereo_is_dual_mono"],
               "declared_latency_ms": rules["io_defaults"]["declared_latency_ms"]},
        "modules": modules,
        "output": {"loudness_target_lufs": intent["loudness_target_lufs"],
                   "true_peak_ceiling_dbfs": rules["io_defaults"]["true_peak_ceiling_dbfs"]},
        "sends": sends,
        "warnings": warnings,
    }


# --------------------------------------------------------------------------------------
# Uscite leggibili
# --------------------------------------------------------------------------------------

def render_text(preset):
    axes = preset["intent"]["axes"]
    lines = [f'PROMPT: "{preset["prompt"]}"',
             f'Genere: {preset["intent"]["genre"] or "—"} · registro: {preset["intent"]["pitch_class"]}'
             f' · esecuzione: {preset["intent"]["delivery"]} · target: {preset["output"]["loudness_target_lufs"]} LUFS',
             f'Profilo sorgente: {preset["source_profile"]["id"]}',
             "Assi: " + "  ".join(f"{k}={v:+.2f}" for k, v in axes.items()), ""]
    for mod in preset["modules"]:
        if not mod["enabled"]:
            lines += [f'[ OFF ] {mod["label"]} — {mod["why"]}', ""]
            continue
        lines.append(f'[ ON  ] {mod["label"]}')
        lines.append(f'         {mod["why"]}')
        for pid, p in mod["params"].items():
            unit = p.get("unit", "")
            lines.append(f'         {pid:<11} {str(_fmt_value(p["value"])):>9} {unit:<7} — {p["why"]}')
        lines.append("")
    for send in preset["sends"]:
        lines.append(f'[ SEND ] {send["group"].upper()} — {send["label"]}  ({send["plugin_logic"]})')
        lines.append('         ' + ", ".join(f"{k}={_fmt_value(v)}" for k, v in send["settings"].items()))
        lines.append(f'         {send["why"]}')
    if preset["warnings"]:
        lines += ["", "AVVISI:"] + [f"  - {w}" for w in preset["warnings"]]
    return "\n".join(lines)


def render_logic_recipe(preset, rules=None):
    rules = rules or load_rules()
    by_id = {m["id"]: m for m in preset["modules"]}
    out = [f'# Ricetta Logic — "{preset["prompt"]}"', "",
           f'Generata da `chain_compiler.py` (regole v{preset["rules_version"]}) '
           f'sul profilo `{preset["source_profile"]["id"]}`.',
           "Solo plugin **stock** di Logic Pro, nella strip della traccia vocale, in quest'ordine.", "",
           f'Target: **{preset["output"]["loudness_target_lufs"]} LUFS**, '
           f'ceiling **{preset["output"]["true_peak_ceiling_dbfs"]} dBFS**.', ""]
    step = 0
    for entry in rules["logic_recipe"]:
        mod = by_id.get(entry["module"])
        if not mod or not mod["enabled"]:
            continue
        step += 1
        out += [f'## {step}. {entry["plugin"]} — {mod["label"]}', f'*{mod["why"]}*', "",
                f'> {entry["note"]}', "", "| parametro | valore | perché |", "|---|---|---|"]
        for pid, p in mod["params"].items():
            out.append(f'| `{pid}` | {_fmt_value(p["value"])} {p.get("unit", "")} | {p["why"]} |')
        out.append("")
    if preset["sends"]:
        out += ["## Mandate (bus aux — bus PARALLELI, mai in serie sulla voce)", ""]
        for send in preset["sends"]:
            out += [f'### {send["label"]} — {send["plugin_logic"]}', f'*{send["why"]}*', "",
                    "| parametro | valore |", "|---|---|"]
            out += [f'| `{k}` | {_fmt_value(v)} |' for k, v in send["settings"].items()]
            out += ["", f'> Manda la voce a un bus aux e imposta il livello di send a `send_db`. '
                        f'Se il bus ha un compressore in sidechain dalla voce, usa `duck_db` come riduzione.', ""]
    if preset["warnings"]:
        out += ["## Avvisi", ""] + [f"- {w}" for w in preset["warnings"]]
    return "\n".join(out)


# --------------------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------------------

def _fmt_value(value):
    """I booleani si leggono meglio come on/off in una ricetta da montare a mano."""
    if isinstance(value, bool):
        return "on" if value else "off"
    return value


def _slug(text):
    return re.sub(r"[^a-z0-9]+", "-", normalize(text)).strip("-")[:48]


def generate_examples(check=False):
    rules = load_rules()
    os.makedirs(EXAMPLES_DIR, exist_ok=True)
    stale, index = [], ["# Esempi generati", "",
                        "Rigenerati con `python3 tools/chain_compiler.py --examples`.",
                        "Sono il riferimento per la **parità Python ↔ C++** (vedi CHECKLIST.md).", ""]
    for prompt in EXAMPLE_PROMPTS:
        slug = _slug(prompt)
        preset = compile_preset(prompt, rules=rules)
        artifacts = {
            f"{slug}.preset.json": json.dumps(preset, indent=2, ensure_ascii=False, sort_keys=False) + "\n",
            f"{slug}.logic.md": render_logic_recipe(preset, rules) + "\n",
        }
        for name, content in artifacts.items():
            path = os.path.join(EXAMPLES_DIR, name)
            if check:
                existing = open(path, encoding="utf-8").read() if os.path.exists(path) else None
                if existing != content:
                    stale.append(name)
            else:
                with open(path, "w", encoding="utf-8") as fh:
                    fh.write(content)
        index.append(f'- `{slug}` — "{prompt}"')
    index_content = "\n".join(index) + "\n"
    index_path = os.path.join(EXAMPLES_DIR, "README.md")
    if check:
        existing_index = open(index_path, encoding="utf-8").read() if os.path.exists(index_path) else None
        if existing_index != index_content:
            stale.append("README.md")
        if stale:
            print("examples/ non allineati:\n  " + "\n  ".join(stale), file=sys.stderr)
            return 1
        print(f"examples/ allineati ({len(EXAMPLE_PROMPTS)} prompt).")
        return 0
    with open(index_path, "w", encoding="utf-8") as fh:
        fh.write(index_content)
    print(f"Generati {len(EXAMPLE_PROMPTS)} esempi in {EXAMPLES_DIR}")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="VOCAL FORGE — testo → vocal chain")
    ap.add_argument("prompt", nargs="?", help="la richiesta testuale")
    ap.add_argument("--profile", default="untreated_room_focusrite_scarlett",
                    help="profilo di sorgente (default: stanza non trattata + Focusrite + Scarlett)")
    ap.add_argument("--format", choices=["text", "json", "recipe", "intent"], default="text")
    ap.add_argument("--examples", action="store_true", help="(ri)genera examples/")
    ap.add_argument("--check", action="store_true", help="con --examples: verifica senza scrivere")
    args = ap.parse_args(argv)

    if args.examples:
        return generate_examples(check=args.check)
    if not args.prompt:
        ap.error("serve un prompt (oppure --examples)")

    rules = load_rules()
    preset = compile_preset(args.prompt, args.profile, rules)
    if args.format == "json":
        print(json.dumps(preset, indent=2, ensure_ascii=False))
    elif args.format == "intent":
        print(json.dumps(preset["intent"], indent=2, ensure_ascii=False))
    elif args.format == "recipe":
        print(render_logic_recipe(preset, rules))
    else:
        print(render_text(preset))
    return 0


if __name__ == "__main__":
    sys.exit(main())
