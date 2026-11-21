import os
import re
import zipfile

from . import ImporterEntry, format_for_file
from ..jobs import WorkerError
from ..registry import IMPORTERS

MAX_UNPACKED_BYTES = 4 * 1024 * 1024 * 1024
PREFERENCE = [".blend", ".fbx", ".glb", ".gltf", ".obj"]


# Says whether a name inside a zip could leave the target folder: an absolute name, a ".." part, or any
# part with a colon (a drive letter or an alternate data stream).
def is_unsafe(name):
    if name.startswith("/") or name.startswith("\\"):
        return True
    return any(part == ".." or ":" in part for part in re.split(r"[\\/]", name))


# Says whether a path is inside a folder, after resolving links.
def is_inside(path, folder):
    root = os.path.realpath(folder)
    real = os.path.realpath(path)
    return os.path.commonpath([root, real]) == root


# Returns the file entries of a zip after checking that every name and the total size are safe.
def checked_members(archive):
    members = []
    total = 0
    for info in archive.infolist():
        if is_unsafe(info.filename):
            raise WorkerError("unpack_failed", "The zip holds an unsafe path: %s" % info.filename)
        total += info.file_size
        if not info.is_dir():
            members.append(info)
    if total > MAX_UNPACKED_BYTES:
        raise WorkerError("unpack_failed", "The zip is larger than 4 GB when unpacked.")
    return members


# Unpacks a zip into the work folder and returns the names of the files written, with forward slashes.
def unpack(path, folder):
    try:
        archive = zipfile.ZipFile(path)
    except (zipfile.BadZipFile, OSError) as error:
        raise WorkerError("unpack_failed", "The zip cannot be opened: %s" % error)
    names = []
    with archive:
        members = checked_members(archive)
        for info in members:
            target = os.path.join(folder, *re.split(r"[\\/]", info.filename))
            if not is_inside(target, folder):
                raise WorkerError("unpack_failed", "The zip holds an unsafe path: %s" % info.filename)
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
