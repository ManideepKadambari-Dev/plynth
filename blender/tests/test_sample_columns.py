import math
import os
import unittest

import bpy
import numpy as np

from generator_helpers import SAMPLES, GeneratorTest, job_item
from plynth import generating, meshes
from plynth.quality import collision as collision_check
from plynth.quality import uvs as uv_check
from plynth.quality._common import polygon_areas
from schema_check import load_schema, validate

COLUMNS = ("column_round", "column_square", "column_tapered")
CEILING = 3.0
TILE = (2.0, 1.0)
SHAFT = 0.8
LOWER_TIER = 0.6

# Edge values for every column: the smallest one with blocks taller than it allows, the largest plain one with
# the neutral look, and one with only a short base.
EDGES = [
    {},
    {"diameter": 0.05, "width": 0.05, "height": 0.3, "segments": 8, "taper": 0.3, "base": True, "base_height": 1.0,
     "capital": True, "capital_height": 1.0},
    {"diameter": 2.0, "width": 2.0, "height": 20.0, "segments": 64, "taper": 1.0, "base": False, "capital": False,
     "material": None},
    {"height": 2.4, "segments": 9, "taper": 0.5, "base": True, "base_height": 0.02, "capital": False},
]


# Returns the path of a sample column file.
def sample_path(generator_id):
    return os.path.join(SAMPLES, generator_id + ".py")


# Returns the default values of a declaration with the ceiling height filled in, changed by the overrides it has.
def values_of(declaration, overrides):
    declared = {p["id"]: p for p in declaration["params"]}
    values = {}
    for param_id, param in declared.items():
        value = param["default"]
        values[param_id] = CEILING if value == "ceiling_height" else value
    for key, value in overrides.items():
        if key in declared:
            values[key] = value
    return values


# Returns the size a declaration gives for these values, as width, depth and height.
def size_of(declaration, values):
    result = []
    for side in ("width", "depth", "height"):
        rule = declaration["size"][side]
        if isinstance(rule, dict):
            result.append(values[rule["param"]] * rule.get("times", 1) + rule.get("plus", 0))
        else:
            result.append(rule)
    return result


# Returns the base and capital heights the column uses, smaller together when they take over half its height.
def blocks(values):
    base = values["base_height"] if values["base"] else 0.0
    capital = values["capital_height"] if values["capital"] else 0.0
    if base + capital > values["height"] * 0.5:
        scale = values["height"] * 0.5 / (base + capital)
        base, capital = base * scale, capital * scale
    return base, capital


# Returns the heights of the rings of the column from bottom to top.
def ring_heights(values):
    base, capital = blocks(values)
    height = values["height"]
    levels = [0.0]
    if base:
        levels += [base * LOWER_TIER, base]
    levels.append(height - capital)
    if capital:
        levels += [height - capital * LOWER_TIER, height]
    return levels


# Returns how many sides the cross section of the column has.
def sides_of(generator_id, values):
    return 4 if generator_id == "column_square" else values["segments"]


# Returns the lowest and highest local corner of an object's mesh.
def local_bounds(obj):
    co = np.array([tuple(v.co) for v in obj.data.vertices])
    return co.min(axis=0), co.max(axis=0)


class DeclarationTests(unittest.TestCase):
    # Every column sample matches the generator schema and keeps one material parameter.
    def test_schema(self):
        for generator_id in COLUMNS:
            with self.subTest(generator=generator_id):
                declaration = generating.read_declaration(sample_path(generator_id))
                self.assertEqual(validate(declaration, load_schema("generator")), [])
                self.assertEqual(declaration["addons"], [])
                materials = [p["id"] for p in declaration["params"] if p["type"] == "material"]
                self.assertEqual(materials, ["material"])

    # The default column is a slim one as tall as the ceiling.
    def test_default_size(self):
        for generator_id in COLUMNS:
            with self.subTest(generator=generator_id):
                declaration = generating.read_declaration(sample_path(generator_id))
                width, depth, height = size_of(declaration, values_of(declaration, {}))
                self.assertEqual(width, depth)
                self.assertTrue(0.2 <= width <= 0.5, width)
                self.assertEqual(height, CEILING)

    # The sample list names every column, so library add-sample-generators copies files the guide describes.
    def test_listed(self):
        with open(os.path.join(SAMPLES, "README.md"), "r", encoding="utf-8") as handle:
            text = handle.read()
        for generator_id in COLUMNS:
            self.assertIn("`%s.py`" % generator_id, text)


