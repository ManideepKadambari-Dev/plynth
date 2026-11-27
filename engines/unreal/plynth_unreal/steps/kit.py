import os

from .. import importing, ue
from ..context import file_hash
from ..errors import LinkError
from ..registry import step


# Imports the modular kit file into Kit/, one mesh per piece, skipping it when no piece has changed.
@step("kit", 40, "import_failed", applies=lambda ctx: ctx.job["kit"] is not None)
def run(ctx):
    kit = ctx.job["kit"]
    filename = ctx.source(kit["file"])
    if not os.path.isfile(filename):
        raise LinkError("import_failed", "The kit file %s is missing." % filename)
    pieces = list(kit["pieces"])
    if not pieces:
        return
    source_hash = file_hash(filename, "kit", pieces)
    paths = [ctx.kit_folder + "/" + piece for piece in pieces]
    before = [ue.load(path) for path in paths]
    if all(ctx.unchanged(asset, source_hash) for asset in before):
        for asset, path in zip(before, paths):
            ctx.claim(asset, path, "unchanged")
    else:
        importing.import_fbx(filename, ctx.kit_folder, None, pieces)
        for old, path in zip(before, paths):
            ctx.claim(ue.load(path), path, "created" if old is None else "updated", source_hash)
    for piece, path in zip(pieces, paths):
        ctx.mesh_assets[piece] = path
