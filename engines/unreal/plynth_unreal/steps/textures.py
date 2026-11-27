import os

from .. import importing, texture_settings, ue
from ..context import file_hash
from ..errors import LinkError
from ..registry import step


# Imports each texture of the job into Textures/ with the compression and sRGB of its channel.
@step("textures", 50, "material_failed", applies=lambda ctx: ctx.pack)
def run(ctx):
    if ctx.job["textures"]:
        ue.make_folder(ctx.textures_folder)
    for texture in ctx.job["textures"]:
        filename = ctx.source(texture["file"])
        if not os.path.isfile(filename):
            raise LinkError("material_failed", "The texture file %s is missing." % filename)
        path = ctx.textures_folder + "/" + texture["asset"]
        source_hash = file_hash(filename, texture["channel"], texture["srgb"])
        asset = ue.load(path)
        if ctx.unchanged(asset, source_hash):
            ctx.claim(asset, path, "unchanged")
            continue
        action = "created" if asset is None else "updated"
        asset = importing.import_texture(filename, ctx.textures_folder, texture["asset"])
        texture_settings.apply(asset, texture["channel"], texture["srgb"])
        ctx.claim(asset, path, action, source_hash)
