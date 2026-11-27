import os
import shutil

from .. import ue
from ..errors import LinkError
from ..registry import step


# Copies the gameplay data and license files as they are into Content/Plynth/<building>/Data/ on disk, and
# removes files there that the job no longer lists.
@step("data_files", 90, "import_failed")
def run(ctx):
    folder = ue.disk_path(ctx.data_folder)
    listed = set()
    for name in ctx.job["data_files"]:
        source = ctx.source(name)
        if not os.path.isfile(source):
            raise LinkError("import_failed", "The data file %s is missing." % source)
        os.makedirs(folder, exist_ok=True)
        target = folder + "/" + name
        listed.add(name.lower())
        if not os.path.isfile(target):
            action = "created"
        elif same_bytes(source, target):
            action = "unchanged"
        else:
            action = "updated"
        if action != "unchanged":
            shutil.copyfile(source, target)
        ctx.result.asset(ctx.data_folder + "/" + name, "file", action)
    if not os.path.isdir(folder):
        return
    for name in sorted(os.listdir(folder)):
        target = folder + "/" + name
        if os.path.isfile(target) and name.lower() not in listed:
            os.remove(target)
            ctx.result.asset(ctx.data_folder + "/" + name, "file", "removed")


# Returns True when two files hold the same bytes.
def same_bytes(first, second):
    if os.path.getsize(first) != os.path.getsize(second):
        return False
    with open(first, "rb") as one, open(second, "rb") as two:
        while True:
            a = one.read(1 << 20)
            b = two.read(1 << 20)
            if a != b:
                return False
            if not a:
                return True
