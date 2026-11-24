import os

from . import Step, count, report
from .. import fills as fills_package
from .. import registry
from ..registry import BUILD_STEPS, OPENING_FILLS

ORDER = 26


# Returns the fill entry that serves an opening type, or None.
def entry_for(opening_type):
    for _, entry in OPENING_FILLS.all():
        if opening_type in entry.types:
            return entry
    return None


# Fits a door or a window into every opening of that type. Skipped when the settings say not to fill openings.
# An opening whose catalog asset file is there is left for the opening_assets step, which builds this fill after
# all when the asset does not load.
def run(ctx):
    registry.discover(fills_package)
    if not ctx.params["settings"]["fill_openings"]:
        return
    asset_files = {a["id"]: a.get("blend_file") for a in ctx.params.get("assets", [])}
    for floor in ctx.params["floors"]:
        for opening in floor["openings"]:
            asset = opening.get("asset")
            if asset and asset_files.get(asset["id"]) and os.path.isfile(asset_files[asset["id"]]):
                continue
            build_fill(ctx, floor, opening)


# Builds the fill of one opening and counts it. Returns true when a fill was made.
def build_fill(ctx, floor, opening):
    entry = entry_for(opening["type"])
    if entry is None:
        return False
    if not opening["inner_outline"]:
        report(ctx, "fill_failed", "warning", "The opening is too small for a frame.", floor["id"], opening["id"])
        return False
    try:
        made = entry.build(ctx, floor, opening)
    except Exception as error:
        report(ctx, "fill_failed", "warning", "The fill could not be made: %s" % error, floor["id"], opening["id"])
        return False
    if made:
        count(ctx, "fills")
    return bool(made)


BUILD_STEPS.register("opening_fills", Step(ORDER, "Fitting doors and windows", run))
