from collections import namedtuple

MAX_FINDINGS = 50
STATUSES = ("pass", "warning", "fail", "skipped")

# What each quality check module registers: where it runs in the order, the phase ("scene" or "file"), the
# name shown to the user, a function run(ctx) that returns the dictionary made by result(), and the ids of the
# export profiles it runs for (exporting/profiles/). A check runs only for the profiles it names.
QualityCheck = namedtuple("QualityCheck", "order phase name run profiles")

PLAIN = "plain"
UNREAL = "unreal"


# Makes one finding. The object is the exported object or slot it is about, the plan id is what the app can
# select. Either can be None.
def finding(message, obj=None, plan_id=None):
    return {"object": obj, "plan_id": plan_id, "message": message}


# Makes the answer of a check: status, one plain sentence and the findings. At most 50 findings are kept and
# the message then says how many there were.
def result(status, message, findings=None):
    findings = list(findings or [])
    if len(findings) > MAX_FINDINGS:
        message = "%s (%d findings, the first %d are listed.)" % (message, len(findings), MAX_FINDINGS)
        findings = findings[:MAX_FINDINGS]
    return {"status": status, "message": message, "findings": findings}
