from . import QualityCheck, finding, result
from ..registry import QUALITY_CHECKS

ORDER = 20


# Fails for every error of the last build, each finding naming its plan id.
def run(ctx):
    problems = [p for p in ctx.params["build"].get("problems", []) if p.get("severity") == "error"]
    if not problems:
        return result("pass", "The last build had no errors.")
    findings = [finding(p.get("message") or "The build left something out.", None, p.get("object") or None)
                for p in problems]
    return result("fail", "The last build had %d error%s." % (len(problems), "" if len(problems) == 1 else "s"),
                  findings)


QUALITY_CHECKS.register("build_complete", QualityCheck(ORDER, "scene", "Build is complete", run))