class ColumnBuildTests(GeneratorTest):
    # Builds one item of the generator for each edge case and returns the items with their values.
    def build_columns(self, generator_id):
        declaration = generating.read_declaration(sample_path(generator_id))
        items = []
        cases = []
        for number, overrides in enumerate(EDGES):
            values = values_of(declaration, overrides)
            width, depth, height = size_of(declaration, values)
            item = job_item("i%d" % (number + 1), generator_id, values, [0.8 + 0.8 * number, -2.0, 0.0],
                            size={"width": width, "depth": depth, "height": height})
            items.append(item)
            cases.append((item, values))
        params = self.room_params({generator_id: sample_path(generator_id)}, items)
        params["materials"] = [self.make_material("white_paint", TILE)]
        result = self.build_open(params)
        for code in ("generator_failed", "generator_size", "generator_unavailable", "collision_failed"):
            self.assertEqual(self.problems(result, code), [], code)
        self.assertEqual(result["output"]["counts"]["generators"], len(items))
        return cases

    # Checks every built column of the generator: closed, the size of its values, one material slot, usable UVs
    # and a closed convex collision hull inside it.
    def check_columns(self, generator_id):
        for item, values in self.build_columns(generator_id):
            with self.subTest(generator=generator_id, item=item["id"]):
                obj = bpy.data.objects["%s_%s" % (generator_id, item["id"])]
                self.assertTrue(meshes.is_manifold(obj))
                self.assertGreater(meshes.volume(obj), 0.0)
                self.check_bounds(generator_id, obj, item, values)
                self.assertEqual([m.name for m in obj.data.materials], [item["slots"]["material"]])
                self.check_uvs(obj)
                self.check_collision(obj, item)

    # The mesh spans the item's size, every ring of the section is made and the rings sit at the block heights.
    def check_bounds(self, generator_id, obj, item, values):
        low, high = local_bounds(obj)
        sides = sides_of(generator_id, values)
        size = item["size"]
        slack = 1e-5 if sides == 4 else size["width"] * (1.0 - math.cos(math.pi / sides)) + 1e-5
        self.assertAlmostEqual(high[0] - low[0], size["width"], delta=slack)
        self.assertAlmostEqual(high[1] - low[1], size["depth"], delta=slack)
        self.assertLessEqual(high[0], size["width"] / 2.0 + 1e-5)
        self.assertLessEqual(high[1], size["depth"] / 2.0 + 1e-5)
        self.assertAlmostEqual(low[2], 0.0, delta=1e-6)
        self.assertAlmostEqual(high[2], size["height"], delta=1e-5)
        levels = ring_heights(values)
        found = sorted({round(v.co.z, 5) for v in obj.data.vertices})
        self.assertEqual(found, sorted({round(z, 5) for z in levels}))
        base, capital = blocks(values)
        rings = 2 + 4 * len([block for block in (base, capital) if block > 0.0])
        self.assertEqual(len(obj.data.vertices), rings * sides)
        if generator_id == "column_tapered" and not capital:
            foot = size["width"] / 2.0 * (SHAFT if base else 1.0)
            top = max(math.hypot(v.co.x, v.co.y) for v in obj.data.vertices if v.co.z > values["height"] - 1e-6)
            self.assertAlmostEqual(top, foot * values["taper"], delta=1e-5)

    # Every face has UVs with room on them, and the sides wrap once around in metres over the tile width.
    def check_uvs(self, obj):
        self.assertIn("UVMap", obj.data.uv_layers)
        uv_area, area = polygon_areas(obj.data)
        empty = (uv_area < uv_check.EMPTY_UV_AREA) & (area > uv_check.MIN_FACE_AREA)
        self.assertEqual(int(np.count_nonzero(empty)), 0)
        if obj.data.materials[0].name.endswith("_Neutral"):
            return
        layer = obj.data.uv_layers["UVMap"]
        bottom = [p for p in obj.data.polygons if abs(p.normal.z) < 0.1 and
                  min(obj.data.vertices[i].co.z for i in p.vertices) < 1e-6]
        perimeter = sum((obj.data.vertices[p.vertices[1]].co - obj.data.vertices[p.vertices[0]].co).length
                        for p in bottom)
        reach = max(layer.uv[i].vector.x for p in bottom for i in p.loop_indices)
        self.assertAlmostEqual(reach, perimeter / TILE[0], delta=1e-4)

    # The build gives the column one closed convex hull that stays inside it.
    def check_collision(self, obj, item):
        hull = bpy.data.objects.get(obj.name + "_Collision_00")
        self.assertIsNotNone(hull)
        self.assertEqual(hull["plynth_kind"], "collision")
        self.assertEqual(hull["plynth_collision_for"], obj.name)
        self.assertIsNone(bpy.data.objects.get(obj.name + "_Collision_01"))
        self.assertTrue(meshes.is_manifold(hull))
        self.assertTrue(collision_check.is_convex(hull.data))
        self.assertGreater(meshes.volume(hull), 0.0)
        low, high = local_bounds(hull)
        column_low, column_high = local_bounds(obj)
        for axis in range(3):
            self.assertGreaterEqual(low[axis], column_low[axis] - 1e-3)
            self.assertLessEqual(high[axis], column_high[axis] + 1e-3)
        self.assertGreater(high[2] - low[2], item["size"]["height"] * 0.9)

    # The round column with defaults and edge values.
    def test_round(self):
        self.check_columns("column_round")

    # The square column with defaults and edge values.
    def test_square(self):
        self.check_columns("column_square")

    # The tapered column with defaults and edge values.
    def test_tapered(self):
        self.check_columns("column_tapered")


if __name__ == "__main__":
    unittest.main()
