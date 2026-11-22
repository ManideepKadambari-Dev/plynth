import os
import re

BASE_COLOR = {"basecolor", "albedo", "diffuse", "diff", "color", "colour", "col", "base"}
NORMAL = {"normal", "nor", "nrm", "norm"}
ROUGHNESS = {"roughness", "rough", "rgh"}
GLOSSINESS = {"gloss", "glossiness"}
METALLIC = {"metallic", "metalness", "metal", "mtl"}
OPACITY = {"opacity", "alpha", "mask"}
OCCLUSION = {"ao", "ambientocclusion", "occlusion", "ambient"}
PACKED = {"arm", "orm"}
IGNORED = {"height", "displacement", "disp", "bump", "specular", "spec", "emission", "emissive", "emit",
           "preview", "thumb", "thumbnail"}
DIRECTX = {"dx", "directx"}
OPENGL = {"gl", "opengl"}

TABLE = {}
for _kind, _words in (("base_color", BASE_COLOR), ("normal", NORMAL), ("roughness", ROUGHNESS),
                      ("gloss", GLOSSINESS), ("metallic", METALLIC), ("opacity", OPACITY),
                      ("occlusion", OCCLUSION), ("packed", PACKED), ("ignored", IGNORED)):
    for _word in _words:
        TABLE[_word] = _kind


# Splits a file name into lower case tokens at _ - space and dot, and where a lower case letter or digit
# meets a capital (NormalDX gives normal and dx).
def tokens(filename):
    stem = os.path.splitext(os.path.basename(filename))[0]
    stem = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", stem)
    stem = re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", "_", stem)
    return [part.lower() for part in re.split(r"[_\-\s.]+", stem) if part]


# Says what a file name means: (kind, convention). kind is base_color, normal, roughness, gloss, metallic,
# opacity, occlusion, packed, ignored or None; convention is opengl, directx or None. The last known word
# in the name decides, because the channel comes last in most names (metal_plate_diff is a base color).
def classify(filename):
    parts = tokens(filename)
    joined = "".join(parts)
    kind = None
    for part in reversed(parts):
        if part in TABLE:
            kind = TABLE[part]
            break
    convention = None
    if "directx" in joined or any(part in DIRECTX for part in parts):
        convention = "directx"
    elif "opengl" in joined or any(part in OPENGL for part in parts):
        convention = "opengl"
    return kind, convention


# Sorts files into groups: {"channels": {channel: [files]}, "gloss": [...], "packed": [...], "ignored": [...],
# "unmatched": [...]}. Each list is in name order.
def group(files):
    result = {"channels": {}, "gloss": [], "packed": [], "ignored": [], "unmatched": []}
    for path in sorted(files, key=lambda item: item.lower()):
        kind, _ = classify(path)
        if kind is None:
            result["unmatched"].append(path)
        elif kind in ("gloss", "packed", "ignored"):
            result[kind].append(path)
        else:
            result["channels"].setdefault(kind, []).append(path)
    return result


# Returns {channel: file} for the files, taking the largest by rank(file) when several fit a channel.
def match(files, rank=lambda path: 0):
    groups = group(files)
    return {channel: max(found, key=lambda path: (rank(path), path))
            for channel, found in groups["channels"].items()}
