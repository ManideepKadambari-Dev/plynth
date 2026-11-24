import re
import sys

from ..jobs import WorkerError, progress
from ..registry import TASKS
from ..stores import blenderkit

GUARD_MODULE = "plynth_fetch_guard"
UNSAFE_SENTENCE = "The download could not be made safe in this Blender, so it was not started."
UNEXPECTED = "The download failed (%s)."
ID_PATTERN = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")


# Checks the params of the job and returns (base id, kind, resolution, output folder, wait seconds, private folder).
def check_params(params):
    base_id = params.get("asset_base_id")
    kind = params.get("kind")
    resolution = params.get("resolution")
    output = params.get("output_folder")
    wait = params.get("wait_seconds")
    private = params.get("private_folder")
    if not isinstance(base_id, str) or not ID_PATTERN.match(base_id):
        raise WorkerError("bad_job", "The params need an asset_base_id of the form 8-4-4-4-12 lower case hex digits.")
    if kind not in blenderkit.KINDS:
        raise WorkerError("bad_job", "The params need a kind of model or material.")
    if resolution not in blenderkit.RESOLUTIONS:
        raise WorkerError("bad_job", "The params need a resolution of 0.5k, 1k, 2k, 4k, 8k or original.")
    if not isinstance(output, str) or not output:
        raise WorkerError("bad_job", "The params need an output_folder.")
    if isinstance(wait, bool) or not isinstance(wait, int) or not 60 <= wait <= 3600:
        raise WorkerError("bad_job", "The params need wait_seconds between 60 and 3600.")
    if not isinstance(private, str) or not private:
        raise WorkerError("bad_job", "The params need a private_folder.")
    return base_id, kind, resolution, output, wait, private


# Downloads one BlenderKit item through the add-on into the output folder, under the fetch guard. Every refusal
# of the guard and every error nobody expected becomes download_failed with a fixed sentence.
def run(ctx):
    base_id, kind, resolution, output, wait, private = check_params(ctx.params)
    guard = sys.modules.get(GUARD_MODULE)
    error_type = getattr(guard, "GuardError", None)
    if guard is None or not isinstance(error_type, type):
        raise WorkerError("download_failed", UNSAFE_SENTENCE)
    try:
        ctx.output = fetch(guard, base_id, kind, resolution, output, wait, private)
    except error_type as error:
        raise WorkerError("download_failed", str(error))
    except WorkerError:
        raise
    except Exception as error:
        raise WorkerError("download_failed", UNEXPECTED % type(error).__name__)


# The steps of the fetch in their order (BlenderKit.h). Returns the fetch output.
def fetch(guard, base_id, kind, resolution, output, wait, private):
    progress("check_addon", 1, 3, "Checking the BlenderKit add-on")
    guard.require(private)
    item = blenderkit.Fetch(base_id, kind, resolution, private, output)
    item.check_output()
    item.find_addon()
    item.load_parts()
    item.seal(guard)
    item.point_children_inside()
    item.choose_ports()
    item.read_sign_in()
    item.set_preferences()
    item.clear_scene_records()
    guard.require_low_integrity()
    progress("download", 2, 3, "Downloading from BlenderKit")
    item.wait_for_helper()
    item.start_download()
    blend = item.wait_for_file(wait)
    progress("copy", 3, 3, "Copying the download")
    return item.copy_out(blend)


TASKS.register("blenderkit_fetch", run)
