"""Test dei profili artista: mock-up del suono, non copia di una catena reale."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from chain_compiler import compile_preset, load_rules, parse_intent  # noqa: E402

RULES = load_rules()
ARTISTS = list(RULES["lexicon"]["artists"].keys())


def sends_of(preset, group):
    return [s for s in preset["sends"] if s["group"] == group]


class TestArtistRecognition(unittest.TestCase):
    def test_every_alias_resolves_to_its_artist(self):
        for key, definition in RULES["lexicon"]["artists"].items():
            for alias in definition["aliases"]:
                self.assertEqual(parse_intent(alias, RULES)["artist"], key, alias)

    def test_artist_sets_genre_delivery_pitch_and_loudness(self):
        for key, definition in RULES["lexicon"]["artists"].items():
            intent = parse_intent(definition["aliases"][0], RULES)
            self.assertEqual(intent["genre"], definition["genre"], key)
            self.assertEqual(intent["delivery"], definition["delivery"], key)
            self.assertEqual(intent["pitch_class"], definition["pitch_class"], key)
            self.assertEqual(intent["loudness_target_lufs"], definition["loudness_target_lufs"], key)

    def test_two_aliases_of_the_same_artist_count_once(self):
        self.assertEqual(parse_intent("sfera", RULES)["axes"],
                         parse_intent("sfera ebbasta", RULES)["axes"])

    def test_a_descriptor_still_corrects_the_artist_profile(self):
        base = compile_preset("capo plaza", rules=RULES)
        darker = compile_preset("capo plaza, ma molto scura", rules=RULES)
        self.assertLess(darker["intent"]["axes"]["brightness"], base["intent"]["axes"]["brightness"])

    def test_artist_name_is_not_reported_as_unknown(self):
        for definition in RULES["lexicon"]["artists"].values():
            for alias in definition["aliases"]:
                intent = parse_intent(alias, RULES)
                for word in alias.split():
                    self.assertNotIn(word, intent["unknown_terms"], alias)


class TestArtistChain(unittest.TestCase):
    """Ogni profilo deve consegnare la catena COMPLETA: main + eq + comp + sat + mandate."""

    def test_every_artist_produces_a_full_chain(self):
        for key, definition in RULES["lexicon"]["artists"].items():
            preset = compile_preset(definition["aliases"][0], rules=RULES)
            active = {m["id"] for m in preset["modules"] if m["enabled"]}
            for required in ("trim", "hpf", "eqSub", "ds1", "comp1", "comp2", "eqTone", "ds2", "limiter", "out"):
                self.assertIn(required, active, f"{key}: manca {required}")

    def test_every_artist_has_saturation(self):
        for key, definition in RULES["lexicon"]["artists"].items():
            preset = compile_preset(definition["aliases"][0], rules=RULES)
            active = {m["id"] for m in preset["modules"] if m["enabled"]}
            self.assertIn("sat", active, f"{key}: senza saturazione non c'è carattere")

    def test_forced_sends_are_honoured(self):
        for key, definition in RULES["lexicon"]["artists"].items():
            preset = compile_preset(definition["aliases"][0], rules=RULES)
            for group, wanted in definition["sends"].items():
                chosen = sends_of(preset, group)
                if wanted.endswith("_none"):
                    self.assertEqual(chosen, [], f"{key}/{group} doveva restare vuoto")
                else:
                    self.assertEqual([s["id"] for s in chosen], [wanted], f"{key}/{group}")

    def test_reverb_and_delay_always_declared(self):
        for key, definition in RULES["lexicon"]["artists"].items():
            preset = compile_preset(definition["aliases"][0], rules=RULES)
            self.assertTrue(sends_of(preset, "reverb"), f"{key}: nessun riverbero")
            self.assertTrue(sends_of(preset, "delay"), f"{key}: nessun delay")

    def test_forced_send_says_it_comes_from_the_artist(self):
        preset = compile_preset("sfera ebbasta", rules=RULES)
        for send in preset["sends"]:
            self.assertIn("Scelta dal profilo", send["why"])

    def test_production_notes_cover_tuning_doubles_and_adlib(self):
        for key, definition in RULES["lexicon"]["artists"].items():
            preset = compile_preset(definition["aliases"][0], rules=RULES)
            ids = {note["id"] for note in preset["production_notes"]}
            for required in ("tuning", "doubles", "adlib"):
                self.assertIn(required, ids, f"{key}: manca la nota '{required}'")
            for note in preset["production_notes"]:
                self.assertTrue(note["text"].strip())

    def test_artists_differ_from_each_other(self):
        """Se due profili producessero la stessa catena, uno dei due non serve."""
        signatures = {}
        for key, definition in RULES["lexicon"]["artists"].items():
            preset = compile_preset(definition["aliases"][0], rules=RULES)
            signature = tuple(
                (m["id"], tuple((p, v["value"]) for p, v in m["params"].items()))
                for m in preset["modules"] if m["enabled"]
            )
            self.assertNotIn(signature, signatures,
                             f"{key} identico a {signatures.get(signature)}")
            signatures[signature] = key

    def test_prompt_without_artist_has_no_production_notes(self):
        preset = compile_preset("voce pop brillante", rules=RULES)
        self.assertIsNone(preset["artist"])
        self.assertEqual(preset["production_notes"], [])


if __name__ == "__main__":
    unittest.main()
