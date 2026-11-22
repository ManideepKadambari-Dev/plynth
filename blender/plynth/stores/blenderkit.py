"""Every touch point with the BlenderKit add-on, in one place.

The worker only uses what the add-on itself offers inside Blender: its download operator, its timer functions
and its preferences. It never calls a BlenderKit web address and never reads out the API key; the only question
asked about the key is whether one is stored. The add-on's version goes into every error message because these
names are not a published interface and can change with an add-on update.
"""
import importlib
import json
import logging
import os
import shutil
import time

import bpy

from ..jobs import WorkerError

ADDON_NAME = "blenderkit"
OPERATOR = "blenderkit_download"
SETTLE_SECONDS = 5.0
PUMP_SECONDS = 0.25
SCAN_EVERY = 4
STABLE_SCANS = 3
SKIPPED_FOLDERS = {"client", "temp", "tmp", "thumbnails", "__pycache__"}
RESOLUTIONS = {"0.5k": "512", "1k": "1024", "2k": "2048", "4k": "4096", "8k": "8192", "original": "ORIGINAL"}
LICENSES = {"royalty_free": "royalty_free", "cc_zero": "cc0", "cc0": "cc0"}
TIMER_FUNCTIONS = (("timer", "client_communication_timer"), ("tasks_queue", "queue_worker"),
                   ("bg_blender", "bg_update"))


# Returns the last dotted part of a module name, so bl_ext.user_default.blenderkit gives blenderkit.
def short_name(module_name):
    return module_name.rsplit(".", 1)[-1]


# Lists the add-on modules Blender knows, keyed by module name. Empty when they cannot be listed.
def installed_modules():
    try:
        import addon_utils
        return {module.__name__: module for module in addon_utils.modules()}
    except Exception:
        return {}


# Reads the add-on's version as text, or "unknown".
def version_of(module):
    try:
        import addon_utils
        version = (addon_utils.module_bl_info(module) or {}).get("version")
        if isinstance(version, (tuple, list)):
            return ".".join(str(part) for part in version[:3])
        if version:
            return str(version)
    except Exception:
        pass
    return "unknown"


class ErrorCatcher(logging.Handler):
    # Collects warnings and errors that the add-on logs while a download runs.
    def __init__(self):
        super().__init__(level=logging.WARNING)
        self.messages = []

    # Keeps the text of one log record when it comes from the add-on.
    def emit(self, record):
        if ADDON_NAME in record.name:
            text = record.getMessage().strip()
            if text and text not in self.messages:
                self.messages.append(text)


