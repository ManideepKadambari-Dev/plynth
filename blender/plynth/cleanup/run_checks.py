from . import Step
from .. import checks as checks_package
from .. import registry
from ..registry import CLEANUP_STEPS

ORDER = 90


# Runs every import check. Checks only add warnings, they never stop the job.
def run(ctx):
    registry.discover(checks_package)
    for _check_id, check in sorted(registry.CHECKS.all(), key=lambda pair: pair[0]):
        check(ctx)


CLEANUP_STEPS.register("run_checks", Step(ORDER, "Checking the result", run))
