import os
import shutil

from .. import registry
from ..jobs import WorkerError, progress
from ..materials import CHANNELS, DEFAULT_TILE, Found, read_size, thumbnail as material_thumbnail
from ..materials import bakers, checks, sources
from ..registry import TASKS
from ..shading import MapError

STEPS = (
    ("load_source", "Reading the source"),
    ("make_maps", "Making the maps"),
    ("checks", "Checking the maps"),
    ("render_thumbnail", "Rendering the thumbnail"),
)
BAKE_SIZES = (256, 512, 1024, 2048, 4096)


# Says whether value is a number from low to high.
def is_number(value, low, high):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and low <= value <= high


# Checks the params of the job. A missing or wrong field is a bad_job.
def check_params(params):
    source = params.get("source")
    if not isinstance(source, dict) or not isinstance(source.get("kind"), str):
        raise WorkerError("bad_job", "The job has no material source.")
    if not isinstance(source.get("path"), str) or not source["path"]:
        raise WorkerError("bad_job", "The job has no source path.")
    for key in ("maps", "values"):
        if not isinstance(source.get(key, {}), dict):
            raise WorkerError("bad_job", "The source %s must be an object." % key)
    if source.get("normal", "auto") not in ("auto", "opengl", "directx"):
        raise WorkerError("bad_job", "The normal convention must be auto, opengl or directx.")
    if not isinstance(params.get("output_folder"), str) or not params["output_folder"]:
        raise WorkerError("bad_job", "The job has no output_folder.")
    tile = params.get("tile_size")
    if tile is not None and not (isinstance(tile, dict) and is_number(tile.get("width"), 0.01, 100)
                                 and is_number(tile.get("height"), 0.01, 100)):
        raise WorkerError("bad_job", "The tile size must be between 0.01 and 100 m.")
    if params.get("bake_size", 1024) not in BAKE_SIZES:
        raise WorkerError("bad_job", "The bake size must be 256, 512, 1024, 2048 or 4096.")
    if not isinstance(params.get("thumbnail"), dict):
        raise WorkerError("bad_job", "The job has no thumbnail settings.")


# Finds the source entry that reads this kind of source.
def find_source(kind):
    for _, entry in registry.MATERIAL_SOURCES.all():
        if kind in entry.kinds:
            return entry
    raise WorkerError("unsupported_format", "Materials from '%s' are not supported." % kind)


# Makes every channel's map with the first baker that can, in the order of the bakers. Returns the entries
# by channel and the absolute path of each map that exists.
def make_maps(ctx, plans, folder):
    textures = os.path.join(folder, "textures")
    os.makedirs(textures, exist_ok=True)
    ordered = [entry for _, entry in sorted(registry.BAKERS.all(), key=lambda pair: pair[1].order)]
    maps = {}
    paths = {}
    for channel in CHANNELS:
        plan = plans.get(channel)
        entry = None
        if plan is not None:
            baker = next((b for b in ordered if b.can_bake(ctx, plan)), None)
            if baker is None:
                raise WorkerError("bake_failed", "No baker can make the %s map." % channel)
            try:
                entry = baker.bake(ctx, plan, textures)
            except ValueError as error:
                raise WorkerError("import_failed", str(error))
            except OSError as error:
                raise WorkerError("save_failed", "The %s map could not be written: %s" % (channel, error))
        if entry is None:
            entry = {"file": None, "from": "none", "detail": ""}
        maps[channel] = entry
        if entry["file"]:
            paths[channel] = os.path.join(folder, *entry["file"].split("/"))
    return maps, paths


# Returns true when the thumbnail settings can be used.
def thumbnail_spec_ok(spec):
    background = spec.get("background")
    return (isinstance(spec.get("size"), int) and not isinstance(spec.get("size"), bool) and 64 <= spec["size"] <= 2048
            and isinstance(background, list) and len(background) == 3
            and all(is_number(c, 0, 1) for c in background))


# Renders thumb.png into the folder. A failure is reported in the result and does not fail the job.
def render_thumbnail(folder, spec, paths, tile_size):
    if not spec.get("render", True):
        return {"file": None, "engine": None, "error": None}
    if not thumbnail_spec_ok(spec):
        return {"file": None, "engine": None, "error": "The thumbnail settings are not valid."}
    target = os.path.join(folder, "thumb.png")
    try:
        engine = material_thumbnail.render(target, spec, paths, tile_size)
        return {"file": "thumb.png", "engine": engine, "error": None}
    except (WorkerError, MapError) as error:
        message = error.message if isinstance(error, WorkerError) else str(error)
    except Exception as error:
        message = "%s: %s" % (type(error).__name__, error)
    if os.path.exists(target + ".part"):
        os.remove(target + ".part")
    return {"file": None, "engine": None, "error": message}


# Makes the maps of one material from its source, checks them and renders its thumbnail.
def run(ctx):
    check_params(ctx.params)
    for package in (sources, bakers, checks):
        registry.discover(package)
    source = ctx.params["source"]
    folder = ctx.params["output_folder"]
    entry = find_source(source["kind"])
    count = len(STEPS)
    try:
        progress(STEPS[0][0], 1, count, STEPS[0][1])
        loaded = entry.load(ctx, source)
        tile = ctx.params.get("tile_size") or loaded.tile_size or {"width": DEFAULT_TILE, "height": DEFAULT_TILE}
        tile = {"width": float(tile["width"]), "height": float(tile["height"])}

        progress(STEPS[1][0], 2, count, STEPS[1][1])
        os.makedirs(folder, exist_ok=True)
        maps, paths = make_maps(ctx, loaded.plans, folder)

        progress(STEPS[2][0], 3, count, STEPS[2][1])
        found = Found(maps, paths, tile)
        for _, check in sorted(registry.MATERIAL_CHECKS.all(), key=lambda pair: pair[1].order):
            try:
                check.run(ctx, found)
            except ValueError as error:
                ctx.warn("missing_texture", str(error))

        progress(STEPS[3][0], 4, count, STEPS[3][1])
        thumb = render_thumbnail(folder, ctx.params["thumbnail"], paths, tile)
        width, height = read_size(paths["base_color"])
    finally:
        for cleanup in ctx.state.get("cleanups", []):
            cleanup()
        for temporary in ctx.state.get("temp_folders", []):
            shutil.rmtree(temporary, ignore_errors=True)

    ctx.output = {
        "maps": maps,
        "texture_size": {"width": width, "height": height},
        "tile_size": tile,
        "picked_material": loaded.picked_material,
        "source_info": loaded.source_info,
        "thumbnail": thumb,
    }


TASKS.register("cleanup_material", run)
