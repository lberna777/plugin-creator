"""Quinto giro: la voce esce a volume pareggiato e il trim può essere misurato (ASCOLTA).

Il difetto che questi test tengono chiuso: `outGain` portava la voce verso il target di loudness
del MASTER (-9 LUFS sulla trap), quindi il plugin acceso suonava sempre più forte di quello spento.
La misura vera (loudness dentro/fuori) sta nel self test C++; qui si controllano le regole.
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from chain_compiler import EXAMPLE_PROMPTS, compile_preset, load_rules  # noqa: E402

RULES = load_rules()


def param(preset, module_id, param_id):
    for mod in preset["modules"]:
        if mod["id"] == module_id:
            return mod["params"][param_id]
    raise KeyError(module_id)


class TestNoMasterTargetOnTheVoice(unittest.TestCase):
    def test_out_gain_is_zero_for_every_example(self):
        for prompt in EXAMPLE_PROMPTS:
            out = param(compile_preset(prompt, rules=RULES), "out", "outGain")
            self.assertEqual(out["value"], 0.0, prompt)

    def test_out_gain_rule_does_not_read_the_loudness_target(self):
        out_rule = next(m for m in RULES["chain"] if m["id"] == "out")["params"]["outGain"]
        self.assertNotIn("loudness_target", out_rule["expr"])

    def test_level_match_settings_are_declared(self):
        level_match = RULES["io_defaults"]["level_match"]
        for key in ("window_s", "gate_lufs", "max_gain_db", "ramp_ms", "warmup_s"):
            self.assertIn(key, level_match)
        self.assertGreater(level_match["window_s"], 0.4)       # più lunga di un blocco BS.1770
        self.assertGreater(level_match["max_gain_db"], 0)


class TestMeasuredTrim(unittest.TestCase):
    PROMPT = "voce trap aggressiva ma non stridula"

    def test_without_measurement_the_profile_estimate_decides(self):
        preset = compile_preset(self.PROMPT, rules=RULES)
        trim = param(preset, "trim", "inTrim")
        iface = preset["source_profile"]["interface"]
        self.assertEqual(trim["value"], iface["target_peak_dbfs"] - iface["assumed_peak_dbfs"])
        self.assertIn("stimati dal profilo", trim["why"])
        self.assertNotIn("measurement", preset)

    def test_measurement_replaces_the_assumed_peak(self):
        measurement = {"peak_dbfs": -24.0, "rms_dbfs": -38.0, "seconds": 6.0}
        preset = compile_preset(self.PROMPT, rules=RULES, measurement=measurement)
        trim = param(preset, "trim", "inTrim")
        target = preset["source_profile"]["interface"]["target_peak_dbfs"]
        self.assertEqual(trim["value"], target - measurement["peak_dbfs"])     # una take bassa si ALZA
        self.assertIn("misurati su 6 s", trim["why"])
        self.assertEqual(preset["measurement"], measurement)

    def test_measured_trim_is_clamped_like_the_parameter(self):
        preset = compile_preset(self.PROMPT, rules=RULES,
                                measurement={"peak_dbfs": -60.0, "rms_dbfs": -75.0, "seconds": 6.0})
        self.assertEqual(param(preset, "trim", "inTrim")["value"], 24.0)

    def test_measurement_changes_only_the_trim(self):
        plain = compile_preset(self.PROMPT, rules=RULES)
        measured = compile_preset(self.PROMPT, rules=RULES,
                                  measurement={"peak_dbfs": -20.0, "rms_dbfs": -34.0, "seconds": 6.0})
        for a, b in zip(plain["modules"], measured["modules"]):
            for pid, p in a["params"].items():
                if (a["id"], pid) == ("trim", "inTrim"):
                    continue
                self.assertEqual(p["value"], b["params"][pid]["value"], f'{a["id"]}.{pid}')


if __name__ == "__main__":
    unittest.main()
