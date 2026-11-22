import os
import re

import bpy

from . import fab_names
from .. import shading
from ..materials import read_pixels, read_size, to_bytes, write_grey, write_png

CHANNELS = ("base_color", "normal", "roughness", "metallic", "opacity", "occlusion")
PACKED_CHANNELS = (("occlusion", 0), ("roughness", 1), ("metallic", 2))


# Returns the materials that objects of the scene use, by name.
def scene_materials():
    found = {}
    for obj in bpy.context.scene.objects:
        for slot in obj.material_slots:
            if slot.material is not None:
                found[slot.material.name] = slot.material
    return [found[name] for name in sorted(found)]


# Says whether a material has image textures of its own and every one of them loads.
def keeps_own_images(material):
    if material.node_tree is None:
        return False
    images = [node.image for node in material.node_tree.nodes if node.type == "TEX_IMAGE"]
    return bool(images) and all(image is not None and image.size[0] > 0 for image in images)


# Returns a file name that is safe on disk for a material name.
def safe_name(name):
    return re.sub(r"[^A-Za-z0-9_.-]", "_", name)


# Sorts the texture files into sets by the material name they carry. Returns {key: set} where a set is
# {"channels": {kind: [files]}, "gloss": [files], "packed": [files], "ignored": [files]}. Files that
# no word of the table fits are left out, and so reach the unused_image warning.
def make_sets(files):
    sets = {}
    for path in files:
        kind, _, _, _ = fab_names.classify(path)
        if kind is None:
            continue
        entry = sets.setdefault(fab_names.set_key(path), {"channels": {}, "gloss": [], "packed": [], "ignored": []})
        if kind in ("gloss", "packed", "ignored"):
            entry[kind].append(path)
        else:
            entry["channels"].setdefault(kind, []).append(path)
    return sets


# Says whether a set holds a map for a channel, a roughness from gloss, or a packed map.
def has_maps(entry):
    return bool(entry["channels"] or entry["gloss"] or entry["packed"])


# Finds the set for a material: the one with its name, or the only set when there is just one.
def set_for(material, sets):
    usable = {key: entry for key, entry in sets.items() if has_maps(entry)}
    key = fab_names.material_key(material.name)
    if key in usable:
        return usable[key]
    if len(usable) == 1:
        return next(iter(usable.values()))
    return None


# Returns (resolution word, pixels, name) for ranking a file, higher is better.
def size_rank(path):
    try:
        width, height = read_size(path)
    except ValueError:
        width = height = 0
    return fab_names.resolution_of(path) or 0, width * height, os.path.basename(path)


# Takes the best file of a channel. Files of the same resolution word and word rank are ambiguous: the
# largest is used and a warning says so. Every file of the channel counts as used.
def best_file(ctx, channel, found, used):
    used.update(found)
    ranked = sorted(found, key=lambda path: (fab_names.classify(path)[2], size_rank(path)), reverse=True)
    best = ranked[0]
    if len(ranked) > 1:
        top = (fab_names.classify(best)[2], fab_names.resolution_of(best))
        tied = [p for p in ranked[1:] if (fab_names.classify(p)[2], fab_names.resolution_of(p)) == top]
        if tied:
            ctx.warn("ambiguous_map", "Several images fit %s; used %s." % (channel, os.path.basename(best)))
    return best


# Writes a grey map from a gloss map (one minus its value) into the folder and returns its path.
def write_roughness(path, folder, stem):
    plane = to_bytes(read_pixels(path)[:, :, 0])
    target = os.path.join(folder, "%s_roughness.png" % stem)
    write_grey(target, (255 - plane) / 255.0)
    return target


# Writes a DirectX normal map with its green flipped, so it reads as OpenGL, and returns its path.
def write_flipped_normal(path, folder, stem):
    rgb = to_bytes(read_pixels(path)[:, :, :3])
    rgb[:, :, 1] = 255 - rgb[:, :, 1]
    target = os.path.join(folder, "%s_normal.png" % stem)
    write_png(target, rgb)
    return target


