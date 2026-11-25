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
RESOLUTION_PROPS_TO_SERVER = {"512": "resolution_0_5K", "1024": "resolution_1K", "2048": "resolution_2K",
                              "4096": "resolution_4K", "8192": "resolution_8K", "ORIGINAL": "blend"}


# Starts a download of a record through the helper. As the real one, it writes the resolution of the call
# into its copy of the preferences, and an unpack_files of the call too, then copies every argument of the
# call into the request. The hostile changes come after, as an add-on that changed the request would.
def download(asset_data, **kwargs):
    hostile = global_vars.scenario()
    if hostile.get("force_unpack"):
        kwargs["unpack_files"] = True
    prefs = utils.get_preferences_as_dict()
    prefs["resolution"] = kwargs.get("resolution", "original")
    if "unpack_files" in kwargs:
        prefs["unpack_files"] = kwargs["unpack_files"]
    data = {"asset_data": asset_data, "PREFS": prefs, "progress": 0,
            "text": "downloading %s" % asset_data.get("name", "")}
    data.update(kwargs)
    prefs["scene_id"] = "fake-scene"
    data["download_dirs"] = paths.get_download_dirs(asset_data.get("assetType", "model"))
    data.setdefault("downloaders", [])
    data["started_at"] = data["last_report_time"] = time.monotonic()
    if hostile.get("force_resolution"):
        prefs["resolution"] = data["resolution"] = hostile["force_resolution"]
    if hostile.get("force_pref_resolution"):
        prefs["resolution"] = hostile["force_pref_resolution"]
    if hostile.get("drop_resolution"):
        data.pop("resolution", None)
    if hostile.get("extra_download_dir"):
        data["download_dirs"].append(hostile["extra_download_dir"])
    bk_logger.info("download %s with key %s", asset_data.get("assetBaseId"), prefs["api_key"])
    answer = client_lib.asset_download(data)
    download_tasks[answer["task_id"]] = {"asset_data": asset_data, "download_dirs": data["download_dirs"],
                                         "progress": 0, "text": "", "started_at": time.time(),
                                         "last_report_time": time.time(), "resolution": data.get("resolution"),
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


# Takes callables to operators the fetch must close, at register time, so before any seal.
def take_callables():
    global_vars.EARLY_CALLABLES.update({
        "addon_enable": bpy.ops.preferences.addon_enable,
        "package_install_files": bpy.ops.extensions.package_install_files,
        "addon_choice": bpy.ops.scene.blenderkit_addon_choice})


# The calls of one hostile group, by name.
def hostile_group(group):
    import addon_utils
    early = global_vars.EARLY_CALLABLES
    groups = {
        "named": {
            "addon_enable": lambda: bpy.ops.preferences.addon_enable(module="other_addon"),
            "addon_disable": lambda: bpy.ops.preferences.addon_disable(module="blenderkit"),
            "addon_install": lambda: bpy.ops.preferences.addon_install(filepath="other_addon.py"),
            "addon_remove": lambda: bpy.ops.preferences.addon_remove(module="blenderkit"),
            "package_install_files": lambda: bpy.ops.extensions.package_install_files(filepath="other.zip"),
            "package_install": lambda: bpy.ops.extensions.package_install(repo_index=0, pkg_id="other"),
            "package_uninstall": lambda: bpy.ops.extensions.package_uninstall(repo_index=0, pkg_id="other"),
            "addon_choice": lambda: bpy.ops.scene.blenderkit_addon_choice(asset_base_id="x")},
        "early": {
            "addon_enable": lambda: early["addon_enable"](module="other_addon"),
            "package_install_files": lambda: early["package_install_files"](filepath="other.zip"),
            "addon_choice": lambda: early["addon_choice"](asset_base_id="x")},
        "addon_utils": {
            "enable": lambda: addon_utils.enable("other_addon", default_set=True),
            "disable": lambda: addon_utils.disable("blenderkit", default_set=True)},
    }
    return groups.get(group, {})


# Tries what an add-on must not reach in a fetch, noting each call that was not refused.
def hostile_calls(groups):
    for group in groups:
        for name, call in hostile_group(group).items():
            try:
                result = call()
                if result is None or (isinstance(result, set) and "FINISHED" in result):
                    global_vars.note("hostile_call_ran", group=group, name=name)
                else:
                    global_vars.note("hostile_call_refused", group=group, name=name)
            except Exception:
                global_vars.note("hostile_call_refused", group=group, name=name)


class BlenderkitDownloadOperator(bpy.types.Operator):
    bl_idname = "scene.blenderkit_download"
    bl_label = "Download"

    asset_base_id: StringProperty(default="")  # type: ignore[valid-type]
    target_object: StringProperty(default="")  # type: ignore[valid-type]
    max_resolution: IntProperty(default=0)  # type: ignore[valid-type]

    # Finds the item through the search module, leaves edit modes, and starts its download with the arguments
    # and the server's name of the resolution the real one passes, or hands an add-on item to its installer.
    # As the real one it takes the first result without a check, so an empty search raises.
    def execute(self, context):
        if getattr(self, "resolution", None) is not None:
            global_vars.note("operator_resolution", value=self.resolution)
        hostile = global_vars.scenario().get("hostile_calls")
        if hostile:
            hostile_calls(hostile)
        if context.active_object is not None and context.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        results = search.get_search_simple({"asset_base_id": self.asset_base_id}, page_size=1, max_results=1,
                                           api_key=global_vars.PREFS.get("api_key", ""))
        if global_vars.scenario().get("operator_raises"):
            raise KeyError(client_lib.SECRET_TEXT)
        asset_data = results[0]
        if asset_data.get("assetType") == "addon":
            bpy.ops.scene.blenderkit_addon_choice(asset_base_id=asset_data.get("assetBaseId", ""))
            return {"FINISHED"}
        resolution = RESOLUTION_PROPS_TO_SERVER[utils.get_prefs().resolution]
        options = {"cast_parent": "", "target_object": self.target_object, "target_collection": "",
                   "material_target_slot": 0, "model_location": (0.0, 0.0, 0.0), "model_rotation": (0.0, 0.0, 0.0),
                   "replace": False, "replace_resolution": False, "parent": None, "resolution": resolution,
                   "node_x": 0, "node_y": 0, "nodegroup_mode": ""}
        downloader = {"location": options["model_location"], "rotation": options["model_rotation"]}
        download(asset_data, downloaders=[downloader], **options)
        return {"FINISHED"}


class BlenderkitAddonChoiceOperator(bpy.types.Operator):
    bl_idname = "scene.blenderkit_addon_choice"
    bl_label = "Install add-on"

    asset_base_id: StringProperty(default="")  # type: ignore[valid-type]

    # Installs the add-on item. Its marker says it ran.
    def execute(self, context):
        global_vars.note("addon_choice_ran")
        global_vars.mark("addon_choice")
        bpy.ops.extensions.package_install_files(filepath="addon.zip")
        return {"FINISHED"}


CLASSES = (BlenderkitDownloadOperator, BlenderkitAddonChoiceOperator)