class Addon:
    # Holds the module name of the enabled add-on and its version.
    def __init__(self, module_name, version):
        self.module_name = module_name
        self.version = version
        self.catcher = ErrorCatcher()
        self._submodules = {}

    # A short text naming the add-on and its version, for error messages.
    @property
    def label(self):
        return "BlenderKit add-on %s" % self.version

    # Returns the add-on's preferences object, or None.
    def preferences(self):
        entry = bpy.context.preferences.addons.get(self.module_name)
        return getattr(entry, "preferences", None)

    # Says whether a key is stored. The key itself is never kept or printed.
    def signed_in(self):
        try:
            return bool(str(getattr(self.preferences(), "api_key", "")).strip())
        except Exception:
            return False

    # Asks the add-on to load the sign in it saved on disk. Blender runs in the background here, so the add-on's own
    # start up timer, which does this, never runs. The add-on's loader deletes a preferences file it cannot parse, so
    # the file is parsed here first, read only, and the loader is called only when it is a valid JSON object.
    # Returns a short note when the file was left alone because it could not be read, else None.
    def load_saved_sign_in(self):
        module = self.submodule("persistent_preferences")
        function = getattr(module, "load_preferences_from_JSON", None)
        where = getattr(module, "get_preferences_path", None)
        if function is None or where is None:
            return None
        try:
            path = where()
            if not os.path.isfile(path):
                return None
            with open(path, "r", encoding="utf-8") as handle:
                data = json.load(handle)
        except Exception:
            return "The saved BlenderKit settings file could not be read and was left as it is."
        if not isinstance(data, dict):
            return "The saved BlenderKit settings file is not in the expected form and was left as it is."
        try:
            function()
        except Exception as error:
            self.catcher.messages.append("load_preferences_from_JSON raised %s" % type(error).__name__)
        return None

    # Returns the add-on's download folder as an absolute path, or empty text.
    def download_folder(self):
        folder = str(getattr(self.preferences(), "global_dir", "") or "")
        return os.path.abspath(bpy.path.abspath(folder)) if folder else ""

    # Imports one submodule of the add-on, or returns None.
    def submodule(self, short):
        if short not in self._submodules:
            try:
                self._submodules[short] = importlib.import_module("%s.%s" % (self.module_name, short))
            except Exception:
                self._submodules[short] = None
        return self._submodules[short]

    # Runs the add-on's own timer functions once. Blender does not run them in background mode, and a script
    # that blocks the main loop starves them, so the worker runs them by hand.
    def pump(self):
        for module_name, function_name in TIMER_FUNCTIONS:
            function = getattr(self.submodule(module_name), function_name, None)
            if function is None:
                continue
            try:
                function()
            except Exception as error:
                self.catcher.messages.append("%s.%s raised %s" % (module_name, function_name, type(error).__name__))

    # Returns how many downloads the add-on says are running, or None when it cannot tell.
    def active_downloads(self):
        tasks = getattr(self.submodule("download"), "download_tasks", None)
        try:
            return len(tasks) if tasks is not None else None
        except Exception:
            return None

    # Asks the add-on's own operator to download the item. Returns false when the operator refused.
    def start_download(self, base_id, resolution):
        operator = getattr(bpy.ops.scene, OPERATOR, None)
        if operator is None:
            raise WorkerError("addon_missing", "The %s has no download operator. Update the add-on in Blender." %
                              self.label)
        try:
            properties = operator.get_rna_type().properties.keys()
        except Exception:
            properties = []
        extra = {}
        if resolution in RESOLUTIONS and "resolution" in properties:
            extra["resolution"] = RESOLUTIONS[resolution]
            if "use_resolution_operator" in properties:
                extra["use_resolution_operator"] = True
            if "max_resolution" in properties:
                extra["max_resolution"] = 8192
        for attempt in (extra, {}):
            try:
                result = operator(asset_base_id=base_id, **attempt)
                return "CANCELLED" not in result
            except TypeError:
                if not attempt:
                    raise
            except Exception as error:
                raise WorkerError("download_failed", "The %s could not start the download: %s: %s" %
                                  (self.label, type(error).__name__, str(error)[:200]))
        return False

    # Returns what the add-on recorded about an item in the scene, as a plain dict, or None.
    def item_data(self, base_id):
        try:
            entry = (bpy.context.scene.get("assets used") or {}).get(base_id)
            if entry is None:
                return None
            return entry.to_dict() if hasattr(entry, "to_dict") else dict(entry)
        except Exception:
            return None

    # Lists the .blend files in the download folder with their times and sizes, down to three folders deep.
    def blend_files(self):
        found = {}
        root = self.download_folder()

        def walk(folder, depth):
            try:
                entries = list(os.scandir(folder))
            except OSError:
                return
            for entry in entries:
                try:
                    if entry.is_dir(follow_symlinks=False):
                        if depth < 3 and entry.name.lower() not in SKIPPED_FOLDERS:
                            walk(entry.path, depth + 1)
                    elif entry.name.lower().endswith(".blend"):
                        stat = entry.stat()
                        found[os.path.abspath(entry.path)] = (stat.st_mtime_ns, stat.st_size)
                except OSError:
                    continue

        if root and os.path.isdir(root):
            walk(root, 0)
        return found


