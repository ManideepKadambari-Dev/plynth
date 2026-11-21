import os
from collections import namedtuple

from ..registry import EXPORTERS

# What each exporter module registers: the file extension and a function export(objects, filepath, path_mode).
# objects is a list of objects to write, or None for everything in the scene.
ExporterEntry = namedtuple("ExporterEntry", "extension export")


# Writes a file with one exporter under a temporary name and renames it when done, so a failed export leaves
# nothing behind. Raises whatever the exporter raises, or RuntimeError when no file appeared.
def write_file(exporter_id, objects, target, path_mode):
    entry = EXPORTERS.get(exporter_id)
    if entry is None:
        raise RuntimeError("The exporter '%s' is not installed." % exporter_id)
    base, extension = os.path.splitext(target)
    temporary = base + ".part" + extension
    if os.path.exists(temporary):
        os.remove(temporary)
    try:
        entry.export(objects, temporary, path_mode)
        if not os.path.isfile(temporary):
            raise RuntimeError("The exporter wrote no file.")
        os.replace(temporary, target)
    finally:
        if os.path.exists(temporary):
            os.remove(temporary)
