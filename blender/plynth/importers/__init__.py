import os
from collections import namedtuple

from ..registry import IMPORTERS

# What each file format module registers: the file endings it reads and a function load(ctx, path) that
# brings the model into the current scene.
ImporterEntry = namedtuple("ImporterEntry", "suffixes load")


# Returns the id of the importer that reads this file's ending, or None. The zip importer is never returned.
def format_for_file(path):
    suffix = os.path.splitext(path)[1].lower()
    for entry_id, entry in IMPORTERS.all():
        if entry_id != "zip" and suffix in entry.suffixes:
            return entry_id
    return None
