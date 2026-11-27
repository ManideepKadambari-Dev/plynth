from .. import texture_settings, ue
from ..registry import check


# Checks every texture of the job exists with its channel's compression and sRGB, and that a send without
# Pack with materials left no texture asset in the building folder.
@check("textures", 40)
def run(ctx):
    if not ctx.pack:
        left = [path for path in ue.list_folder(ctx.content_path) if texture_settings.is_texture(ue.load(path))]
        if left:
            return "fail", "Pack with materials is off, but %d textures are still there: %s." % (
                len(left), ", ".join(path.rsplit("/", 1)[-1] for path in left[:10]))
        return "pass", "Pack with materials is off and the building has no texture assets."
    problems = []
    for texture in ctx.job["textures"]:
        asset = ue.load(ctx.textures_folder + "/" + texture["asset"])
        if not texture_settings.is_texture(asset):
            problems.append("%s is missing" % texture["asset"])
            continue
        problem = texture_settings.problem(asset, texture["channel"], texture["srgb"])
        if not problem and texture["channel"] != "base_color" and texture["srgb"]:
            problem = "is a data map with sRGB on"
        if problem:
            problems.append("%s %s" % (texture["asset"], problem))
    if problems:
        return "fail", "%d textures are wrong: %s." % (len(problems), "; ".join(problems[:10]))
    return "pass", "All %d textures are there with the right compression and sRGB." % len(ctx.job["textures"])
