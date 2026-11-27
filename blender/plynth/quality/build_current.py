from . import PLAIN, UNREAL, QualityCheck, finding, result
from ..registry import QUALITY_CHECKS

ORDER = 10


# Fails when the plan changed since the last build or there is no build report.
def run(ctx):
    build = ctx.params["build"]
    if not build.get("report_found"):
        return result("fail", "There is no build report. Build the project first.",
                      [finding("No build report was found.")])
    if build.get("plan_changed"):
        return result("fail", "The plan changed since the last build. Build the project again.",
                      [finding("The plan is newer than the build.")])
    return result("pass", "The build is up to date.")


QUALITY_CHECKS.register("build_current", QualityCheck(ORDER, "scene", "Build is up to date", run, (PLAIN, UNREAL)))
