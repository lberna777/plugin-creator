"""Le regole devono essere valutabili: un'espressione rotta non deve diventare uno zero silenzioso."""
import itertools
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from chain_compiler import (ExprError, build_context, compile_preset, evaluate,  # noqa: E402
                            load_expression_cases, load_rules, parse_intent)

RULES = load_rules()


def all_expressions():
    """Ogni espressione presente in rules.json, con il posto da cui viene."""
    for module in RULES["chain"]:
        yield f'chain/{module["id"]}/enabled', module["enabled"]
        for pid, spec in module["params"].items():
            if "expr" in spec:
                yield f'chain/{module["id"]}/{pid}', spec["expr"]
            for choice in spec.get("choices", []):
                if "when" in choice:
                    yield f'chain/{module["id"]}/{pid}/when', choice["when"]
    for send in RULES["sends"]:
        yield f'sends/{send["id"]}/enabled', send["enabled"]
        for pid, spec in send.get("settings", {}).items():
            if "expr" in spec:
                yield f'sends/{send["id"]}/{pid}', spec["expr"]


def contexts():
    """Una griglia di casi estremi: gli assi ai loro limiti, i due profili di stanza."""
    axes = RULES["axis_ranges"]
    for profile in ("untreated_room_focusrite_scarlett", "treated_room", "neutral"):
        for corner in itertools.product(*[[lo, hi] for lo, hi in list(axes.values())[:4]]):
            intent = parse_intent("", RULES)
            for axis, value in zip(list(axes.keys())[:4], corner):
                intent["axes"][axis] = value
            ctx, _ = build_context(intent, RULES["source_profiles"][profile], RULES)
            yield profile, ctx


class TestEveryExpressionEvaluates(unittest.TestCase):
    def test_no_expression_fails_in_any_corner(self):
        for where, expression in all_expressions():
            for profile, ctx in contexts():
                try:
                    value = evaluate(expression, ctx)
                except ExprError as error:
                    self.fail(f"{where} ({profile}): {error}")
                self.assertEqual(value, value, f"{where}: NaN")            # NaN != NaN
                self.assertNotIn(value, (float("inf"), float("-inf")), where)

    def test_no_preset_carries_an_expression_warning(self):
        for prompt in ("", "voce tipo sfera ebbasta", "podcast pulito", "metal urlato molto aggressivo"):
            for profile in RULES["source_profiles"]:
                preset = compile_preset(prompt, profile, RULES)
                for warning in preset["warnings"]:
                    self.assertNotIn("non valutabile", warning, f"{prompt}/{profile}: {warning}")


class TestExpressionSemantics(unittest.TestCase):
    """Le fixture sono il contratto fra il compilatore Python e il motore C++."""

    def test_fixtures_cover_the_precedence_traps(self):
        expressions, _ = load_expression_cases()
        joined = " ".join(expressions)
        for trap in ("not ", " and ", " or ", "/", "-"):
            self.assertIn(trap, joined)

    def test_not_binds_looser_than_comparison(self):
        # `not a > b` in Python e' `not (a > b)`: se il C++ lo legge come `(not a) > b` la parita' cade
        self.assertEqual(evaluate("not brightness > 0.5", {"brightness": 0.45}), True)
        self.assertEqual(evaluate("not brightness > 0.5", {"brightness": 0.55}), False)

    def test_division_by_zero_is_declared_not_silent(self):
        with self.assertRaises(ExprError):
            evaluate("1 / (density - density)", dict(load_expression_cases()[1]))

    def test_unknown_variable_is_declared_not_zero(self):
        with self.assertRaises(ExprError):
            evaluate("nonexistent + 1", dict(load_expression_cases()[1]))


if __name__ == "__main__":
    unittest.main()
