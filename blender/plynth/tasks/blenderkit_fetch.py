import logging
import os
import re
import time

from ..jobs import WorkerError, progress
from ..registry import TASKS
from ..stores import blenderkit

ID_PATTERN = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")
KINDS = ("model", "material")
PAGE_ADDRESS = "https://www.blenderkit.com/asset-gallery-detail/%s/"
OTHER_TAB = {
    "model": "This BlenderKit item is a model. Import it in the Assets tab or with plynth assets import.",
    "material": "This BlenderKit item is a material. Import it in the Materials tab or with plynth materials import.",
}


# Checks the params of the job and returns them as (base id, kind, resolution, output folder, wait seconds).
def check_params(params):
    base_id = params.get("asset_base_id")
    kind = params.get("kind")
    resolution = params.get("resolution")
    output = params.get("output_folder")
    wait = params.get("wait_seconds")
    if not isinstance(base_id, str) or not ID_PATTERN.match(base_id):
        raise WorkerError("bad_job", "The params need an asset_base_id of the form 8-4-4-4-12 lower case hex digits.")
    if kind not in KINDS:
        raise WorkerError("bad_job", "The params need a kind of model or material.")
    if resolution not in blenderkit.RESOLUTIONS:
        raise WorkerError("bad_job", "The params need a resolution of 0.5k, 1k, 2k, 4k, 8k or original.")
    if not isinstance(output, str) or not output:
        raise WorkerError("bad_job", "The params need an output_folder.")
    if isinstance(wait, bool) or not isinstance(wait, int) or not 60 <= wait <= 3600:
        raise WorkerError("bad_job", "The params need wait_seconds between 60 and 3600.")
    return base_id, kind, resolution, output, wait


# Refuses an item that is not the kind the import expects, using what the add-on recorded about it.
def check_kind(addon, base_id, kind):
    data = addon.item_data(base_id)
    actual = str((data or {}).get("assetType") or "").lower()
    if not actual or actual == kind:
        return
    if actual in KINDS:
        raise WorkerError("wrong_asset_kind", OTHER_TAB[actual])
    raise WorkerError("wrong_asset_kind", "This BlenderKit item is a %s. Only models and materials can be imported." %
                      actual)


# Downloads one BlenderKit item through the add-on and copies it into the output folder.
def run(ctx):
    base_id, kind, resolution, output, wait = check_params(ctx.params)

    progress("check_addon", 1, 3, "Checking the BlenderKit add-on")
    addon = blenderkit.open_addon()
    logging.getLogger().addHandler(addon.catcher)
    try:
        blend = download(ctx, addon, base_id, kind, resolution, wait)
    finally:
        logging.getLogger().removeHandler(addon.catcher)

    progress("copy", 3, 3, "Copying the download")
    data = addon.item_data(base_id) or {}
    actual = str(data.get("assetType") or "").lower() or blenderkit.kind_from_blend(blend)
    if actual != kind:
        raise WorkerError("wrong_asset_kind", OTHER_TAB.get(actual, OTHER_TAB["material" if kind == "model" else "model"]))
    file = blenderkit.copy_download(blend, addon.download_folder(), os.path.abspath(output))

    name = str(data.get("displayName") or data.get("name") or "").strip() or None
    author_value = data.get("author")
    author = author_value.get("fullName") if isinstance(author_value, dict) else author_value
    author = str(author).strip() if author else ""
    material = None
    if kind == "material":
        names = blenderkit.material_names(os.path.join(os.path.abspath(output), file))
        if not names:
            raise WorkerError("download_failed", "The downloaded file holds no material (%s)." % addon.label)
        material = name if name in names else names[0]
    ctx.output = {
        "file": file,
        "format": "blend",
        "kind": kind,
        "name": name,
        "author": author or None,
        "license": blenderkit.license_id(data.get("license")),
        "reference": PAGE_ADDRESS % base_id,
        "material": material,
    }


# Starts the download and waits for the new .blend. Returns its path.
def download(ctx, addon, base_id, kind, resolution, wait):
    folder = addon.download_folder()
    if not folder:
        raise WorkerError("download_failed", "The %s has no download folder set." % addon.label)
    progress("download", 2, 3, "Downloading from BlenderKit")
    settle_end = time.time() + blenderkit.SETTLE_SECONDS
    while time.time() < settle_end:
        addon.pump()
        time.sleep(blenderkit.PUMP_SECONDS)
    before = addon.blend_files()
    started = time.time()
    if not addon.start_download(base_id, resolution):
        detail = " ".join(addon.catcher.messages[:2])[:400]
        raise WorkerError("download_failed", "The %s refused the download%s" %
                          (addon.label, (": " + detail) if detail else "."))
    return blenderkit.wait_for_blend(addon, before, started, wait, lambda: check_kind(addon, base_id, kind))


TASKS.register("blenderkit_fetch", run)
