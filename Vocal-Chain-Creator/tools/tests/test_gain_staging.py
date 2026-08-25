"""Misura del gain staging della catena — il test che tiene onesta la taratura.

Non basta che i preset si compilino: devono anche *suonare* controllati. Questo file
ricostruisce, dai numeri del preset, il percorso del livello lungo i 13 moduli e misura:

  · guadagno statico accumulato (c1Makeup + c2Makeup + outGain);
  · riduzione di guadagno attesa per stadio (nessuno oltre 6 dB — CLAUDE.md);
  · quanto la saturazione lavora RISPETTO al livello che le arriva (sat excess);
  · headroom residuo davanti al limiter — che dopo la correzione B4 è l'ULTIMO stadio, con
    mix, mandate e output gain davanti a lui — e quanto il limiter è quindi costretto a fare.

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

    # Da qui in poi l'ordine è quello del DSP DOPO la correzione B4: mix, mandate, output gain,
    # e il limiter come ULTIMO stadio. Prima il limiter stava in mezzo e tutto quello che veniva
    # dopo (output gain e tre bus paralleli) poteva superare il ceiling senza che nessuno lo vedesse.
    out = _p(preset, "out")
    level += 20.0 * math.log10(max(1e-6, out["mix"] / 100.0))

    # le mandate sono bus paralleli sommati alla voce PRIMA del limiter: caso peggiore, in fase.
    # Il MIX le scala insieme alla voce (correzione del terzo passaggio: prima suonavano a livello
    # pieno anche a mix 0 %), quindi entra qui dentro e non solo sulla riga sopra.
    mix_scale = max(1e-6, out["mix"] / 100.0)
    sends_gain = mix_scale * sum(10.0 ** (send["settings"]["send_db"] / 20.0)
                                 for send in preset.get("sends", [])
                                 if "send_db" in send.get("settings", {}))
    level += 20.0 * math.log10(1.0 + sends_gain)

    level += out["outGain"]

    ceiling = _p(preset, "limiter")["limCeiling"]
    headroom = ceiling - level
    limiter_gr = max(0.0, -headroom)
    level = min(level, ceiling)

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


def _sat(value):
    """Il livello rispetto al ginocchio della saturazione, o un trattino se la saturazione è spenta."""
    return f"{value:.1f}" if value is not None else "—"


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


class TestSecondPass(unittest.TestCase):
    """Secondo giro di taratura: gli invarianti nati dopo le correzioni del DSP (REVIEW.md B2/B4/B5/B11).

    Qui si controlla che i NUMERI abbiano ancora un riferimento; quanto poi facciano davvero sul
    segnale lo misura `vocalforge_selftest` (banda sibilante, ducking per bus, inclinazione del tilt).
    """

    MAX_SAT_TILT_DB = 1.5        # il tilt vale il DOPPIO fra 200 Hz e 8 kHz: 1.5 -> 3.1 dB misurati
    MAX_DOUBLER_DUCK_DB = 4.0    # il doubler allarga MENTRE la voce parla: duckarlo lo cancella

    def setUp(self):
        prompts = [RULES["lexicon"]["artists"][a]["aliases"][0] for a in ARTISTS] + list(EXAMPLE_PROMPTS)
        self.presets = {p: compile_preset(p, rules=RULES) for p in dict.fromkeys(prompts)}

    def test_de_esser_thresholds_are_referenced_to_the_working_level(self):
        """Erano numeri assoluti in dBFS, scelti quando il de-esser era un allpass.

        La banda alta di una 's' arriva ~10 dB sotto il picco di lavoro (misurato sul DSP): la soglia
        deve stare sotto di quella, ma non cosi' sotto da agganciare anche le vocali.
        """
        for prompt, preset in self.presets.items():
            work_peak = float(preset["source_profile"]["interface"]["target_peak_dbfs"])
            for module, key in (("ds1", "ds1Thresh"), ("ds2", "ds2Thresh")):
                params = _p(preset, module)
                if params is None:
                    continue
                self.assertLessEqual(params[key], work_peak - 10.0,
                                     f"{prompt}/{key}: aggancia anche la voce, non solo le esse")
                self.assertGreaterEqual(params[key], work_peak - 26.0,
                                        f"{prompt}/{key}: cosi' in basso non aggancia piu' niente")

    def test_de_esser_range_cannot_produce_a_lisp(self):
        """La riduzione sulla banda alta e' un tetto: oltre i 5 dB la 's' sparisce."""
        for prompt, preset in self.presets.items():
            ds1, ds2 = _p(preset, "ds1"), _p(preset, "ds2")
            self.assertLessEqual(ds1["ds1Range"], 5.0, f"{prompt}/ds1Range")
            if ds2 is not None:
                self.assertLessEqual(ds2["ds2Range"], 3.5, f"{prompt}/ds2Range")
                self.assertLess(ds2["ds2Range"], ds1["ds1Range"],
                                f"{prompt}: il secondo de-esser deve rifinire, non correggere")

    def test_both_de_essers_work_on_the_high_band_only(self):
        """Da quando il crossover e' vero, il modo 'wide' abbassa TUTTA la voce a ogni 's'."""
        for prompt, preset in self.presets.items():
            for module, key in (("ds1", "ds1Mode"), ("ds2", "ds2Mode")):
                params = _p(preset, module)
                if params is not None:
                    self.assertEqual(params[key], "split", f"{prompt}/{key}")

    def test_sat_tilt_stays_a_colour(self):
        """satTilt e' due shelf speculari: il dislivello che produce e' il DOPPIO del valore."""
        for prompt, preset in self.presets.items():
            sat = _p(preset, "sat")
            if sat is not None:
                self.assertLessEqual(abs(sat["satTilt"]), self.MAX_SAT_TILT_DB, f"{prompt}/satTilt")

    def test_the_doubler_is_not_ducked_like_a_reverb(self):
        """Il ducking del doubler non aveva effetto quando questi numeri sono stati scelti (B5)."""
        for prompt, preset in self.presets.items():
            for send in preset.get("sends", []):
                if send["id"] == "fx_doubler":
                    self.assertLessEqual(send["settings"]["duck_db"], self.MAX_DOUBLER_DUCK_DB, prompt)

    def test_reverb_and_delay_are_still_ducked_under_the_voice(self):
        """Questi invece restano dov'erano: misurati, fanno 3.9-5.0 dB sotto la parola."""
        for prompt, preset in self.presets.items():
            for send in preset.get("sends", []):
                if send["group"] in ("reverb", "delay"):
                    self.assertGreaterEqual(send["settings"]["duck_db"], 4.0, f"{prompt}/{send['id']}")


