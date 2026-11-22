import re
import traceback
from collections import namedtuple

from .. import quality

# What each export module registers: where it runs in the order, the sentence shown as progress, and a
# function run(ctx).
Step = namedtuple("Step", "order message run")

COLLISION_KIND = "collision"
CHANNEL_WORDS = {"base_color": "BaseColor", "normal": "Normal", "roughness": "Roughness",
                 "metallic": "Metallic", "opacity": "Opacity", "occlusion": "Occlusion"}


# Returns the text with every character other than a letter, digit or "_" replaced by "_".
def sanitize(text):
    return re.sub(r"[^A-Za-z0-9_]", "_", text or "")


# Turns a snake case word into PascalCase, for example paint_a into PaintA.
def pascal(text):
    parts = [p for p in re.split(r"[^A-Za-z0-9]+", text or "") if p]
    return "".join(p[:1].upper() + p[1:] for p in parts)


# Returns the name without the number Blender adds to a repeated name, such as ".001".
def strip_suffix(name):
    return re.sub(r"\.\d{3}$", "", name)


# Returns the name, or the name with _2, _3 and so on when taken (compared without case). Adds it to taken.
def unique_name(name, taken):
    candidate = name
    number = 2
    while candidate.lower() in taken:
        candidate = "%s_%d" % (name, number)
        number += 1
    taken.add(candidate.lower())
    return candidate


# Runs one registered quality check and returns its entry for the result. A check that raises fails with a
# plain sentence, so it never passes by accident.
def run_check(ctx, check_id, check):
    try:
        answer = check.run(ctx)
        status = answer["status"]
        if status not in quality.STATUSES:
            raise ValueError("unknown status %r" % status)
        message = answer.get("message") or "Done."
        findings = list(answer.get("findings") or [])
    except Exception as error:
        traceback.print_exc()
        status, message, findings = "fail", "The check could not run: %s" % error, []
    answer = quality.result(status, message, findings)
    return {"id": check_id, "name": check.name, "phase": check.phase, "status": answer["status"],
            "message": answer["message"], "findings": answer["findings"]}


# Returns the registered checks of one phase as (id, check) pairs, in order.
def checks_of(phase):
    from ..registry import QUALITY_CHECKS
    found = [(check_id, check) for check_id, check in QUALITY_CHECKS.all() if check.phase == phase]
    return sorted(found, key=lambda pair: pair[1].order)
