"""Downloads of the fake add-on, under the real names. The operator asks the search for the item and sends
the download to the helper at once. Tasks are keyed by the helper's task id and hold asset_data, the folders,
the whole preferences with the key, and the refresh token. When the helper reports a task finished, the
append step runs: it puts the item in the scene, writes the record of the item, and on any failure deletes
the file. A task the helper stops reporting is dropped after 20 seconds without a record.
"""
import logging
import os
import time

import bpy
from bpy.props import IntProperty, StringProperty

from . import client_lib, global_vars, paths, reports, search, utils

bk_logger = logging.getLogger("blenderkit")
download_tasks = {}
SILENT_SECONDS = 20


# Starts a download of a record through the helper.
def download(asset_data, **kwargs):
    prefs = utils.get_preferences_as_dict()
    hostile = global_vars.scenario()
    if hostile.get("force_unpack"):
        prefs["unpack_files"] = True
    if hostile.get("force_resolution"):
        prefs["resolution"] = hostile["force_resolution"]
    data = {"asset_data": asset_data, "download_dirs": paths.get_download_dirs(asset_data.get("assetType", "model")),
            "PREFS": prefs, "resolution": prefs["resolution"], "unpack_files": prefs["unpack_files"],
            "create_asset_library": prefs["create_asset_library"], "api_key": prefs["api_key"],
            "binary_path": bpy.app.binary_path, "addon_dir": os.path.dirname(os.path.abspath(__file__))}
    if hostile.get("extra_download_dir"):
        data["download_dirs"].append(hostile["extra_download_dir"])
    bk_logger.info("download %s with key %s", asset_data.get("assetBaseId"), prefs["api_key"])
    answer = client_lib.asset_download(data)
    download_tasks[answer["task_id"]] = {"asset_data": asset_data, "download_dirs": data["download_dirs"],
                                         "progress": 0, "text": "", "started_at": time.time(),
                                         "last_report_time": time.time(), "resolution": prefs["resolution"],
                                         "PREFS": prefs, "api_key": prefs["api_key"],
                                         "api_key_refresh": prefs["api_key_refresh"], "kwargs": kwargs}


# Handles one download task of the helper.
def handle_download_task(task):
    entry = download_tasks.get(task.get("task_id"))
    if entry is None:
        return
    entry["last_report_time"] = time.time()
    status = task.get("status")
    if status == "finished":
        paths_found = task.get("result", {}).get("file_paths", [])
        try:
            try_finished_append(asset_data=entry["asset_data"], file_paths=paths_found, **entry["kwargs"])
        finally:
            download_tasks.pop(task["task_id"], None)
    elif status == "error":
        reports.add_report("Download failed", details=str(task.get("message", "")))
        download_tasks.pop(task["task_id"], None)


# Puts the downloaded item in the scene and writes its record. Any failure deletes the file.
def try_finished_append(asset_data, **kwargs):
    file_paths = kwargs.get("file_paths") or []
    global_vars.note("append_ran", file_paths=file_paths)
    if not file_paths:
        return
    file = file_paths[-1]
    try:
        if asset_data.get("assetType") == "material" and kwargs.get("target_object") in (None, ""):
            raise RuntimeError("no target object")
        bpy.ops.wm.append(filepath=file)
        bpy.ops.ed.undo_push(message="add asset")
        used = bpy.context.scene.get("assets used") or {}
        record = {name: asset_data.get(name) for name in ("assetBaseId", "id", "assetType", "name", "displayName")}
        record.update({"files": asset_data.get("files", []), "file_name": file, "resolution": kwargs.get("resolution")})
        used[asset_data.get("assetBaseId")] = record
        bpy.context.scene["assets used"] = used
    except Exception:
        global_vars.note("downloaded_file_deleted", path=file)
        if os.path.exists(file):
            os.remove(file)
        raise


# Drops tasks the helper no longer reports.
def drop_silent_tasks():
    now = time.time()
    for task_id, entry in list(download_tasks.items()):
        if now - entry["last_report_time"] > SILENT_SECONDS:
            global_vars.note("silent_task_dropped", task_id=task_id)
            download_tasks.pop(task_id, None)


# Cancels every download and empties the list.
def cancel_running_downloads(reason=""):
    for task_id in list(download_tasks):
        try:
            client_lib.cancel_download(task_id)
        except ConnectionError:
            pass
    download_tasks.clear()


# Tries what an add-on must not reach in a fetch, noting each call that was not refused.
def hostile_calls():
    import addon_utils
    calls = {"addon_disable": lambda: bpy.ops.preferences.addon_disable(module="blenderkit"),
             "addon_enable": lambda: addon_utils.enable("other_addon"),
             "save_userpref": lambda: bpy.ops.wm.save_userpref(),
             "install": lambda: bpy.ops.extensions.package_install_files(filepath="x.zip"),
             "addon_choice": lambda: bpy.ops.scene.blenderkit_addon_choice(asset_base_id="x")}
    for name, call in calls.items():
        try:
            call()
            global_vars.note("hostile_call_ran", name=name)
        except Exception:
            global_vars.note("hostile_call_refused", name=name)


class BlenderkitDownloadOperator(bpy.types.Operator):
    bl_idname = "scene.blenderkit_download"
    bl_label = "Download"

    asset_base_id: StringProperty(default="")  # type: ignore[valid-type]
    target_object: StringProperty(default="")  # type: ignore[valid-type]
    max_resolution: IntProperty(default=0)  # type: ignore[valid-type]

    # Finds the item, leaves edit modes, and starts its download, or hands an add-on item to its installer.
    def execute(self, context):
        if getattr(self, "resolution", None) is not None:
            global_vars.note("operator_resolution", value=self.resolution)
        if global_vars.scenario().get("hostile_calls"):
            hostile_calls()
        if context.active_object is not None and context.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        asset_data = search.get_asset_by_base_id(self.asset_base_id)
        if asset_data is None:
            reports.add_report("Asset not found")
            return {"CANCELLED"}
        if asset_data.get("assetType") == "addon":
            bpy.ops.scene.blenderkit_addon_choice(asset_base_id=asset_data.get("assetBaseId", ""))
            return {"FINISHED"}
        download(asset_data, target_object=self.target_object, resolution=utils.get_prefs().resolution)
        return {"FINISHED"}


class BlenderkitAddonChoiceOperator(bpy.types.Operator):
    bl_idname = "scene.blenderkit_addon_choice"
    bl_label = "Install add-on"

    asset_base_id: StringProperty(default="")  # type: ignore[valid-type]

    # Installs the add-on item.
    def execute(self, context):
        global_vars.note("addon_choice_ran")
        bpy.ops.extensions.package_install_files(filepath="addon.zip")
        return {"FINISHED"}


CLASSES = (BlenderkitDownloadOperator, BlenderkitAddonChoiceOperator)
