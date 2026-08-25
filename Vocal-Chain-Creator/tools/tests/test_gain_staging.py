"""Misura del gain staging della catena — il test che tiene onesta la taratura.

Non basta che i preset si compilino: devono anche *suonare* controllati. Questo file
ricostruisce, dai numeri del preset, il percorso del livello lungo i 13 moduli e misura:

  · guadagno statico accumulato (c1Makeup + c2Makeup + outGain);
  · riduzione di guadagno attesa per stadio (nessuno oltre 6 dB — CLAUDE.md);
  · quanto la saturazione lavora RISPETTO al livello che le arriva (sat excess);
  · headroom residuo davanti al limiter, e quanto il limiter è quindi costretto a fare.

Il tutto a DUE livelli d'ingresso: quello nominale del profilo di sorgente e uno basso
(-18 dBFS di picco). Il caso "gain basso" è quello che l'utente lamentava: con i makeup
legati alla densità invece che alla riduzione reale, la catena spingeva comunque il
segnale dentro saturazione e limiter anche quando in ingresso non c'era niente da domare.

Uso come script:   python3 tools/tests/test_gain_staging.py            (tabella per profilo artista)
                   python3 tools/tests/test_gain_staging.py --json     (per confronti prima/dopo)
Uso come test:     python3 -m unittest test_gain_staging
"""
import argparse
import json
import math
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from chain_compiler import EXAMPLE_PROMPTS, compile_preset, load_rules  # noqa: E402

RULES = load_rules()
ARTISTS = list(RULES["lexicon"]["artists"].keys())

# Limiti di CLAUDE.md / CHAIN_ARCHITECTURE.md, non scelte di questo file.
MAX_GR_PER_STAGE_DB = 6.0        # "nessuno stadio da solo può superare 6 dB"
MAX_LIMITER_GR_DB = 3.0          # "se il limiter lavora più di 2-3 dB, sono sbagliati gli stadi prima"
LOW_INPUT_PEAK_DBFS = -18.0      # il caso "gain d'ingresso basso" lamentato dall'utente


def _p(preset, module_id):
    for mod in preset["modules"]:
        if mod["id"] == module_id:
            return {k: v["value"] for k, v in mod["params"].items()} if mod["enabled"] else None
    raise AssertionError(f"modulo assente: {module_id}")


def _gr(level_db, thresh_db, ratio):
    """Riduzione di guadagno di un compressore hard-knee a regime, in dB."""
    if ratio <= 1.0 or level_db <= thresh_db:
        return 0.0
    return (level_db - thresh_db) * (1.0 - 1.0 / ratio)


def stage_trace(preset, input_peak_dbfs=None):
    """Percorre la catena in dB e restituisce la misura. Modello statico, deterministico.

    Approssimazioni dichiarate: le EQ sottrattive (che tolgono headroom-positivo) sono
    ignorate a favore della sicurezza; dell'EQ tonale si conta solo il boost massimo,
    che è il caso peggiore per il picco.
    """
    iface = preset["source_profile"]["interface"]
    if input_peak_dbfs is None:
        input_peak_dbfs = float(iface["assumed_peak_dbfs"])

    trim = _p(preset, "trim")["inTrim"]
    level = input_peak_dbfs + trim

    c1 = _p(preset, "comp1")
    gr1 = _gr(level, c1["c1Thresh"], c1["c1Ratio"]) if c1 else 0.0
    makeup1 = c1["c1Makeup"] if c1 else 0.0
    level = level - gr1 + makeup1

    c2 = _p(preset, "comp2")
    gr2 = _gr(level, c2["c2Thresh"], c2["c2Ratio"]) if c2 else 0.0
    makeup2 = c2["c2Makeup"] if c2 else 0.0
    level = level - gr2 + makeup2

    sat = _p(preset, "sat")
    sat_in = level
    if sat:
        drive = sat["satDrive"] / 100.0
        amount = 1.0 + drive * 8.0                       # ChainDsp: amount = 1 + drive * 8
        knee_dbfs = -20.0 * math.log10(amount)           # sopra questo livello la tanh piega
        sat_excess = sat_in - knee_dbfs
        # guadagno a piccolo segnale del ramo saturo, miscelato come fa jmap(drive, dry, wet)
        wet_gain = amount / max(1.0, amount * 0.8)
        level += 20.0 * math.log10((1.0 - drive) + drive * wet_gain)
    else:
        drive, sat_excess = 0.0, float("-inf")

    tone = _p(preset, "eqTone")
    tone_boost = max([0.0] + [tone[k] for k in ("tone1Gain", "tone2Gain", "tone3Gain", "airGain")
                              if k in tone]) if tone else 0.0
    level += tone_boost

    ceiling = _p(preset, "limiter")["limCeiling"]
    headroom = ceiling - level
    limiter_gr = max(0.0, -headroom)
    level = min(level, ceiling)

    out = _p(preset, "out")
    level += out["outGain"]
    level += 20.0 * math.log10(max(1e-6, out["mix"] / 100.0))

    return {
        "prompt": preset["prompt"],
        "artist": preset.get("artist"),
        "input_peak_dbfs": round(input_peak_dbfs, 2),
        "makeup1_db": round(makeup1, 2),
        "makeup2_db": round(makeup2, 2),
        "out_gain_db": round(out["outGain"], 2),
        "total_static_gain_db": round(makeup1 + makeup2 + out["outGain"], 2),
        "gr1_db": round(gr1, 2),
        "gr2_db": round(gr2, 2),
        "total_gr_db": round(gr1 + gr2, 2),
        "sat_drive_pct": round(drive * 100.0, 1),
        "sat_in_dbfs": round(sat_in, 2),
        "sat_excess_db": (round(sat_excess, 2) if sat else None),
        "headroom_before_limiter_db": round(headroom, 2),
        "limiter_gr_db": round(limiter_gr, 2),
        "mix_pct": out["mix"],
        "out_peak_dbfs": round(level, 2),
    }