# Writes one channel of a packed map as a grey image and returns its path.
def write_packed_channel(path, folder, stem, channel, index):
    target = os.path.join(folder, "%s_%s.png" % (stem, channel))
    write_grey(target, read_pixels(path)[:, :, index])
    return target


# Chooses the map file of each channel for one material from its set. Returns {channel: path}; maps made
# by a conversion are written into the folder.
def maps_from_set(ctx, entry, normal_option, folder, stem, used):
    chosen = {}
    for channel in CHANNELS:
        if channel in entry["channels"]:
            chosen[channel] = best_file(ctx, channel, entry["channels"][channel], used)
    if "roughness" not in chosen and entry["gloss"]:
        chosen["roughness"] = write_roughness(best_file(ctx, "roughness", entry["gloss"], used), folder, stem)
    elif entry["gloss"]:
        used.update(entry["gloss"])
    if entry["packed"]:
        packed = best_file(ctx, "packed", entry["packed"], used)
        for channel, index in PACKED_CHANNELS:
            if channel not in chosen:
                chosen[channel] = write_packed_channel(packed, folder, stem, channel, index)
    used.update(entry["ignored"])
    if "normal" in chosen:
        convention = normal_option if normal_option != "auto" else (fab_names.classify(chosen["normal"])[1] or "opengl")
        if convention == "directx":
            chosen["normal"] = write_flipped_normal(chosen["normal"], folder, stem)
    return chosen


# Returns the maps the user picked for a material, as {channel: path}. A picked file that is not there is
# reported and left out.
def picked_maps(ctx, source, material, root):
    maps = {}
    wanted = dict(source["maps"].get("*", {}))
    wanted.update(source["maps"].get(material.name, {}))
    for channel, entry in wanted.items():
        path = os.path.join(root, *entry.split("/"))
        if os.path.isfile(path):
            maps[channel] = path
        else:
            ctx.warn("missing_texture", "The texture file %s was not found." % os.path.basename(entry))
    return maps


# Builds the materials of the imported model from the unpacked textures. A material whose own images all
# load keeps them unless the user picked maps for it. The others get the set of textures that carries the
# material's name, or the only set. Warns missing_texture, ambiguous_map and unused_image. Returns a list of
# {"material", "source", "channels"} for the result's details.
def apply(ctx, source, root):
    files = [os.path.join(root, *entry.split("/")) for entry in source["textures"]]
    files = [path for path in files if os.path.isfile(path)]
    sets = make_sets(files)
    folder = os.path.join(ctx.work_folder, "fab_maps")
    used = set()
    report = []
    for material in scene_materials():
        picks = picked_maps(ctx, source, material, root)
        if keeps_own_images(material) and not picks:
            report.append({"material": material.name, "source": "model", "channels": []})
            continue
        entry = set_for(material, sets)
        os.makedirs(folder, exist_ok=True)
        stem = safe_name(material.name)
        maps = maps_from_set(ctx, entry, source["normal"], folder, stem, used) if entry else {}
        maps.update(picks)
        used.update(picks.values())
        if "base_color" not in maps:
            ctx.warn("missing_texture", "The material %s has no base color texture." % material.name)
            report.append({"material": material.name, "source": "none", "channels": []})
            continue
        try:
            shading.apply_maps(material, maps)
        except shading.MapError as error:
            ctx.warn("missing_texture", "The material %s could not use its textures: %s" % (material.name, error))
            report.append({"material": material.name, "source": "none", "channels": []})
            continue
        report.append({"material": material.name, "source": "textures", "channels": sorted(maps)})
    skipped = ignored_files(sets)
    for path in sorted(p for p in files if p not in used and p not in skipped):
        ctx.warn("unused_image", "%s matched no material and was not used." % os.path.basename(path))
    return report


# Returns the files of the sets that only hold maps Plynth does not use.
def ignored_files(sets):
    found = set()
    for entry in sets.values():
        found.update(entry["ignored"])
    return found
