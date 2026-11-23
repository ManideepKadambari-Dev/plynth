import os
import re
import zipfile

from . import ImporterEntry, format_for_file
from ..jobs import WorkerError
from ..registry import IMPORTERS

MAX_UNPACKED_BYTES = 4 * 1024 * 1024 * 1024
PREFERENCE = [".blend", ".fbx", ".glb", ".gltf", ".obj"]


DEVICES = {"con", "prn", "aux", "nul", "conin$", "conout$"} | {
    word + digit for word in ("com", "lpt") for digit in "123456789¹²³"}
# Only A to Z are lowered, as core does, so no other letter can turn into a device name.
ASCII_LOWER = str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz")


# Says whether one part of a zip name is one Windows would treat as something else: a ".." part, a colon (a
# drive letter or a hidden data stream), an ending dot or space (Windows drops them), or a device name such as
# CON or COM1 in any letter case, also with an ending after it (nul.png) or spaces before that ending.
def is_unsafe_part(part):
    if part == ".." or ":" in part:
        return True
    if part != "." and part.endswith((".", " ")):
        return True
    base = part.split(".", 1)[0].rstrip(" ")
    return base.translate(ASCII_LOWER) in DEVICES


# Says whether a name inside a zip is unsafe to unpack: an absolute name or any unsafe part. ZipListing.h in
# core has the same rule, so core refuses the same names before the worker starts.
def is_unsafe(name):
    if name.startswith("/") or name.startswith("\\"):
        return True
    return any(is_unsafe_part(part) for part in re.split(r"[\\/]", name) if part)


# Says whether a path is inside a folder, after resolving links.
def is_inside(path, folder):
    root = os.path.realpath(folder)
    real = os.path.realpath(path)
    return os.path.commonpath([root, real]) == root


# Returns the file entries of a zip after checking that every name and the total size are safe. With members,
# only those entries are looked at: each must be a safe name and be in the zip.
def checked_members(archive, members=None):
    if members is not None:
        found = {info.filename.replace("\\", "/"): info for info in archive.infolist() if not info.is_dir()}
        chosen = []
        for name in members:
            if is_unsafe(name):
                raise WorkerError("unpack_failed", "The zip holds an unsafe path: %s" % name)
            if name not in found:
                raise WorkerError("unpack_failed", "The zip has no entry %s." % name)
            chosen.append(found[name])
        if sum(info.file_size for info in chosen) > MAX_UNPACKED_BYTES:
            raise WorkerError("unpack_failed", "The files are larger than 4 GB when unpacked.")
        return chosen
    result = []
    total = 0
    for info in archive.infolist():
        if is_unsafe(info.filename):
            raise WorkerError("unpack_failed", "The zip holds an unsafe path: %s" % info.filename)
        total += info.file_size
        if not info.is_dir():
            result.append(info)
    if total > MAX_UNPACKED_BYTES:
        raise WorkerError("unpack_failed", "The zip is larger than 4 GB when unpacked.")
    return result


# Unpacks a zip into the folder and returns the names of the files written, with forward slashes. With
# members (entry names), only those are unpacked; every name is checked before the first file is written.
def unpack(path, folder, members=None):
    try:
        archive = zipfile.ZipFile(path)
    except (zipfile.BadZipFile, OSError) as error:
        raise WorkerError("unpack_failed", "The zip cannot be opened: %s" % error)
    names = []
    with archive:
        infos = checked_members(archive, members)
        for info in infos:
            target = os.path.join(folder, *re.split(r"[\/]", info.filename))
            if not is_inside(target, folder):
                raise WorkerError("unpack_failed", "The zip holds an unsafe path: %s" % info.filename)
        for info in infos:
            target = os.path.join(folder, *re.split(r"[\/]", info.filename))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            try:
                with archive.open(info) as source, open(target, "wb") as out:
                    while True:
                        chunk = source.read(1 << 20)
                        if not chunk:
                            break
                        out.write(chunk)
            except (zipfile.BadZipFile, OSError, RuntimeError) as error:
                raise WorkerError("unpack_failed", "Unpacking %s failed: %s" % (info.filename, error))
            names.append(info.filename.replace("\\", "/"))
    return names


# Picks the model to read from the unpacked files by the order of preference. Raises several_models when
# two files of the best kind exist, and import_failed when there is no model.
def pick_model(names):
    usable = [n for n in names if not n.startswith("__MACOSX/") and not os.path.basename(n).startswith(".")]
    for suffix in PREFERENCE:
        found = sorted(n for n in usable if os.path.splitext(n)[1].lower() == suffix)
        if len(found) > 1:
            raise WorkerError("several_models", "The zip holds several models: %s." % ", ".join(found))
        if found:
            return found[0]
    raise WorkerError("import_failed", "The zip holds no model file.")


# Unpacks the zip, picks the model inside and hands it to the importer of its file format.
def load(ctx, path):
    folder = os.path.join(ctx.work_folder, "unpacked")
    names = unpack(path, folder)
    picked = pick_model(names)
    ctx.state["picked_file"] = picked
    ctx.state.setdefault("search_folders", []).append(folder)
    importer = IMPORTERS.get(format_for_file(picked))
    importer.load(ctx, os.path.join(folder, *picked.split("/")))


IMPORTERS.register("zip", ImporterEntry((".zip",), load))