def measure_all():
    """Misura ogni profilo artista (e ogni prompt di esempio) al livello nominale e a gain basso."""
    report = {}
    prompts = [RULES["lexicon"]["artists"][a]["aliases"][0] for a in ARTISTS] + list(EXAMPLE_PROMPTS)
    seen = set()
    for prompt in prompts:
        if prompt in seen:
            continue
        seen.add(prompt)
        preset = compile_preset(prompt, rules=RULES)
        report[prompt] = {
            "nominal": stage_trace(preset),
            "low_gain": stage_trace(preset, LOW_INPUT_PEAK_DBFS),
        }
    return report


class TestGainStaging(unittest.TestCase):
    """Le regole di taratura diventano assert: valgono a gain nominale E a gain basso."""

    def setUp(self):
        self.report = measure_all()

    def test_no_compressor_stage_exceeds_six_db(self):
        for prompt, cases in self.report.items():
            for case, m in cases.items():
                self.assertLessEqual(m["gr1_db"], MAX_GR_PER_STAGE_DB, f"{prompt}/{case}/comp1")
                self.assertLessEqual(m["gr2_db"], MAX_GR_PER_STAGE_DB, f"{prompt}/{case}/comp2")

    def test_limiter_is_a_ceiling_not_an_effect(self):
        for prompt, cases in self.report.items():
            for case, m in cases.items():
                self.assertLessEqual(m["limiter_gr_db"], MAX_LIMITER_GR_DB, f"{prompt}/{case}")

    def test_headroom_survives_in_front_of_the_limiter(self):
        for prompt, cases in self.report.items():
            self.assertGreater(cases["nominal"]["headroom_before_limiter_db"], 0.0, prompt)

    def test_saturation_is_referenced_to_the_working_level(self):
        """Il drive non può portare il segnale molto oltre il ginocchio della tanh."""
        for prompt, cases in self.report.items():
            for case, m in cases.items():
                if m["sat_excess_db"] is not None:
                    self.assertLessEqual(m["sat_excess_db"], 0.0, f"{prompt}/{case}")

    def test_low_input_gain_is_not_pushed_back_up_blindly(self):
        """Il caso lamentato: con poco gain in ingresso la catena non deve comunque spingere.

        Il guadagno statico (makeup + output) non può superare il recupero che avrebbe senso
        a gain nominale: se i compressori non riducono, i makeup non devono comunque alzare.
        """
        for prompt, cases in self.report.items():
            low = cases["low_gain"]
            self.assertLessEqual(low["makeup1_db"] + low["makeup2_db"], MAX_GR_PER_STAGE_DB,
                                 f"{prompt}: makeup statico troppo alto a gain basso")
            self.assertLess(low["sat_in_dbfs"], cases["nominal"]["sat_in_dbfs"] - 3.0,
                            f"{prompt}: a gain basso la saturazione riceve lo stesso livello")

    def test_makeup_tracks_the_reduction_it_is_supposed_to_recover(self):
        """Un makeup che cresce con la densità invece che con la riduzione è il bug d'origine."""
        for prompt, cases in self.report.items():
            m = cases["nominal"]
            self.assertLessEqual(m["makeup1_db"], m["gr1_db"] + 0.6, f"{prompt}/comp1")
            self.assertLessEqual(m["makeup2_db"], m["gr2_db"] + 0.6, f"{prompt}/comp2")


def _table(report):
    head = (f'{"prompt":<44}{"makeup":>8}{"out":>7}{"tot":>7}{"gr1":>7}{"gr2":>7}'
            f'{"drive":>7}{"satEx":>8}{"head":>7}{"limGR":>7}{"outPk":>8}')
    lines = []
    for case in ("nominal", "low_gain"):
        lines += ["", f"=== livello d'ingresso: {case} ===", head, "-" * len(head)]
        for prompt, cases in report.items():
            m = cases[case]
            lines.append(
                f'{prompt[:43]:<44}{m["makeup1_db"] + m["makeup2_db"]:>8.1f}{m["out_gain_db"]:>7.1f}'
                f'{m["total_static_gain_db"]:>7.1f}{m["gr1_db"]:>7.2f}{m["gr2_db"]:>7.2f}'
                f'{m["sat_drive_pct"]:>7.0f}'
                f'{(m["sat_excess_db"] if m["sat_excess_db"] is not None else float("nan")):>8.1f}'
                f'{m["headroom_before_limiter_db"]:>7.1f}{m["limiter_gr_db"]:>7.2f}{m["out_peak_dbfs"]:>8.1f}')
    return "\n".join(lines)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="misura del gain staging di VOCAL FORGE")
    ap.add_argument("--json", action="store_true", help="stampa la misura in JSON (per diff prima/dopo)")
    ap.add_argument("--test", action="store_true", help="esegui gli assert invece della tabella")
    args, rest = ap.parse_known_args()
    if args.test:
        sys.argv = [sys.argv[0]] + rest
        unittest.main()
    else:
        data = measure_all()
        print(json.dumps(data, indent=2, ensure_ascii=False) if args.json else _table(data))
