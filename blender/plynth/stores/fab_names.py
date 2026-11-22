import re

from ..materials import names

# Channel words of Fab packs on top of materials/names.py, which stays as it is. Fab downloads carry
# several extra maps (cavity, bump, specular, displacement) that Plynth does not use, and often both a
# BaseColor and a Diffuse map that are the same picture.
SMOOTHNESS = {"smooth", "smoothness"}
EXTRA_IGNORED = {"cavity", "curvature", "curv", "thickness", "translucency", "transmission", "sss", "sheen",
                 "fuzz", "tangent", "worldposition", "position", "id", "idmask"}
LOW_RANK = {"diffuse", "diff"}
QUALITY_WORDS = {"low", "lowpoly", "lp", "medium", "med", "mid", "high", "highpoly", "hp", "raw", "nanite",
                 "source", "original"}
PREFIXES = ("t", "m", "mi", "mat")
COMPOUNDS = {("base", "color"), ("base", "colour"), ("ambient", "occlusion")}
RESOLUTION = re.compile(r"^(\d+(?:\.\d+)?)k$")
PIXELS = {"256", "512", "1024", "2048", "4096", "8192"}

TABLE = dict(names.TABLE)
for _word in SMOOTHNESS:
    TABLE[_word] = "gloss"
for _word in EXTRA_IGNORED:
    TABLE[_word] = "ignored"


# Returns the pixels across that a resolution word names (2k gives 2048, 05k gives 512), or None.
def resolution_of_word(word):
    if word in PIXELS:
        return int(word)
    found = RESOLUTION.match(word)
    if not found:
        return None
    digits = found.group(1)
    value = float("0." + digits[1:]) if len(digits) > 1 and digits[0] == "0" and "." not in digits else float(digits)
    return int(round(value * 1024)) if value > 0 else None


# Splits a file name into words like names.tokens, but keeps a size such as 4K together (names.tokens
# splits it into 4 and k).
def words(filename):
    parts = names.tokens(filename)
    merged = []
    for part in parts:
        if part == "k" and merged and re.fullmatch(r"\d+(?:\.\d+)?", merged[-1]):
            merged[-1] += "k"
        else:
            merged.append(part)
    return merged


# Returns the largest resolution named in a file's words, or None.
def resolution_of(filename):
    found = [resolution_of_word(word) for word in words(filename)]
    found = [value for value in found if value]
    return max(found) if found else None


# Says what a file name means: (kind, convention, rank, index). kind is as in names.classify with the Fab
# words added; rank is 0 for a word that is only a stand in (diffuse beside basecolor) and 1 otherwise; index
# is the position of the channel word among the words, or None. A bent normal map is ignored.
def classify(filename):
    parts = words(filename)
    kind = None
    index = None
    rank = 1
    if "bent" in parts:
        kind = "ignored"
    else:
        for position in range(len(parts) - 1, -1, -1):
            if parts[position] in TABLE:
                kind = TABLE[parts[position]]
                index = position
                rank = 0 if parts[position] in LOW_RANK else 1
                break
    convention = names.classify(filename)[1]
    return kind, convention, rank, index


# Returns the key that says which material a texture belongs to: the file's words without the channel word,
# the convention, resolution, quality and LOD words and the leading T, M, MI and MAT, joined with "_".
def set_key(filename):
    parts = words(filename)
    _, _, _, index = classify(filename)
    if index is not None:
        start = index - 1 if index > 0 and (parts[index - 1], parts[index]) in COMPOUNDS else index
        parts = parts[:start] + parts[index + 1:]
    return join_key(parts)


# Splits a plain name (not a file name) into words.
def words_of(name):
    return words(name + ".x")


# Returns the key of a material name, made the same way. A Blender copy suffix such as .001 is dropped.
def material_key(name):
    return join_key(words_of(re.sub(r"\.\d{3}$", "", name)))


# Drops the words that do not name the material and joins the rest.
def join_key(parts):
    kept = []
    skip_lod_number = False
    for part in parts:
        if skip_lod_number and part.isdigit():
            skip_lod_number = False
            continue
        skip_lod_number = False
        if part in names.DIRECTX or part in names.OPENGL or part in QUALITY_WORDS or part in ("directx", "opengl", "map"):
            continue
        if resolution_of_word(part) or re.fullmatch(r"lod\d*", part):
            skip_lod_number = part == "lod"
            continue
        kept.append(part)
    while kept and kept[0] in PREFIXES:
        kept.pop(0)
    return "_".join(kept)
