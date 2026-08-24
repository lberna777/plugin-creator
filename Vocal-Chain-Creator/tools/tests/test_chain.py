"""Test delle regole di catena: invarianti di CLAUDE.md e PLUGIN_SPEC.md, verificate sui preset compilati."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from chain_compiler import EXAMPLE_PROMPTS, compile_preset, generate_examples, load_rules  # noqa: E402

RULES = load_rules()
PRESETS = [compile_preset(p, rules=RULES) for p in EXAMPLE_PROMPTS]
EXPECTED_ORDER = [m["id"] for m in RULES["chain"]]


def params(preset, module_id):
    for mod in preset["modules"]:
        if mod["id"] == module_id:
            return mod["params"] if mod["enabled"] else None
    raise AssertionError(f"modulo assente: {module_id}")


class TestStructure(unittest.TestCase):
    def test_module_order_is_fixed(self):
        for preset in PRESETS:
            self.assertEqual([m["id"] for m in preset["modules"]], EXPECTED_ORDER, preset["prompt"])

    def test_every_module_and_param_has_a_why(self):
        for preset in PRESETS:
            for mod in preset["modules"]:
                self.assertTrue(mod["why"].strip(), f'{preset["prompt"]}/{mod["id"]}')
                for pid, p in mod["params"].items():
                    self.assertTrue(str(p["why"]).strip(), f'{preset["prompt"]}/{mod["id"]}/{pid}')

    def test_why_templates_are_fully_resolved(self):
        for preset in PRESETS:
            for mod in preset["modules"]:
                self.assertNotIn("{", mod["why"], mod["id"])
                for pid, p in mod["params"].items():
                    self.assertNotIn("{", str(p["why"]), pid)

    def test_source_profile_is_declared_in_the_preset(self):
        for preset in PRESETS:
            self.assertEqual(preset["source_profile"]["id"], "untreated_room_focusrite_scarlett")
            self.assertTrue(preset["io"]["stereo_is_dual_mono"], "Scarlett a due ingressi: non è vero stereo")


class TestDspInvariants(unittest.TestCase):
    """Le regole di CLAUDE.md non sono commenti: qui diventano assert."""

    def test_hpf_stays_in_the_allowed_automatic_range(self):
        for preset in PRESETS:
            freq = params(preset, "hpf")["hpfFreq"]["value"]
            self.assertGreaterEqual(freq, 70, preset["prompt"])
            self.assertLessEqual(freq, 140, preset["prompt"])

    def test_room_tamer_never_exceeds_8_db_on_a_band(self):
        for preset in PRESETS:
            p = params(preset, "room")
            for band in ("room1Depth", "room2Depth", "room3Depth"):
                self.assertGreaterEqual(p[band]["value"], -8, f'{preset["prompt"]}/{band}')

    def test_room_tamer_has_at_most_three_notches(self):
        notches = {k for k in RULES["chain"][3]["params"] if k.endswith("Freq")}
        self.assertLessEqual(len(notches), 3)

    def test_compressor_ratios_within_spec(self):
        for preset in PRESETS:
            self.assertLessEqual(params(preset, "comp1")["c1Ratio"]["value"], 10)
            p2 = params(preset, "comp2")
            if p2:
                self.assertLessEqual(p2["c2Ratio"]["value"], 6)

    def test_gate_is_key_filtered_within_the_voice_band(self):
        for preset in PRESETS:
            p = params(preset, "gate")
            if p:
                self.assertLess(p["gateKeyLo"]["value"], p["gateKeyHi"]["value"])
                self.assertGreaterEqual(p["gateKeyLo"]["value"], 60)

    def test_second_de_esser_is_on_whenever_saturation_is(self):
        for preset in PRESETS:
            if params(preset, "sat") is not None:
                self.assertIsNotNone(params(preset, "ds2"),
                                     f'{preset["prompt"]}: saturazione senza de-esser 2')

    def test_de_essers_track_the_pitch_register(self):
        low = compile_preset("voce bassa baritono", rules=RULES)
        high = compile_preset("voce acuta soprano", rules=RULES)
        self.assertLess(params(low, "ds1")["ds1Freq"]["value"], params(high, "ds1")["ds1Freq"]["value"])

    def test_limiter_ceiling_leaves_true_peak_headroom(self):
        for preset in PRESETS:
            self.assertLessEqual(params(preset, "limiter")["limCeiling"]["value"], -1.0)

    def test_no_reverb_or_delay_inside_the_chain(self):
        for preset in PRESETS:
            ids = [m["id"] for m in preset["modules"]]
            self.assertNotIn("reverb", ids)
            self.assertNotIn("delay", ids)
            for send in preset["sends"]:
                self.assertIn(send["group"], ("reverb", "delay"))


class TestIntentDrivesTheChain(unittest.TestCase):
    def test_more_density_means_lower_compressor_threshold(self):
        soft = compile_preset("voce naturale e dinamica", rules=RULES)
        hard = compile_preset("voce molto compressa e schiacciata", rules=RULES)
        self.assertLess(params(hard, "comp1")["c1Thresh"]["value"], params(soft, "comp1")["c1Thresh"]["value"])

    def test_clean_request_turns_saturation_off(self):
        self.assertIsNone(params(compile_preset("voce pulita e trasparente", rules=RULES), "sat"))

    def test_vintage_request_picks_tape_saturation(self):
        preset = compile_preset("voce vintage a nastro", rules=RULES)
        self.assertEqual(params(preset, "sat")["satType"]["value"], "tape")

    def test_dynamic_request_disables_the_glue_stage(self):
        preset = compile_preset("cantautore, voce molto dinamica e naturale", rules=RULES)
        self.assertIsNone(params(preset, "comp2"), "il secondo stadio dovrebbe spegnersi")

    def test_treated_room_changes_the_chain(self):
        prompt = "pop moderno, voce presente"
        untreated = compile_preset(prompt, "untreated_room_focusrite_scarlett", RULES)
        treated = compile_preset(prompt, "treated_room", RULES)
        self.assertIsNotNone(params(untreated, "room"))
        self.assertIsNone(params(treated, "room"), "in stanza trattata il room-tamer non serve")
        self.assertNotEqual(params(untreated, "hpf")["hpfFreq"]["value"],
                            params(treated, "hpf")["hpfFreq"]["value"])

    def test_untreated_room_always_warns_about_early_reflections(self):
        for preset in PRESETS:
            self.assertTrue(any("riflessioni" in w for w in preset["warnings"]), preset["prompt"])


class TestSends(unittest.TestCase):
    """I send sono bus paralleli: uno solo per gruppo, mai dentro la catena."""

    def test_at_most_one_variant_per_group(self):
        for preset in PRESETS:
            groups = [s["group"] for s in preset["sends"]]
            self.assertEqual(len(groups), len(set(groups)), preset["prompt"])

    def test_every_send_declares_group_label_plugin_and_why(self):
        for preset in PRESETS:
            for send in preset["sends"]:
                for field in ("id", "group", "label", "plugin_logic", "why"):
                    self.assertTrue(str(send[field]).strip(), f'{preset["prompt"]}/{field}')
                self.assertIn(send["group"], ("reverb", "delay"))

    def test_every_send_is_filtered_ducked_and_leveled(self):
        for preset in PRESETS:
            for send in preset["sends"]:
                st = send["settings"]
                for field in ("hpf", "lpf", "duck_db", "send_db"):
                    self.assertIn(field, st, f'{send["id"]}: manca {field}')
                self.assertLess(st["hpf"], st["lpf"], send["id"])
                self.assertLessEqual(st["send_db"], -6, "una mandata non può stare al livello della voce")

    def test_surgical_spoken_request_gets_no_reverb_at_all(self):
        preset = compile_preset("podcast, voce parlata pulita, senza rumore di fondo", rules=RULES)
        self.assertEqual([s for s in preset["sends"] if s["group"] == "reverb"], [])

    def test_spoken_gets_short_ambience_not_a_hall(self):
        preset = compile_preset("podcast, voce parlata", rules=RULES)
        reverbs = [s for s in preset["sends"] if s["group"] == "reverb"]
        self.assertEqual([s["id"] for s in reverbs], ["rev_ambience"])
        self.assertLessEqual(reverbs[0]["settings"]["decay_s"], 1.0)

    def test_vintage_picks_room_and_slapback(self):
        preset = compile_preset("voce vintage a nastro", rules=RULES)
        ids = {s["id"] for s in preset["sends"]}
        self.assertIn("rev_room", ids)
        self.assertIn("dly_slap", ids)

    def test_intimate_picks_the_long_hall(self):
        preset = compile_preset("voce sussurrata intima", rules=RULES)
        self.assertIn("rev_hall", {s["id"] for s in preset["sends"]})

    def test_rhythmic_delivery_gets_the_dotted_delay(self):
        preset = compile_preset("rap, flow serrato", rules=RULES)
        delays = [s for s in preset["sends"] if s["group"] == "delay"]
        self.assertEqual([s["id"] for s in delays], ["dly_eighth"])

    def test_send_selection_is_mutually_exclusive_by_priority(self):
        preset = compile_preset("voce vintage a nastro intima", rules=RULES)   # room e hall entrambe vere
        self.assertEqual([s["id"] for s in preset["sends"] if s["group"] == "reverb"], ["rev_room"])


class TestDeterminismAndExamples(unittest.TestCase):
    def test_compilation_is_deterministic(self):
        first = compile_preset("trap aggressiva", rules=RULES)
        for _ in range(25):
            self.assertEqual(compile_preset("trap aggressiva", rules=RULES), first)

    def test_examples_are_in_sync(self):
        self.assertEqual(generate_examples(check=True), 0,
                         "examples/ non allineati: esegui `python3 tools/chain_compiler.py --examples`")


class TestNoHardcodedDspValues(unittest.TestCase):
    """CLAUDE.md: un numero DSP nel codice è un bug, come un pixel hardcoded nella UI."""

    def test_compiler_source_has_no_domain_numbers(self):
        import re
        source = open(os.path.join(os.path.dirname(__file__), "..", "chain_compiler.py"), encoding="utf-8").read()
        source = re.sub(r'"""(?:.|\n)*?"""', "", source)               # via i docstring
        source = re.sub(r"#.*", "", source)                              # via i commenti
        source = re.sub(r"(?:f?r?)(\'[^\']*\'|\"[^\"]*\")", "S", source)   # via le stringhe (formattazione, regex)
        # Unici numeri ammessi nel codice: indici e costanti di presentazione, nessuna delle quali è DSP.
        allowed = {"0", "0.0", "1", "1.0", "2", "3", "4", "6", "9", "-1", "48"}
        # indici e step, scala neutra dei modificatori, precisioni di arrotondamento,
        # larghezze di colonna della stampa, lunghezza dello slug: nessuno è un valore DSP.
        numbers = {n for n in re.findall(r"(?<![\w.])-?\d+\.?\d*", source)} - allowed
        self.assertEqual(numbers, set(), f"numeri sospetti nel codice: {sorted(numbers)} — devono stare in rules.json")


if __name__ == "__main__":
    unittest.main()
