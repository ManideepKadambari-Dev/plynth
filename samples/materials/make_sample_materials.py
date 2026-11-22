"""Draws the seven sample materials of Plynth into the folders next to this file.

Run it with Blender in the background:
    blender --background --factory-startup --python samples/materials/make_sample_materials.py

Everything is made from numbers: tileable noise and simple shapes. The output is the same every time. Nothing
is downloaded and nothing comes from a store. The file names mix styles on purpose, so the name matching of
the importer is tested by the samples.
"""
import json
import os
import shutil
import sys

import bpy
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "blender"))

from plynth.materials import write_png  # noqa: E402


# Returns a smooth 0 to 1 step between two edges.
def smoothstep(low, high, value):
    t = np.clip((value - low) / (high - low), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


# Returns tileable value noise of height x width pixels from a lattice of cells_y x cells_x random values.
def value_noise(rng, height, width, cells_y, cells_x):
    lattice = rng.rand(cells_y, cells_x)
    y = np.arange(height) * cells_y / height
    x = np.arange(width) * cells_x / width
    y0 = np.floor(y).astype(int)
    x0 = np.floor(x).astype(int)
    fy = smoothstep(0, 1, y - y0)[:, None]
    fx = smoothstep(0, 1, x - x0)[None, :]
    y1 = (y0 + 1) % cells_y
    x1 = (x0 + 1) % cells_x
    y0 %= cells_y
    x0 %= cells_x
    top = lattice[y0][:, x0] * (1 - fx) + lattice[y0][:, x1] * fx
    bottom = lattice[y1][:, x0] * (1 - fx) + lattice[y1][:, x1] * fx
    return top * (1 - fy) + bottom * fy


# Returns tileable noise from several octaves, 0 to 1.
def fbm(rng, size, cells_y, cells_x=None, octaves=4):
    cells_x = cells_x or cells_y
    total = np.zeros((size, size))
    weight = 1.0
    norm = 0.0
    for octave in range(octaves):
        factor = 2 ** octave
        total += weight * value_noise(rng, size, size, cells_y * factor, cells_x * factor)
        norm += weight
        weight *= 0.5
    return total / norm


# Returns a tileable blur: the average of the array and its shifted copies.
def blur(plane, radius):
    result = np.zeros_like(plane)
    count = 0
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            result += np.roll(np.roll(plane, dy, axis=0), dx, axis=1)
            count += 1
    return result / count


# Returns an OpenGL normal map (rows top first, floats 0 to 1) from a height map where the strength is the
# slope that tilts the normal 45 degrees.
def normal_from_height(height, strength):
    slope_x = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5 * strength
    slope_up = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5 * strength
    normal = np.stack([-slope_x, slope_up, np.ones_like(height)], axis=2)
    normal /= np.linalg.norm(normal, axis=2, keepdims=True)
    return normal * 0.5 + 0.5


# Turns floats 0 to 1 into bytes.
def to_bytes(values):
    return np.clip(np.rint(np.asarray(values) * 255.0), 0, 255).astype(np.uint8)


# Writes an RGB PNG from floats 0 to 1.
def save_rgb(path, rgb):
    write_png(path, to_bytes(rgb))


# Writes a one channel grey PNG from a plane of floats 0 to 1.
def save_grey(path, plane):
    write_png(path, to_bytes(plane)[:, :, None])


# Writes a JPEG from floats 0 to 1, with the values as they are (no colour management).
def save_jpeg(path, rgb):
    height, width, _ = rgb.shape
    pixels = np.ones((height, width, 4), dtype=np.float32)
    pixels[:, :, :3] = np.round(rgb * 255.0) / 255.0
    image = bpy.data.images.new("sample", width, height, alpha=False)
    image.colorspace_settings.name = "Non-Color"
    image.pixels.foreach_set(np.flipud(pixels).ravel())
    image.filepath_raw = path
    image.file_format = "JPEG"
    image.save()
    bpy.data.images.remove(image)


# Mixes two colours by a mask plane.
def mix(color_a, color_b, mask):
    return np.array(color_a)[None, None, :] * (1 - mask[:, :, None]) + np.array(color_b)[None, None, :] * mask[:, :, None]


# Brick wall: a tile 1.2 m wide and 0.6 m high with five bricks across and eight courses.
def red_brick(folder):
    size = 512
    rng = np.random.RandomState(11)
    x = (np.arange(size) + 0.5) / size * 1.2
    y = (np.arange(size) + 0.5) / size * 0.6
    brick_length, course = 0.24, 0.075
    row = np.floor(y / course).astype(int)
    shifted = x[None, :] + (row[:, None] % 2) * brick_length / 2.0
    column = np.floor(shifted / brick_length).astype(int) % 5
    local_x = shifted % brick_length
    local_y = (y % course)[:, None] * np.ones((1, size))
    edge = np.minimum(np.minimum(local_x, brick_length - local_x), np.minimum(local_y, course - local_y))
    brick = smoothstep(0.0035, 0.0085, edge)
    tone = rng.rand(8, 5)
    per_brick = tone[row[:, None] % 8, column]
    grain = fbm(rng, size, 24, 24, 4)
    pits = fbm(rng, size, 64, 64, 2)
    color_brick = np.stack([0.50 + 0.14 * per_brick + 0.10 * grain, 0.17 + 0.07 * per_brick + 0.05 * grain,
                            0.11 + 0.05 * per_brick + 0.04 * grain], axis=2)
    mortar_noise = fbm(rng, size, 40, 40, 3)
    color_mortar = np.stack([0.60 + 0.12 * mortar_noise] * 3, axis=2) * np.array([1.0, 0.98, 0.94])
    color = color_mortar * (1 - brick[:, :, None]) + color_brick * brick[:, :, None]
    height = brick * (0.75 + 0.25 * pits) + 0.05 * grain
    roughness = np.where(brick > 0.5, 0.80 + 0.12 * pits, 0.93)
    save_jpeg(os.path.join(folder, "brick_color.jpg"), color)
    save_rgb(os.path.join(folder, "brick_nor_gl.png"), normal_from_height(blur(height, 1), 9.0))
    save_grey(os.path.join(folder, "brick_rough.png"), roughness)


# Oak floor boards: a tile 2 m square with ten boards 0.2 m wide, each with its own joint across the length.
def oak_planks(folder):
    size = 1024
    rng = np.random.RandomState(23)
    x = (np.arange(size) + 0.5) / size * 2.0
    y = (np.arange(size) + 0.5) / size * 2.0
    board = np.floor(x / 0.2).astype(int)
    across = (x % 0.2)
    joints = rng.rand(10) * 2.0
    piece = ((y[:, None] - joints[board][None, :]) % 2.0 < 1.0).astype(int)
    seed_tint = rng.rand(10, 2)
    tint = seed_tint[board[None, :] * np.ones((size, 1), dtype=int), piece]
    streak = fbm(rng, size, 3, 90, 4)
    warp = fbm(rng, size, 2, 30, 3)
    rings = 0.5 + 0.5 * np.sin(2 * np.pi * (across[None, :] / 0.2 * 5.0 + 3.0 * warp + 6.0 * streak))
    fine = fbm(rng, size, 6, 200, 2)
    tone = 0.35 * rings + 0.35 * streak + 0.30 * fine
    dark = np.array([0.42, 0.27, 0.14])
    light = np.array([0.72, 0.53, 0.32])
    color = dark[None, None, :] * (1 - tone[:, :, None]) + light[None, None, :] * tone[:, :, None]
    color *= (0.88 + 0.22 * tint)[:, :, None]
    gap_x = np.minimum(across, 0.2 - across)[None, :] * np.ones((size, 1))
    joint_distance = np.minimum((y[:, None] - joints[board][None, :]) % 1.0, (joints[board][None, :] - y[:, None]) % 1.0)
    gap = 1.0 - smoothstep(0.0015, 0.0035, np.minimum(gap_x, joint_distance))
    color = color * (1 - 0.65 * gap[:, :, None])
    height = 0.12 * tone + 0.05 * fine - 0.9 * gap
    roughness = 0.52 + 0.14 * fine + 0.18 * gap
    normal = normal_from_height(blur(height, 1), 7.0)
    normal[:, :, 1] = 1.0 - normal[:, :, 1]
    save_jpeg(os.path.join(folder, "oak_Color.jpg"), color)
    save_rgb(os.path.join(folder, "NormalDX.png"), normal)
    save_grey(os.path.join(folder, "oak_Roughness.png"), roughness)


# Grey ceramic tile: a tile 0.6 m square holding four tiles 0.3 m wide with grout between them.
def grey_tile(folder):
    size = 512
    rng = np.random.RandomState(37)
    coord = (np.arange(size) + 0.5) / size * 0.6
    local = coord % 0.3
    edge_1d = np.minimum(local, 0.3 - local)
    edge = np.minimum(edge_1d[:, None], edge_1d[None, :])
    face = smoothstep(0.0025, 0.0055, edge)
    tone = rng.rand(2, 2)
    which = tone[(np.floor(coord / 0.3).astype(int))[:, None] % 2, (np.floor(coord / 0.3).astype(int))[None, :] % 2]
    speckle = fbm(rng, size, 48, 48, 3)
    glaze = 0.46 + 0.06 * which + 0.05 * speckle
    color = np.stack([glaze, glaze * 1.01, glaze * 1.03], axis=2)
    grout_color = np.stack([0.34 + 0.05 * speckle] * 3, axis=2)
    color = grout_color * (1 - face[:, :, None]) + color * face[:, :, None]
    height = face * (0.8 + 0.2 * speckle)
    roughness = np.where(face > 0.5, 0.30 + 0.12 * speckle, 0.88)
    occlusion = 0.55 + 0.45 * np.clip(blur(face, 3) * 1.25, 0.0, 1.0)
    save_rgb(os.path.join(folder, "grey_tile_BaseColor.png"), color)
    save_rgb(os.path.join(folder, "grey_tile_Normal.png"), normal_from_height(blur(height, 1), 6.0))
    save_grey(os.path.join(folder, "grey_tile_Roughness.png"), roughness)
    save_grey(os.path.join(folder, "grey_tile_AmbientOcclusion.png"), occlusion)


# Painted boards: a tile 1 m square with four boards of green paint over faint grain.
def painted_wood(folder):
    size = 512
    rng = np.random.RandomState(41)
    x = (np.arange(size) + 0.5) / size
    board = np.floor(x / 0.25).astype(int)
    across = np.minimum(x % 0.25, 0.25 - x % 0.25)[None, :] * np.ones((size, 1))
    seam = 1.0 - smoothstep(0.002, 0.005, across)
    grain = fbm(rng, size, 2, 80, 4)
    brush = fbm(rng, size, 3, 120, 3)
    tint = rng.rand(4)[board][None, :]
    paint = np.array([0.17, 0.33, 0.27])
    color = paint[None, None, :] * (0.92 + 0.10 * brush[:, :, None] + 0.06 * tint[:, :, None])
    color *= (1 - 0.45 * seam[:, :, None])
    height = 0.06 * grain + 0.04 * brush - 0.9 * seam
    roughness = 0.38 + 0.10 * brush + 0.2 * seam
    save_rgb(os.path.join(folder, "wood_painted_albedo.png"), np.clip(color, 0, 1))
    save_rgb(os.path.join(folder, "wood_painted_normal.png"), normal_from_height(blur(height, 1), 6.0))
    save_grey(os.path.join(folder, "wood_painted_rgh.png"), roughness)


# Brushed steel: a tile 0.5 m square of fine horizontal streaks.
def brushed_steel(folder):
    size = 512
    rng = np.random.RandomState(53)
    streak = fbm(rng, size, 120, 2, 3)
    fine = fbm(rng, size, 200, 4, 2)
    tone = 0.6 * streak + 0.4 * fine
    grey = 0.58 + 0.12 * tone
    color = np.stack([grey, grey * 1.01, grey * 1.03], axis=2)
    roughness = 0.22 + 0.22 * tone
    save_rgb(os.path.join(folder, "brushed_steel_basecolor.png"), np.clip(color, 0, 1))
    save_grey(os.path.join(folder, "brushed_steel_roughness.png"), roughness)


# White wall paint: a faint mottled off white.
def white_paint(folder):
    size = 512
    rng = np.random.RandomState(61)
    soft = fbm(rng, size, 6, 6, 4)
    fine = fbm(rng, size, 64, 64, 2)
    tone = 0.93 + 0.025 * soft + 0.012 * fine
    color = np.stack([tone, tone * 0.995, tone * 0.98], axis=2)
    save_rgb(os.path.join(folder, "paint.png"), color)


# Clear glass has no images, only values in material.json.
def clear_glass(folder):
    pass


# Each material: its drawing function and what material.json says.
MATERIALS = [
    ("white_paint", white_paint, {
        "name": "White paint", "groups": ["wall", "ceiling"], "tags": ["paint", "smooth", "white"],
        "tile_size": {"width": 1.0, "height": 1.0}, "maps": {"base_color": "paint.png"}, "values": {"roughness": 0.9}}),
    ("red_brick", red_brick, {
        "name": "Red brick", "groups": ["exterior", "wall"], "tags": ["brick", "red"],
        "tile_size": {"width": 1.2, "height": 0.6}}),
    ("oak_planks", oak_planks, {
        "name": "Oak planks", "groups": ["floor"], "tags": ["boards", "oak", "wood"],
        "tile_size": {"width": 2.0, "height": 2.0}}),
    ("grey_tile", grey_tile, {
        "name": "Grey tile", "groups": ["floor", "wall"], "tags": ["ceramic", "grey", "tile"],
        "tile_size": {"width": 0.6, "height": 0.6}}),
    ("painted_wood", painted_wood, {
        "name": "Painted wood", "groups": ["door", "trim"], "tags": ["green", "paint", "wood"],
        "tile_size": {"width": 1.0, "height": 1.0}}),
    ("brushed_steel", brushed_steel, {
        "name": "Brushed steel", "groups": ["metal"], "tags": ["metal", "steel"],
        "tile_size": {"width": 0.5, "height": 0.5}, "values": {"metallic": 1.0}}),
    ("clear_glass", clear_glass, {
        "name": "Clear glass", "groups": ["glass"], "tags": ["clear", "glass"],
        "tile_size": {"width": 1.0, "height": 1.0}, "base_color_value": [0.82, 0.9, 0.93],
        "values": {"roughness": 0.05, "opacity": 0.2}}),
]


# Draws every sample into its own folder with its material.json.
def main():
    for material_id, draw, info in MATERIALS:
        folder = os.path.join(HERE, material_id)
        shutil.rmtree(folder, ignore_errors=True)
        os.makedirs(folder)
        draw(folder)
        source = {"format": "plynth-material-source", "version": 1, "id": material_id}
        source.update(info)
        source["license"] = "own_work"
        with open(os.path.join(folder, "material.json"), "w", encoding="utf-8", newline="\n") as handle:
            json.dump(source, handle, indent=2)
            handle.write("\n")
        print("drew", material_id)
    total = sum(os.path.getsize(os.path.join(root, name)) for root, _, files in os.walk(HERE) for name in files)
    print("samples/materials is %.2f MB" % (total / 1048576.0))


if __name__ == "__main__":
    main()
