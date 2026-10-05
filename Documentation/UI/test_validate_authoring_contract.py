"""Focused source-preflight checks for the authored CSS selector subset."""

import contextlib
import io
from pathlib import Path
import unittest

from validate_authoring_contract import declared_css_classes, main


class CssSelectorPreflightTests(unittest.TestCase):
    def test_single_class_rules_and_comments(self):
        self.assertEqual(
            declared_css_classes("/* comment */ .first { color: red; }\n.second{}"),
            {"first", "second"},
        )

    def test_rejects_id_before_valid_class(self):
        with self.assertRaisesRegex(ValueError, "single \\.class"):
            declared_css_classes("#id .valid { color: red; }")

    def test_rejects_compound_selector(self):
        with self.assertRaisesRegex(ValueError, "single \\.class"):
            declared_css_classes(".first.second { color: red; }")

    def test_rejects_multiple_selectors(self):
        with self.assertRaisesRegex(ValueError, "single \\.class"):
            declared_css_classes(".first, .second { color: red; }")

    def test_rejects_unsupported_later_rule(self):
        with self.assertRaisesRegex(ValueError, "single \\.class"):
            declared_css_classes(".valid {} #id { color: red; }")

    def test_rejects_unterminated_rule(self):
        with self.assertRaisesRegex(ValueError, "unterminated CSS rule"):
            declared_css_classes(".valid { color: red;")

    def test_dark_fantasy_contract(self):
        root = Path(__file__).resolve().parents[2]
        fixture = root / "Resources" / "UiAuthoring" / "DarkFantasyHudProof"
        contract = Path(__file__).parent / "DarkFantasyHudProof" / "design-contract.json"
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            result = main([
                str(fixture / "DarkFantasyHudProof.ui.html"),
                str(fixture / "DarkFantasyHudProof.ui.css"),
                "--contract", str(contract),
            ])
        self.assertEqual(result, 0, output.getvalue())
        self.assertIn("contract 100 nodes matched", output.getvalue())


if __name__ == "__main__":
    unittest.main()
