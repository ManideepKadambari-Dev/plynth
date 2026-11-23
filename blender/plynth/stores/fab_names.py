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

PREVIEW_WORDS = {"preview", "previews", "thumb", "thumbs", "thumbnail", "thumbnails", "render", "renders",
                 "screenshot", "screenshots", "cover", "covers"}
FIRST_PREVIEW_WORDS = {"preview", "previews", "thumb", "thumbs", "thumbnail", "thumbnails", "screenshot",
                       "screenshots"}
FORMAT_WORDS = {"fbx", "gltf", "glb", "obj", "blend", "usd", "usda", "usdc", "usdz"}
PREVIEW_RESOLUTIONS = {"05k", "1k", "2k", "4k", "8k", "16k", "256", "512", "1024", "2048", "4096", "8192"}
ASCII_LOWER = str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz")

# Previews are told by is_preview alone, as core does, so the preview words of names.py are no channel here.
TABLE = {word: kind for word, kind in names.TABLE.items() if word not in PREVIEW_WORDS}
for _word in SMOOTHNESS:
    TABLE[_word] = "gloss"
for _word in EXTRA_IGNORED:
    TABLE[_word] = "ignored"


# Lowers the letters A to Z only, as core does.
def ascii_lower(text):
    return text.translate(ASCII_LOWER)


# Splits one part of a zip entry into words the way core does: at ASCII characters that are not letters or
# digits, and where a lower case letter is followed by an upper case one. A piece that is a format or quality
# word in any case (glTF, LowPoly) stays whole.
def entry_words(part):
    found = []

    # Adds one piece between separators, split at its case changes.
    def add(piece):
        if not piece:
            return
        if ascii_lower(piece) in FORMAT_WORDS or ascii_lower(piece) in QUALITY_WORDS:
            found.append(piece)
            return
        current = ""
        for char in piece:
            if current and "A" <= char <= "Z" and "a" <= current[-1] <= "z":
                found.append(current)
                current = ""
            current += char
        found.append(current)

    piece = ""
    for char in part:
        if ord(char) < 128 and not char.isalnum():
            add(piece)
            piece = ""
        else:
            piece += char
    add(piece)
    return found


# Says whether an image entry of a zip is a preview picture and not a texture, by core's rule: a folder whose
# whole name is a preview word or its plural; a file whose last word, leaving out numbers and resolutions, is
# one; or whose first word is preview, thumb, thumbnail or screenshot, or a plural of one. The same words
# elsewhere belong to the item's name (Manhole_Cover_BaseColor).
def is_preview(entry):
    parts = [part for part in entry.split("/") if part]
    if not parts:
        return False
    if any(ascii_lower(part) in PREVIEW_WORDS for part in parts[:-1]):
        return True
    stem = parts[-1][:parts[-1].rfind(".")] if "." in parts[-1] else parts[-1]
    kept = [ascii_lower(word) for word in entry_words(stem)]
    kept = [word for word in kept if not re.fullmatch(r"[0-9]+", word) and word not in PREVIEW_RESOLUTIONS]
    return bool(kept) and (kept[-1] in PREVIEW_WORDS or kept[0] in FIRST_PREVIEW_WORDS)


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
    convention = names.classify(filename)[1]
    if any(part.startswith("bent") and part[4:] in names.NORMAL for part in parts):
        return "ignored", convention, 1, None
    for position in range(len(parts) - 1, -1, -1):
        if parts[position] in TABLE:
            kind = TABLE[parts[position]]
            if kind == "normal" and is_bent_normal(parts, position):
                return "ignored", convention, 1, None
            return kind, convention, 0 if parts[position] in LOW_RANK else 1, position
    return None, convention, 1, None


# Says whether the normal word at a position has bent right before or after it (Wood_Bent_Normal). Bent
# elsewhere is part of the item's name (Bent_Wood_Chair_Normal is a plain normal map).
def is_bent_normal(parts, position):
    return (position > 0 and parts[position - 1] == "bent") or (
        position + 1 < len(parts) and parts[position + 1] == "bent")


# Returns the file name stem for the maps made for one material: its place in the list of materials and its
# name in ASCII letters, digits, "_" and "-". The place keeps two names apart that differ only in letter case
# or in letters outside ASCII, on every file system.
def map_stem(index, name):
    return "%d_%s" % (index, re.sub(r"[^A-Za-z0-9_-]", "_", name))


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
