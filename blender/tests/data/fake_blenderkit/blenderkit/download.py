import json
import logging
import os
import shutil
import time

import bpy
from bpy.props import BoolProperty, EnumProperty, IntProperty, StringProperty

bk_logger = logging.getLogger(__name__)

MODEL_ID = "11111111-1111-4111-8111-111111111111"
MATERIAL_ID = "22222222-2222-4222-8222-222222222222"
HDR_ID = "33333333-3333-4333-8333-333333333333"
ERROR_ID = "44444444-4444-4444-8444-444444444444"
NEVER_ID = "55555555-5555-4555-8555-555555555555"

ITEMS = {
    MODEL_ID: ("model", "Fake chair", "royalty_free", "models"),
    MATERIAL_ID: ("material", "Fake oak", "cc_zero", "materials"),
    HDR_ID: ("hdr", "Fake sky", "royalty_free", "hdrs"),
    ERROR_ID: ("model", "Broken chair", "royalty_free", "models"),
    NEVER_ID: ("model", "Slow chair", "royalty_free", "models"),
}
STEPS_TO_FINISH = 3

download_tasks = {}


# Appends one line about a request to the file named by FAKE_BLENDERKIT_LOG, for the tests to read.
def record(entry):
    path = os.environ.get("FAKE_BLENDERKIT_LOG")
    if path:
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(json.dumps(entry) + "\n")


# Copies the files the test prepared into the download folder, the way a finished download would appear.
def deliver(base_id, resolution):
    kind, name, _license, folder = ITEMS[base_id]
    root = bpy.context.preferences.addons[__package__].preferences.global_dir
    target = os.path.join(bpy.path.abspath(root), folder, "%s_%s" % (name.replace(" ", "_"), base_id[:8]))
    source = os.environ.get("FAKE_BLENDERKIT_SOURCE", "")
    os.makedirs(target, exist_ok=True)
    if kind == "model" and base_id == MODEL_ID:
        shutil.copyfile(os.path.join(source, "chair.blend"), os.path.join(target, "Fake_chair_%s.blend" % resolution))
        shutil.copytree(os.path.join(source, "textures"), os.path.join(target, "textures"), dirs_exist_ok=True)
    elif kind == "material":
        shutil.copyfile(os.path.join(source, "material.blend"), os.path.join(target, "Fake_oak.blend"))
    else:
        with open(os.path.join(target, "sky.hdr"), "wb") as handle:
            handle.write(b"FAKEHDR")


# Moves every running fake download one step on and finishes the ones that are due.
def step_all():
    for base_id in list(download_tasks):
        task = download_tasks[base_id]
        if base_id == NEVER_ID:
            continue
        task["steps"] += 1
        if task["steps"] < STEPS_TO_FINISH:
            continue
        del download_tasks[base_id]
        if base_id == ERROR_ID:
            bk_logger.error("The server returned an error for this fake item.")
        else:
            deliver(base_id, task["resolution"])


class BlenderkitDownloadOperator(bpy.types.Operator):
    bl_idname = "scene.blenderkit_download"
    bl_label = "Download"
    bl_options = {"REGISTER", "INTERNAL"}

    asset_base_id: StringProperty(name="Asset base Id", default="")  # type: ignore[valid-type]
    use_resolution_operator: BoolProperty(default=False, options={"SKIP_SAVE"})  # type: ignore[valid-type]
    resolution: EnumProperty(  # type: ignore[valid-type]
        items=(("512", "512", ""), ("1024", "1024", ""), ("2048", "2048", ""), ("4096", "4096", ""),
               ("8192", "8192", ""), ("ORIGINAL", "Original", "")), default="ORIGINAL", options={"SKIP_SAVE"})
    max_resolution: IntProperty(default=0)  # type: ignore[valid-type]

    # Starts a fake download of the item, or refuses a base id the fake does not know.
    def execute(self, context):
        preferences = context.preferences.addons[__package__].preferences
        if not preferences.api_key:
            bk_logger.error("Not signed in.")
            return {"CANCELLED"}
        item = ITEMS.get(self.asset_base_id)
        if item is None:
            bk_logger.error("No such fake item.")
            return {"CANCELLED"}
        resolution = self.resolution if self.use_resolution_operator else preferences.resolution
        record({"asset_base_id": self.asset_base_id, "resolution": resolution,
                "use_resolution_operator": self.use_resolution_operator, "max_resolution": self.max_resolution,
                "time": time.time()})
        kind, name, license_word, _folder = item
        scene = context.scene
        used = scene.get("assets used", {})
        used[self.asset_base_id] = {"assetBaseId": self.asset_base_id, "assetType": kind, "displayName": name,
                                    "author": {"fullName": "Fake Author"}, "license": license_word}
        scene["assets used"] = used
        download_tasks[self.asset_base_id] = {"steps": 0, "resolution": resolution}
        return {"FINISHED"}


# Registers the operator.
def register_download():
    bpy.utils.register_class(BlenderkitDownloadOperator)


# Removes the operator.
def unregister_download():
    bpy.utils.unregister_class(BlenderkitDownloadOperator)
