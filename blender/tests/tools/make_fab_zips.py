import datetime
import hashlib
import importlib
import importlib.util
import json
import os
import shutil
import sys
import tempfile
import types
import zipfile

import bpy
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
BLENDER_FOLDER = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO_FOLDER = os.path.abspath(os.path.join(BLENDER_FOLDER, ".."))
if BLENDER_FOLDER not in sys.path:
    sys.path.insert(0, BLENDER_FOLDER)

from plynth.materials import write_png  # noqa: E402

FIXED_TIME = (1980, 1, 1, 0, 0, 0)
FIXED_DATE = datetime.datetime(2000, 1, 1, 0, 0, 0)

# Pixel sizes that stand in for the resolution words of the names, so the zips stay small.
SIZES = {"1K": 32, "2K": 64, "4K": 96}
CHANNELS = ("BaseColor", "Normal", "Roughness", "Metallic")
COLORS = {"Wood": (0.55, 0.38, 0.24), "Fabric": (0.45, 0.12, 0.10), "Stone": (0.40, 0.40, 0.42)}


# Loads the sample model writer from samples/models, which draws the chair without any stored file.
def sample_writer():
    path = os.path.join(REPO_FOLDER, "samples", "models", "make_sample_models.py")
    spec = importlib.util.spec_from_file_location("make_sample_models", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# Returns a picture of a channel as rows of RGB bytes. It is drawn from the seed, so it is the same every run.
def drawn_map(channel, size, seed):
    rng = np.random.RandomState(seed)
    x = np.arange(size)[None, :].repeat(size, axis=0)
    y = np.arange(size)[:, None].repeat(size, axis=1)
    noise = rng.randint(-8, 9, (size, size))
    if channel == "BaseColor":
        grain = 150 + 40 * np.sin(x / size * 18.0 + y / size * 3.0) + noise
        rgb = np.stack([grain + 40, grain, grain - 60], axis=2)
    elif channel == "Normal":
        rgb = np.stack([128 + noise, 100 + (y * 40 // size) + noise // 2, np.full((size, size), 230)], axis=2)
    elif channel == "Roughness":
        rgb = np.repeat((120 + 60 * np.cos(x / size * 9.0) + noise)[:, :, None], 3, axis=2)
    else:
        rgb = np.repeat(((x // 8 + y // 8) % 2 * 90 + 20)[:, :, None], 3, axis=2)
    return np.clip(rgb, 0, 255).astype(np.uint8)


# Returns the bytes of a drawn map as a PNG.
def png_bytes(channel, size, seed):
    handle, path = tempfile.mkstemp(suffix=".png")
    os.close(handle)
    try:
        write_png(path, drawn_map(channel, size, seed))
        with open(path, "rb") as source:
            return source.read()
    finally:
        os.remove(path)


# Returns the bytes of a drawn map as a JPEG made by Blender.
def jpg_bytes(channel, size, seed):
    pixels = drawn_map(channel, size, seed)[::-1].astype(np.float32) / 255.0
    rgba = np.concatenate([pixels, np.ones((size, size, 1), dtype=np.float32)], axis=2)
    image = bpy.data.images.new("fab_jpg", size, size, alpha=False)
    image.colorspace_settings.name = "Non-Color"
    image.pixels.foreach_set(rgba.reshape(-1))
    handle, path = tempfile.mkstemp(suffix=".jpg")
    os.close(handle)
    try:
        image.filepath_raw = path
        image.file_format = "JPEG"
        image.save()
        with open(path, "rb") as source:
            return source.read()
    finally:
        bpy.data.images.remove(image)
        os.remove(path)


# Returns a plain Principled material of one colour with no images.
def plain_material(name):
    color = COLORS[name]
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    tree = material.node_tree
    tree.nodes.clear()
    output = tree.nodes.new("ShaderNodeOutputMaterial")
    shader = tree.nodes.new("ShaderNodeBsdfPrincipled")
    shader.inputs["Base Color"].default_value = (color[0], color[1], color[2], 1.0)
    tree.links.new(shader.outputs["BSDF"], output.inputs["Surface"])
    return material


# Imports the sample chair into an empty scene and gives it image free materials named Wood and Fabric.
def load_chair(work):
    writer = sample_writer()
    folder = os.path.join(work, "chair_source")
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, "chair.obj"), "w", encoding="ascii") as handle:
        handle.write(writer.chair_obj())
    with open(os.path.join(folder, "chair.mtl"), "w", encoding="ascii") as handle:
        handle.write(writer.chair_mtl())
    with open(os.path.join(folder, "chair_wood.png"), "wb") as handle:
        handle.write(writer.wood_png())
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.wm.obj_import(filepath=os.path.join(folder, "chair.obj"))
    slots = [(obj, index, slot.material.name)
             for obj in bpy.context.scene.objects for index, slot in enumerate(obj.material_slots)]
    for material in list(bpy.data.materials):
        bpy.data.materials.remove(material)
    plain = {name: plain_material(name) for name in sorted({name for _, _, name in slots})}
    for obj, index, name in slots:
        obj.material_slots[index].material = plain[name]
    for image in list(bpy.data.images):
        bpy.data.images.remove(image)


# Replaces the scene with a plain box of one material, 0.3 metres each way.
def load_box(material_name):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.mesh.primitive_cube_add(size=0.3, location=(0.0, 0.0, 0.15))
    box = bpy.context.object
    box.name = "Box"
    box.data.materials.append(plain_material(material_name))


# Makes the FBX exporter repeatable: a fixed time stamp, and object ids made from a stable hash of the names
# (Python's own string hash changes on every start).
def freeze_fbx():
    module = importlib.import_module("io_scene_fbx.export_fbx_bin")
    utils = importlib.import_module("io_scene_fbx.fbx_utils")
    if getattr(module.datetime, "__name__", "") == "datetime":
        module.datetime = types.SimpleNamespace(datetime=types.SimpleNamespace(now=lambda: FIXED_DATE))
    if not getattr(utils, "_plynth_frozen", False):
        original = utils._key_to_uuid
        utils._key_to_uuid = lambda uuids, key: original(
            uuids, int(hashlib.sha1(repr(key).encode("utf-8")).hexdigest()[:15], 16))
        utils._plynth_frozen = True
    utils._keys_to_uuids.clear()
    utils._uuids_to_keys.clear()


# Exports the whole scene as FBX bytes.
def fbx_bytes(work):
    freeze_fbx()
    path = os.path.join(work, "export.fbx")
    bpy.ops.export_scene.fbx(filepath=path, path_mode="STRIP")
    with open(path, "rb") as handle:
        return handle.read()


# Exports the whole scene as a glTF with a separate .bin and returns (gltf bytes, bin bytes).
def gltf_bytes(work, stem):
    path = os.path.join(work, stem + ".gltf")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLTF_SEPARATE")
    with open(path, "rb") as handle:
        gltf = handle.read()
    with open(os.path.join(work, stem + ".bin"), "rb") as handle:
        binary = handle.read()
    return gltf, binary


# Writes a zip with the entries in name order, fixed times and permissions, so the bytes repeat.
def write_zip(path, entries):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name in sorted(entries):
            info = zipfile.ZipInfo(name, FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, entries[name])


# Returns a json file's bytes.
def json_bytes(data):
    return json.dumps(data, indent=1, sort_keys=True).encode("ascii")


# The map entries of a folder: the four maps of a prefix at the sizes.
def texture_entries(folder, prefix, sizes, channels=CHANNELS):
    entries = {}
    for size_name in sizes:
        for seed, channel in enumerate(channels):
            entries["%s/T_%s_%s_%s.png" % (folder, prefix, channel, size_name)] = png_bytes(
                channel, SIZES[size_name], seed + 1)
    return entries


# Makes chair_variants.zip: FBX at three qualities, a LOD 1, glTF, and the maps of each variant at 1K and 2K.
def chair_variants(work):
    load_chair(work)
    fbx = fbx_bytes(work)
    gltf, binary = gltf_bytes(work, "Chair")
    entries = {
        "Chair/FBX/Low/Chair_LOD0.fbx": fbx,
        "Chair/FBX/Medium/Chair_LOD0.fbx": fbx,
        "Chair/FBX/Medium/Chair_LOD1.fbx": fbx,
        "Chair/FBX/High/Chair_LOD0.fbx": fbx,
        "Chair/glTF/Medium/Chair.gltf": gltf,
        "Chair/glTF/Medium/Chair.bin": binary,
        "Chair/Previews/front.png": png_bytes("BaseColor", 32, 9),
        "Chair/Chair.json": json_bytes({"id": "chair01", "semanticTags": {"name": "Chair"}, "physicalSize": "0.45x0.5"}),
    }
    for folder in ("Chair/FBX/Low", "Chair/FBX/Medium", "Chair/FBX/High", "Chair/glTF/Medium"):
        entries.update(texture_entries(folder + "/Textures", "Chair", ("1K", "2K")))
    return entries


# Makes chair_unlinked.zip: one FBX whose materials point at no images, and maps beside it named after them.
def chair_unlinked(work):
    load_chair(work)
    entries = {"Chair.fbx": fbx_bytes(work)}
    for number, material in enumerate(("Wood", "Fabric")):
        for seed, channel in enumerate(CHANNELS[:3]):
            entries["%s_%s.png" % (material, channel)] = png_bytes(channel, 64, 20 + number * 10 + seed)
    return entries


# Makes chair_flat.zip: the layout of a Megascans download, a flat folder with the model, one map set whose
# names carry the quality, the resolution and the channel, and a json.
def chair_flat(work):
    load_chair(work)
    stem = "Wooden_Chair_Old_01_abc123"
    entries = {
        "%s_High.fbx" % stem: fbx_bytes(work),
        "abc123.json": json_bytes({"id": "abc123", "semanticTags": {"name": "Wooden Chair Old 01"},
                                   "physicalSize": "0.45x0.5"}),
    }
    for number, channel in enumerate(("BaseColor", "Normal", "Roughness", "Diffuse", "Cavity")):
        look = "BaseColor" if channel == "Diffuse" else ("Roughness" if channel == "Cavity" else channel)
        entries["%s_High_4K_%s.jpg" % (stem, channel)] = jpg_bytes(look, SIZES["4K"], 40 + number)
    return entries


# Makes pack.zip: the chair and a box, each with its own maps.
def pack(work):
    load_chair(work)
    entries = {"Chair/Chair.fbx": fbx_bytes(work)}
    entries.update(texture_entries("Chair/Textures", "Chair", ("2K",)))
    load_box("Stone")
    entries["Box/Box.fbx"] = fbx_bytes(work)
    entries.update(texture_entries("Box/Textures", "Slate", ("2K",)))
    return entries


# Makes uasset_only.zip: engine files and nothing a worker can read.
def uasset_only(work):
    return {"Content/Chair/Chair.uasset": b"UASSET", "Content/Chair/Chair.uexp": b"UEXP"}


# Makes oak_material.zip: an oak map set at 2K and 4K, a preview and a json with a size.
def oak_material(work):
    entries = texture_entries("Oak", "Oak", ("2K", "4K"), CHANNELS[:3])
    entries["Oak/Previews/Oak_preview.png"] = png_bytes("BaseColor", 32, 70)
    entries["Oak/Oak.json"] = json_bytes({"id": "oak01", "semanticTags": {"name": "Oak"}, "physicalSize": "2x2"})
    return entries


MAKERS = [
    ("chair_variants.zip", chair_variants),
    ("chair_unlinked.zip", chair_unlinked),
    ("chair_flat.zip", chair_flat),
    ("pack.zip", pack),
    ("uasset_only.zip", uasset_only),
    ("oak_material.zip", oak_material),
]


# Writes every test zip into the folder and returns {file name: path}. The bytes are the same every run.
def make_zips(folder):
    os.makedirs(folder, exist_ok=True)
    work = os.path.join(tempfile.gettempdir(), "plynth_fab_zips_work")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    paths = {}
    try:
        for name, maker in MAKERS:
            path = os.path.join(folder, name)
            write_zip(path, maker(work))
            paths[name] = path
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return paths


# Returns the SHA-256 of a file.
def digest(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


# Makes the zips into the folder after "--" in the command line and prints their names, sizes and digests.
def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if not args:
        print("usage: blender -b --python make_fab_zips.py -- <folder>")
        sys.exit(2)
    for name, path in make_zips(args[0]).items():
        print("%s %d bytes sha256 %s" % (name, os.path.getsize(path), digest(path)))


if __name__ == "__main__":
    main()
