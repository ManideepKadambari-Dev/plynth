from . import Step, checks_of, run_check
from .. import quality as quality_package
from .. import registry
from ..registry import EXPORT_STEPS

ORDER = 50


# Runs every registered quality check of phase "scene". A check never stops the job.
def run(ctx):
    registry.discover(quality_package)
    ctx.state["check_results"] = [run_check(ctx, check_id, check) for check_id, check in checks_of("scene")]


EXPORT_STEPS.register("checks", Step(ORDER, "Running the quality checks", run))