class TestThirdPass(unittest.TestCase):
    """Terzo giro: gli invarianti nati dopo la riscrittura del limiter, del room tamer e del MIX.

    Anche qui i numeri si controllano sul preset; quanto facciano davvero sul segnale lo misura
    `vocalforge_selftest` (sezioni "room tamer", "mandate", "coda dichiarata").
    """

    MAX_ROOM_DEPTH_DB = 8.0        # CLAUDE.md: mai oltre -8 dB su una singola banda
    MIN_REVERB_SEND_DB = -29.0     # misurato: sotto, il wet finisce oltre 43 dB sotto la voce
    MAX_DECLARED_TAIL_S = 6.0      # oltre, il bounce si allunga senza che si senta niente

    def setUp(self):
        prompts = [RULES["lexicon"]["artists"][a]["aliases"][0] for a in ARTISTS] + list(EXAMPLE_PROMPTS)
        self.presets = {p: compile_preset(p, rules=RULES) for p in dict.fromkeys(prompts)}

    @staticmethod
    def declared_tail_seconds(preset):
        """Stessa formula di ChainDsp::getTailSeconds(), che dal terzo passaggio non e' piu' una
        costante: decay e feedback allungano la coda che il plugin chiede all'host per il bounce."""
        reverb = next((s for s in preset.get("sends", []) if s["group"] == "reverb"), None)
        delay = next((s for s in preset.get("sends", []) if s["group"] == "delay"), None)
        reverb_tail = 0.0
        if reverb is not None and "decay_s" in reverb.get("settings", {}):
            reverb_tail = reverb["settings"]["decay_s"] + reverb["settings"].get("predelay", 0.0) * 0.001
        delay_tail = 0.0
        if delay is not None:
            feedback = min(0.85, max(0.0, delay["settings"].get("feedback", 0.0) / 100.0))
            repeats = min(40.0, max(1.0, -60.0 / (20.0 * math.log10(feedback)))) if feedback > 0.01 else 1.0
            delay_tail = delay["settings"].get("time_ms", 375.0) * 0.001 * repeats
        return min(20.0, max(0.5, max(reverb_tail, delay_tail) + 0.5))

    def test_room_threshold_is_referenced_to_the_level_a_mode_really_reaches(self):
        """Era un numero assoluto in dBFS (-25) tarato su un room tamer che leggeva tutto il blocco.

        Col detector causale e normalizzato quel numero non agganciava piu' niente: misurati sul DSP,
        i notch toglievano 0.0-0.4 dB su tutti e sei i profili artista. La soglia adesso parte dal
        livello a cui un modo arriva davvero al detector (12 dB sotto il picco di lavoro).
        """
        for prompt, preset in self.presets.items():
            room = _p(preset, "room")
            if room is None:
                continue
            mode_peak = float(preset["source_profile"]["interface"]["target_peak_dbfs"]) - 12.0
            self.assertLessEqual(room["roomThresh"], mode_peak - 4.0,
                                 f"{prompt}: soglia cosi' alta che i notch non agganciano il modo")
            self.assertGreaterEqual(room["roomThresh"], mode_peak - 14.0,
                                    f"{prompt}: soglia cosi' bassa che i notch lavorano sulla voce")

    def test_a_cleaner_request_makes_the_room_tamer_engage_earlier(self):
        """Il segno era rovesciato: piu' pulizia chiesta alzava la soglia e annullava i notch."""
        by_cleanliness = sorted(
            ((preset["intent"]["axes"]["cleanliness"], _p(preset, "room")["roomThresh"])
             for preset in self.presets.values() if _p(preset, "room") is not None),
            key=lambda pair: pair[0])
        for (clean_low, thresh_low), (clean_high, thresh_high) in zip(by_cleanliness, by_cleanliness[1:]):
            if clean_high > clean_low:
                self.assertLessEqual(thresh_high, thresh_low + 1e-6,
                                     f"cleanliness {clean_high} aggancia piu' tardi di {clean_low}")

    def test_room_depth_stays_inside_the_eight_db_limit(self):
        for prompt, preset in self.presets.items():
            room = _p(preset, "room")
            if room is None:
                continue
            for band in (1, 2, 3):
                self.assertLessEqual(abs(room[f"room{band}Depth"]), self.MAX_ROOM_DEPTH_DB,
                                     f"{prompt}/room{band}Depth")

    def test_the_reverb_send_survives_the_mix_scaling(self):
        """Dal terzo passaggio il MIX scala anche le mandate. Il rapporto ambiente/voce non cambia
        (il MIX scala tutt'e due — misurato: meno di 1,5 dB di scarto fra mix 78 % e mix 100 %), ma
        una mandata gia' troppo bassa in partenza sparisce comunque: `rev_ambience` stava a -32 dB e
        con mezzo secondo di coda finiva 43 dB sotto la voce."""
        for prompt, preset in self.presets.items():
            for send in preset.get("sends", []):
                if send["group"] == "reverb" and "send_db" in send.get("settings", {}):
                    self.assertGreaterEqual(send["settings"]["send_db"], self.MIN_REVERB_SEND_DB, prompt)

    def test_the_declared_tail_stays_sane(self):
        """`getTailLengthSeconds` non e' piu' una costante: decay e feedback allungano il bounce."""
        for prompt, preset in self.presets.items():
            tail = self.declared_tail_seconds(preset)
            self.assertLessEqual(tail, self.MAX_DECLARED_TAIL_S, f"{prompt}: coda dichiarata {tail:.2f} s")
            self.assertGreaterEqual(tail, 0.5, prompt)


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
                f'{_sat(m["sat_excess_db"]):>8}'
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
