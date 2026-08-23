"""Test del parsing testo → intent. Ogni termine nuovo del vocabolario aggiunge un caso qui."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from chain_compiler import load_rules, parse_intent, normalize  # noqa: E402

RULES = load_rules()


def axes(prompt):
    return parse_intent(prompt, RULES)["axes"]


class TestNormalize(unittest.TestCase):
    def test_accents_and_punctuation(self):
        self.assertEqual(normalize("Voce Calda, però!").split(), ["voce", "calda", "pero"])


class TestGenres(unittest.TestCase):
    def test_genre_sets_axes_and_loudness(self):
        intent = parse_intent("trap", RULES)
        self.assertEqual(intent["genre"], "trap")
        self.assertEqual(intent["loudness_target_lufs"], -9)
        self.assertGreater(intent["axes"]["aggression"], 0.4)

    def test_genre_can_set_delivery(self):
        self.assertEqual(parse_intent("podcast", RULES)["delivery"], "spoken")

    def test_multiword_alias(self):
        self.assertEqual(parse_intent("hip hop italiano", RULES)["genre"], "rap")

    def test_every_genre_alias_resolves(self):
        for key, definition in RULES["lexicon"]["genre"].items():
            for alias in definition["aliases"]:
                self.assertEqual(parse_intent(alias, RULES)["genre"], key, alias)


class TestGenreIsAppliedOnce(unittest.TestCase):
    def test_two_aliases_of_the_same_genre_count_once(self):
        one = parse_intent("cantautore", RULES)["axes"]
        two = parse_intent("cantautore acustico", RULES)["axes"]
        self.assertEqual(one, two)

    def test_a_second_genre_does_not_stack_its_axes(self):
        self.assertEqual(parse_intent("trap", RULES)["axes"], parse_intent("trap pop", RULES)["axes"])


class TestDescriptors(unittest.TestCase):
    def test_bright_and_dark_oppose(self):
        self.assertGreater(axes("brillante")["brightness"], 0)
        self.assertLess(axes("scura")["brightness"], 0)

    def test_negation_flips_the_term(self):
        self.assertLess(axes("non brillante")["brightness"], 0)
        self.assertGreater(axes("meno scura")["brightness"], 0)

    def test_negation_flips_every_axis_of_the_term(self):
        neutral = axes("")
        negated = axes("non stridula")
        self.assertLess(negated["brightness"], neutral["brightness"])
        self.assertGreater(negated["sibilance_control"], neutral["sibilance_control"])

    def test_intensifier_and_diminisher(self):
        base = axes("aggressiva")["aggression"]
        self.assertGreater(axes("molto aggressiva")["aggression"], base)
        self.assertLess(axes("poco aggressiva")["aggression"], base)

    def test_negation_window_does_not_leak(self):
        far = axes("non voglio proprio davvero affatto brillante")  # oltre la finestra di 3 parole
        self.assertGreater(far["brightness"], 0)

    def test_every_descriptor_alias_moves_something(self):
        neutral = axes("")
        for key, definition in RULES["lexicon"]["descriptors"].items():
            for alias in definition["aliases"]:
                self.assertNotEqual(axes(alias), neutral, f"{key}/{alias} non muove nessun asse")


class TestAxes(unittest.TestCase):
    def test_axes_stay_in_range(self):
        prompt = "trap metal edm molto aggressiva molto compressa molto sporca molto brillante"
        for axis, value in axes(prompt).items():
            lo, hi = RULES["axis_ranges"][axis]
            self.assertGreaterEqual(value, lo, axis)
            self.assertLessEqual(value, hi, axis)

    def test_empty_prompt_is_the_declared_neutral(self):
        self.assertEqual(axes(""), {k: float(v) for k, v in RULES["intent_defaults"]["axes"].items()})


class TestUnknownTerms(unittest.TestCase):
    def test_unknown_terms_are_declared_not_invented(self):
        intent = parse_intent("voce come quella di mio cugino", RULES)
        self.assertIn("cugino", intent["unknown_terms"])
        self.assertEqual(intent["axes"], {k: float(v) for k, v in RULES["intent_defaults"]["axes"].items()})


class TestDeterminism(unittest.TestCase):
    def test_same_prompt_same_intent(self):
        prompt = "r&b intimo, voce calda ma non scura, poco compressa"
        first = parse_intent(prompt, RULES)
        for _ in range(50):
            self.assertEqual(parse_intent(prompt, RULES), first)


if __name__ == "__main__":
    unittest.main()
