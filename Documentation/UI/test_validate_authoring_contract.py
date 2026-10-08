"""Focused source-preflight checks for the authored CSS selector subset."""

import contextlib
import io
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

from validate_authoring_contract import authored_templates, declared_css_classes, main


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
        self.assertIn("contract 79 nodes matched", output.getvalue())

    def test_template_signature_and_use_are_checked(self):
        root = ET.fromstring('<ui version="1"><template name="spell">'
            '<param name="art" type="image-binding"/><param name="key" type="text"/>'
            '<column id="slot"><hud-ability id="image" image-bind-param="art"/>'
            '<key-hint id="label" label-param="key"/></column></template>'
            '<region name="main"><use id="first" template="spell" art-bind="Art" key="1"/>'
            '</region></ui>')
        templates = authored_templates(root, root.find("region"))
        self.assertEqual(templates["spell"]["instances"], ["first"])
        self.assertEqual([node["id"] for node in templates["spell"]["nodes"]],
                         ["slot", "image", "label"])

    def test_template_rejects_duplicate_local_id(self):
        root = ET.fromstring('<ui version="1"><template name="spell">'
            '<column id="slot"><text id="same"/><text id="same"/></column>'
            '</template><region name="main"><use id="first" template="spell"/></region></ui>')
        with self.assertRaisesRegex(ValueError, "duplicate id"):
            authored_templates(root, root.find("region"))

    def test_template_rejects_missing_use_parameter(self):
        root = ET.fromstring('<ui version="1"><template name="spell">'
            '<param name="art" type="image-binding"/><column id="slot"/>'
            '</template><region name="main"><use id="first" template="spell"/></region></ui>')
        with self.assertRaisesRegex(ValueError, "missing or unsupported arguments"):
            authored_templates(root, root.find("region"))

    def test_template_rejects_unknown_parameter_reference(self):
        root = ET.fromstring('<ui version="1"><template name="spell">'
            '<param name="art" type="image-binding"/>'
            '<hud-ability id="image" image-bind-param="missing"/>'
            '</template><region name="main"><use id="first" template="spell" art-bind="Art"/>'
            '</region></ui>')
        with self.assertRaisesRegex(ValueError, "unknown template parameter"):
            authored_templates(root, root.find("region"))

    def test_template_accepts_builtin_text_bind_reference(self):
        root = ET.fromstring('<ui version="1"><template name="caption">'
            '<param name="value" type="text-binding"/><text id="label" bind-param="value"/>'
            '</template><region name="main"><use id="first" template="caption" '
            'value-bind="Caption"/></region></ui>')
        templates = authored_templates(root, root.find("region"))
        self.assertEqual(templates["caption"]["instances"], ["first"])

    def test_template_accepts_builtin_image_bind_reference(self):
        root = ET.fromstring('<ui version="1"><template name="portrait">'
            '<param name="art" type="image-binding"/><image id="picture" bind-param="art"/>'
            '</template><region name="main"><use id="first" template="portrait" '
            'art-bind="Portrait"/></region></ui>')
        templates = authored_templates(root, root.find("region"))
        self.assertEqual(templates["portrait"]["instances"], ["first"])

    def test_template_accepts_visible_reference(self):
        root = ET.fromstring('<ui version="1"><template name="panel">'
            '<param name="shown" type="bool-binding"/>'
            '<column id="body" visible-param="shown"/></template>'
            '<region name="main"><use id="first" template="panel" '
            'shown-bind="IsShown"/></region></ui>')
        templates = authored_templates(root, root.find("region"))
        self.assertEqual(templates["panel"]["instances"], ["first"])

    def test_template_rejects_wrong_builtin_bind_kind(self):
        root = ET.fromstring('<ui version="1"><template name="caption">'
            '<param name="value" type="image-binding"/><text id="label" bind-param="value"/>'
            '</template><region name="main"><use id="first" template="caption" '
            'value-bind="Art"/></region></ui>')
        with self.assertRaisesRegex(ValueError, "type-mismatched"):
            authored_templates(root, root.find("region"))

    def test_template_rejects_class_on_use(self):
        root = ET.fromstring('<ui version="1"><template name="panel">'
            '<column id="body"/></template><region name="main">'
            '<use id="first" template="panel" class="panel"/></region></ui>')
        with self.assertRaisesRegex(ValueError, "missing or unsupported arguments"):
            authored_templates(root, root.find("region"))

    def test_template_rejects_mismatched_nested_use_parameter(self):
        root = ET.fromstring('<ui version="1"><template name="inner">'
            '<param name="art" type="image-binding"/><hud-ability id="image" image-bind-param="art"/>'
            '</template><template name="outer"><param name="value" type="number-binding"/>'
            '<use id="nested" template="inner" art-bind-param="value"/></template>'
            '<region name="main"><use id="first" template="outer" value-bind="Value"/>'
            '</region></ui>')
        with self.assertRaisesRegex(ValueError, "type-mismatched"):
            authored_templates(root, root.find("region"))

    def test_template_rejects_recursive_use(self):
        root = ET.fromstring('<ui version="1"><template name="spell">'
            '<use id="nested" template="spell"/></template>'
            '<region name="main"><use id="first" template="spell"/></region></ui>')
        with self.assertRaisesRegex(ValueError, "recursive template"):
            authored_templates(root, root.find("region"))


if __name__ == "__main__":
    unittest.main()
