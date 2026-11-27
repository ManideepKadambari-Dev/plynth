from .. import ue
from ..registry import step


# Deletes this building's assets that the job no longer lists, such as the textures and materials of an
# earlier send with Pack with materials, or the per group meshes after Grouped was turned off. Assets
# without this building's tag are left alone.
@step("cleanup", 110, "import_failed")
def run(ctx):
    for path in ue.list_folder(ctx.content_path):
        if path in ctx.expected:
            continue
        asset = ue.load(path)
        if asset is None or ue.get_tag(asset, ue.TAG_BUILDING) != ctx.building:
            continue
        kind = ue.kind_of(asset)
        del asset
        if ue.delete(path):
            ctx.result.asset(path, kind, "removed")
        else:
            ue.warn("could not delete %s, which this building no longer uses" % path)
    for folder in (ctx.kit_folder, ctx.textures_folder, ctx.materials_folder):
        try:
            ue.delete_folder_if_empty(folder)
        except Exception as error:
            ue.warn("could not remove the empty folder %s: %s" % (folder, error))