# Finds the enabled add-on and checks that it is signed in. Raises addon_missing or not_signed_in.
def open_addon():
    modules = installed_modules()
    installed = [name for name in modules if short_name(name) == ADDON_NAME]
    enabled = [name for name in bpy.context.preferences.addons.keys() if short_name(name) == ADDON_NAME]
    if not enabled:
        if installed:
            raise WorkerError("addon_missing", "The BlenderKit add-on is installed but not enabled. Open Blender, "
                              "go to Preferences, Add-ons and enable BlenderKit.")
        raise WorkerError("addon_missing", "The BlenderKit add-on is not installed. Install it in Blender from "
                          "Preferences, Get Extensions, and sign in.")
    name = enabled[0]
    addon = Addon(name, version_of(modules.get(name)))
    note = None
    if not addon.signed_in():
        note = addon.load_saved_sign_in()
    if not addon.signed_in():
        raise WorkerError("not_signed_in", "Sign in to BlenderKit inside Blender first: open Blender, go to "
                          "Preferences, Add-ons, BlenderKit, and log in.%s" % ((" " + note) if note else ""))
    return addon


# Maps the add-on's license word to a license id of the catalogs, or None.
def license_id(text):
    return LICENSES.get(str(text or "").strip().lower())


# Reads the names of the materials in a .blend without loading anything.
def material_names(blend):
    try:
        with bpy.data.libraries.load(blend) as (data_from, _data_to):
            return list(data_from.materials)
    except Exception:
        return []


# Says what a .blend holds when the add-on did not record the kind: a material when it has materials and no
# objects, a model otherwise.
def kind_from_blend(blend):
    try:
        with bpy.data.libraries.load(blend) as (data_from, _data_to):
            return "material" if data_from.materials and not data_from.objects else "model"
    except Exception:
        return "model"


# Copies the downloaded .blend and the files beside it into output_folder, keeping the folder layout so
# relative texture paths still work. Never changes or removes anything in the add-on's folder.
def copy_download(blend, download_folder, output_folder):
    os.makedirs(output_folder, exist_ok=True)
    source = os.path.dirname(blend)
    shutil.copyfile(blend, os.path.join(output_folder, os.path.basename(blend)))
    if os.path.normcase(source) == os.path.normcase(download_folder):
        return os.path.basename(blend)
    for folder, folders, files in os.walk(source):
        folders[:] = [name for name in folders if name.lower() not in SKIPPED_FOLDERS]
        relative = os.path.relpath(folder, source)
        for name in files:
            if name.lower().endswith((".blend", ".blend1", ".tmp", ".part")):
                continue
            target = os.path.normpath(os.path.join(output_folder, relative, name))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copyfile(os.path.join(folder, name), target)
    return os.path.basename(blend)


# Waits for the download to finish and returns the path of the new .blend. check is called on every round and
# may raise. Raises download_failed or download_timeout.
def wait_for_blend(addon, before, started, wait_seconds, check):
    stable = {}
    tick = 0
    while True:
        addon.pump()
        time.sleep(PUMP_SECONDS)
        tick += 1
        elapsed = time.time() - started
        check()
        if tick % SCAN_EVERY == 0:
            now = addon.blend_files()
            arrived = {path: stat for path, stat in now.items() if before.get(path) != stat}
            for path in list(stable):
                if path not in arrived or arrived[path] != stable[path][0]:
                    del stable[path]
            for path, stat in arrived.items():
                stable[path] = (stat, stable[path][1] + 1 if path in stable else 1)
            active = addon.active_downloads()
            ready = [path for path, (_stat, count) in stable.items()
                     if count >= STABLE_SCANS and (active in (0, None) or count >= STABLE_SCANS * 8)]
            if ready:
                return max(ready, key=lambda path: now[path][0])
            if not arrived and active == 0 and (elapsed > 15 or (addon.catcher.messages and elapsed > 3)):
                detail = " ".join(addon.catcher.messages[:2])[:400]
                raise WorkerError("download_failed", "The %s did not deliver the item%s Check the item in "
                                  "BlenderKit inside Blender." % (addon.label, (": " + detail) if detail else "."))
        if elapsed > wait_seconds:
            raise WorkerError("download_timeout", "Nothing arrived from BlenderKit after %d seconds (%s). Try "
                              "again, or check the item in BlenderKit inside Blender." %
                              (wait_seconds, addon.label))
