import json
import os
import unittest

import helpers
from generator_helpers import SAMPLES, TEST_GENERATORS
from plynth import generating
from plynth.generating import addons

COLUMN = {
    "format": "plynth-generator", "version": 1, "name": "Column",
    "description": "A round or square column from the floor up.",
    "addons": [],
    "size": {"width": {"param": "diameter"}, "depth": {"param": "diameter"}, "height": {"param": "height"}},
    "params": [
        {"id": "diameter", "label": "Diameter", "type": "number", "unit": "m", "default": 0.3, "min": 0.05, "max": 2.0},
        {"id": "height", "label": "Height", "type": "number", "unit": "m", "default": "ceiling_height", "min": 0.1,
         "max": 20},
        {"id": "shape", "label": "Shape", "type": "choice", "default": "round",
         "choices": [{"id": "round", "label": "Round"}, {"id": "square", "label": "Square"}]},
        {"id": "sides", "label": "Sides", "type": "integer", "default": 24, "min": 3, "max": 64},
        {"id": "plinth", "label": "Plinth and cap", "type": "boolean", "default": True},
        {"id": "material", "label": "Material", "type": "material", "default": "white_paint"}],
}


class DeclarationTests(unittest.TestCase):
    # The column sample declares what the plan says.
    def test_column(self):
        self.assertEqual(generating.read_declaration(os.path.join(SAMPLES, "column.py")), COLUMN)

    # The railing sample declares its parameters and a fixed depth.
    def test_railing(self):
        declaration = generating.read_declaration(os.path.join(SAMPLES, "railing.py"))
        self.assertEqual([p["id"] for p in declaration["params"]], ["length", "height", "spacing", "material"])
        self.assertEqual(declaration["size"]["depth"], 0.05)

    # Both samples match the generator schema.
    def test_schema(self):
        from schema_check import load_schema, validate
        for stem in ("column", "railing"):
            declaration = generating.read_declaration(os.path.join(SAMPLES, stem + ".py"))
            self.assertEqual(validate(declaration, load_schema("generator")), [], stem)

    # A file without a declaration is refused.
    def test_refused(self):
        with self.assertRaises(generating.GeneratorError):
            generating.read_declaration(os.path.join(TEST_GENERATORS, "no_declaration.py"))

    # A file with a declaration but no generate function is refused when loaded.
    def test_load_needs_generate(self):
        path = os.path.join(TEST_GENERATORS, "no_function.py")
        with self.assertRaises(generating.GeneratorError):
            generating.load_generator(path, "no_function")

    # Comments and blank lines before the docstring are allowed.
    def test_comments_before_docstring(self):
        import tempfile
        text = "# comment\n\n" + open(os.path.join(SAMPLES, "railing.py"), encoding="utf-8").read()
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "railing.py")
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(text)
            self.assertEqual(generating.read_declaration(path)["name"], "Railing")


class AddonTests(unittest.TestCase):
    # An add-on nobody has installed cannot be found or enabled.
    def test_missing(self):
        self.assertIsNone(addons.find_module("plynth_test_missing"))
        self.assertFalse(addons.enable("plynth_test_missing"))


if __name__ == "__main__":
    unittest.main()
