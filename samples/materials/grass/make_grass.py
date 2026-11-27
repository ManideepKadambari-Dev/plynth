r"""Draws the grass sample material of Plynth into this folder: a lawn seen from above, a tile 2 m square.

Run it with Blender in the background, with the five user variables pointed at a private folder first, as
samples/materials/README.md says:
    blender --background --factory-startup --python samples/materials/grass/make_grass.py

Everything is made from numbers: tileable noise and short blade strokes from a fixed seed, so the output is the
same every time. Nothing is downloaded and nothing comes from a store. It uses the drawing helpers of
make_sample_materials.py in the folder above.
"""
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import make_sample_materials as samples  # noqa: E402

SIZE = 1024
TILE = 2.0
BLADES = 60000
FILES = ("grass_basecolor.jpg", "grass_normal.png", "grass_roughness.png", "material.json")
INFO = {
    "format": "plynth-material-source",
    "version": 1,
    "id": "grass",
    "name": "Grass",
    "groups": ["exterior", "floor"],
    "tags": ["grass", "green", "lawn"],
    "tile_size": {"width": TILE, "height": TILE},
    "license": "cc0",
}


# Returns a height map of grass blades seen from above, 0 to 1, that repeats without a seam: each blade is a
# short stroke two pixels wide from its root to its tip, higher towards the tip, and the highest blade wins.
def blade_heights(rng, size, count, shortest, longest):
    height = np.zeros((size, size))
    x = rng.rand(count) * size
    y = rng.rand(count) * size
    angle = rng.rand(count) * 2.0 * np.pi
    length = shortest + rng.rand(count) * (longest - shortest)
    stand = 0.45 + 0.55 * rng.rand(count)
    steps = int(np.ceil(longest)) + 1
    for step in range(steps):
        t = step / (steps - 1.0)
        column = np.floor(x + np.cos(angle) * length * t).astype(int) % size
        row = np.floor(y + np.sin(angle) * length * t).astype(int) % size
        value = stand * (0.3 + 0.7 * t)
        np.maximum.at(height, (row, column), value)
        np.maximum.at(height, (row, (column + 1) % size), value * 0.85)
    return height


# Draws the base color, normal and roughness maps of the lawn into the folder.
def draw(folder):
    rng = np.random.RandomState(97)
    blades = blade_heights(rng, SIZE, BLADES, 8.0, 26.0)
    cover = samples.smoothstep(0.0, 0.2, blades)
    lit = np.clip(samples.blur(blades, 1) * 0.5 + blades * 0.5, 0.0, 1.0)
    patches = samples.fbm(rng, SIZE, 4, 4, 4)
    dry = samples.smoothstep(0.60, 0.78, samples.fbm(rng, SIZE, 6, 6, 3))
    tint = samples.fbm(rng, SIZE, 128, 128, 2)
    dark = np.array([0.05, 0.12, 0.03])
    light = np.array([0.28, 0.45, 0.11])
    straw = np.array([0.46, 0.42, 0.20])
    soil = np.array([0.11, 0.08, 0.05])
    shade = np.clip(0.75 * lit + 0.25 * patches, 0.0, 1.0)[:, :, None]
    color = dark[None, None, :] * (1.0 - shade) + light[None, None, :] * shade
    withered = (0.65 * dry * lit)[:, :, None]
    color = color * (1.0 - withered) + straw[None, None, :] * withered
    color *= (0.88 + 0.24 * tint)[:, :, None]
    color = soil[None, None, :] * (1.0 - cover[:, :, None]) + color * cover[:, :, None]
    roughness = np.clip(0.82 + 0.10 * (1.0 - lit) + 0.05 * dry, 0.0, 1.0)
    normal = samples.normal_from_height(samples.blur(blades, 1), 4.0)
    samples.save_jpeg(os.path.join(folder, FILES[0]), np.clip(color, 0.0, 1.0))
    samples.save_rgb(os.path.join(folder, FILES[1]), normal)
    samples.save_grey(os.path.join(folder, FILES[2]), roughness)


# Draws the grass again: removes its old files, draws the maps and writes material.json.
def main():
    for name in FILES:
        path = os.path.join(HERE, name)
        if os.path.isfile(path):
            os.remove(path)
    draw(HERE)
    with open(os.path.join(HERE, "material.json"), "w", encoding="utf-8", newline="\n") as handle:
        json.dump(INFO, handle, indent=2)
        handle.write("\n")
    print("drew grass")


if __name__ == "__main__":
    main()
