import json
import os
import sys
import tempfile
import zipfile

import bpy
import numpy as np

import helpers
from cleanup_helpers import CleanupTest
from plynth.materials import write_png

TOOLS = os.path.join(helpers.BLENDER_FOLDER, "tests", "tools")
if TOOLS not in sys.path:
    sys.path.insert(0, TOOLS)

import make_fab_zips  # noqa: E402

_ZIP_FOLDER = []


# Makes the test zips once for the whole run and returns their folder.
def zip_folder():
    if not _ZIP_FOLDER:
        folder = tempfile.mkdtemp(prefix="plynth_fab_test_zips_")
        make_fab_zips.make_zips(folder)
        _ZIP_FOLDER.append(folder)
    return _ZIP_FOLDER[0]


# Returns the path of one of the test zips.
def zip_path(name):
    return os.path.join(zip_folder(), name).replace("\\", "/")


# Returns every file of a zip as {entry name: bytes}.
def zip_entries(name):
    with zipfile.ZipFile(zip_path(name)) as archive:
        return {info.filename: archive.read(info) for info in archive.infolist() if not info.is_dir()}


# Returns the bytes of a small grey PNG.
def small_png(value=128, size=8):
    handle, path = tempfile.mkstemp(suffix=".png")
    os.close(handle)
    try:
        write_png(path, np.full((size, size, 3), value, dtype=np.uint8))
        with open(path, "rb") as source:
            return source.read()
    finally:
        os.remove(path)


class FabTest(CleanupTest):
    # Writes a zip with the given entries and returns its path.
    def write_entries_zip(self, name, entries):
        path = self.path("in", name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with zipfile.ZipFile(path, "w") as archive:
            for entry, data in entries.items():
                archive.writestr(entry, data)
        return path

    # Writes source.fab.json into the work folder and returns its path. The fields are the schema's, with
    # the defaults of an empty choice.
    def write_source(self, zip_file, model, files, textures=(), **fields):
        source = {
            "format": "plynth-fab-source", "version": 1, "zip": zip_file.replace("\\", "/"), "kind": "model",
            "unpack_folder": self.path("work", "unpacked"), "files": sorted(files), "model": model,
            "model_format": os.path.splitext(model)[1][1:].lower().replace("glb", "gltf"),
            "textures": sorted(textures), "metadata": None, "maps": {}, "normal": "auto",
        }
        source.update(fields)
        path = self.path("work", "source.fab.json")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(source, handle)
        return path

    # Runs the cleanup of a Fab source file.
    def cleanup_fab(self, source_path, **overrides):
        return self.cleanup(source_path, "fab", **overrides)

    # Returns the files under the unpack folder, as entry names.
    def unpacked(self):
        root = self.path("work", "unpacked")
        found = []
        for current, _dirs, files in os.walk(root):
            for name in files:
                found.append(os.path.relpath(os.path.join(current, name), root).replace("\\", "/"))
        return sorted(found)

    # Returns {material name: {socket name: source file name}} for the opened asset. The cleanup rebuilds every
    # material from maps in textures/maps/<material>/, so the name of the file a socket reads says nothing about
    # the pack. The map it reads is found in the result's material_maps, and that entry holds the pack file it
    # was made from. A socket fed by a constant (a map of origin "value", "color" or "none") is left out.
    def material_images(self, result):
        entries = {}
        for item in result["output"]["material_maps"]:
            for channel, entry in item["maps"].items():
                if entry["file"] and entry["from"] == "file":
                    entries[(item["name"], entry["file"])] = entry["detail"]
        found = {}
        for material in bpy.data.materials:
            if material.node_tree is None:
                continue
            shader = next((n for n in material.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
            images = {}
            for link in material.node_tree.links:
                if link.to_node == shader:
                    source = link.from_node
                    if source.type == "NORMAL_MAP" and source.inputs["Color"].links:
                        source = source.inputs["Color"].links[0].from_node
                    if source.type == "TEX_IMAGE" and source.image is not None:
                        path = source.image.filepath.replace("\\", "/")
                        detail = entries.get((material.name, path[2:] if path.startswith("//") else path))
                        if detail is not None:
                            images[link.to_socket.name] = detail
            found[material.name] = images
        return found

    # Returns the path of a map the cleanup wrote for a material: textures/maps/<key>/<channel>.<suffix>.
    def map_path(self, key, channel, suffix=".png"):
        return self.path("item", "textures", "maps", key, channel + suffix)
