import os

from .. import importing, ue
from ..context import file_hash
from ..errors import LinkError
from ..registry import step


# Imports each mesh file of the job into Meshes/ over its asset path, skipping a file that has not changed.
@step("meshes", 30, "import_failed")
def run(ctx):
    for mesh in ctx.job["meshes"]:
        filename = ctx.source(mesh["file"])
        if not os.path.isfile(filename):
            raise LinkError("import_failed", "The mesh file %s is missing." % filename)
        path = ctx.mesh_path(mesh["asset"])
        source_hash = file_hash(filename, "mesh")
        asset = ue.load(path)
        if ctx.unchanged(asset, source_hash):
            ctx.claim(asset, path, "unchanged")
        else:
            action = "created" if asset is None else "updated"
            importing.import_fbx(filename, ctx.meshes_folder, mesh["asset"], [mesh["asset"]])
            asset = ue.load(path)
            ctx.claim(asset, path, action, source_hash)
        ctx.mesh_assets[mesh["asset"]] = path
