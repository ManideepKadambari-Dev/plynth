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
def run(ctx):
    registry.discover(fills_package)
    if not ctx.params["settings"]["fill_openings"]:
        return
    for floor in ctx.params["floors"]:
        for opening in floor["openings"]:
            entry = entry_for(opening["type"])
            if entry is None:
                continue
            if not opening["inner_outline"]:
                report(ctx, "fill_failed", "warning", "The opening is too small for a frame.", floor["id"], opening["id"])
                continue
            try:
                made = entry.build(ctx, floor, opening)
            except Exception as error:
                report(ctx, "fill_failed", "warning", "The fill could not be made: %s" % error, floor["id"],
                       opening["id"])
                continue
            if made:
                count(ctx, "fills")


BUILD_STEPS.register("opening_fills", Step(ORDER, "Fitting doors and windows", run))
