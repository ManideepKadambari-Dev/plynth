from . import Step, checks_of, run_check
from ..registry import EXPORT_STEPS

ORDER = 70


# Runs every registered check of phase "file" on the written files. When nothing was written each one is
# reported as skipped.
def run(ctx):
    results = []
    for check_id, check in checks_of("file"):
        if ctx.state.get("written"):
            results.append(run_check(ctx, check_id, check))
        else:
            results.append({"id": check_id, "name": check.name, "phase": check.phase, "status": "skipped",
                            "message": "Nothing was written, so the files were not checked.", "findings": []})
    ctx.state["check_results"] = ctx.state.get("check_results", []) + results


EXPORT_STEPS.register("file_checks", Step(ORDER, "Checking the written files", run))
