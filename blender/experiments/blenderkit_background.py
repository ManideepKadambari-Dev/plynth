"""Owner run test: can the BlenderKit add-on download an asset when Blender has no window?

Run it in Blender after signing in to BlenderKit once in a normal Blender window. It only uses the add-on's own
operator and functions. It never calls a BlenderKit web address itself and never reads out the API key; the
only thing it asks is whether a key is stored. See blender/experiments/README.md.
"""
import argparse
import importlib
import json
import logging
import os
import shutil
import sys
import time

import bpy

ADDON_NAME = "blenderkit"
POLL_SECONDS = 0.25
START_DELAY_SECONDS = 5.0


# Returns the text after the "--" in the command line.
def script_args():
    argv = sys.argv
    return argv[argv.index("--") + 1:] if "--" in argv else []


# Reads the options of this experiment.
def parse_args():
    parser = argparse.ArgumentParser(prog="blenderkit_background.py")
    parser.add_argument("--asset-base-id", required=True, help="The asset base id from the asset's BlenderKit page.")
    parser.add_argument("--out", required=True, help="A folder for report.json and the downloaded file.")
    parser.add_argument("--timeout", type=float, default=300.0, help="Seconds to wait for the download.")
    return parser.parse_args(script_args())


class ErrorCatcher(logging.Handler):
    # Collects warning and error messages that the add-on logs.
    def __init__(self):
        super().__init__(level=logging.WARNING)
        self.messages = []

    # Keeps the text of one log record when it comes from the add-on.
    def emit(self, record):
        if ADDON_NAME in record.name:
            self.messages.append("%s: %s" % (record.levelname, record.getMessage()))


class Experiment:
    # Sets up the report and the places the experiment writes to.
    def __init__(self, asset_base_id, out_folder, timeout):
        self.asset_base_id = asset_base_id
        self.out = os.path.abspath(out_folder)
        self.timeout = timeout
        self.started = time.time()
        self.download_started = None
        self.module = None
        self.finished = False
        self.looked = False
        self.catcher = ErrorCatcher()
        logging.getLogger().addHandler(self.catcher)
        self.report = {
            "asset_base_id": asset_base_id,
            "blender_version": ".".join(str(part) for part in bpy.app.version),
            "background": bool(bpy.app.background),
            "addon_module": None,
            "addon_version": None,
            "signed_in": False,
            "steps": [],
            "file_arrived": False,
            "files": [],
            "seconds": None,
            "result": "not_started",
            "errors": [],
        }

    # Records one step with whether it worked and a short detail.
    def step(self, name, ok, detail=""):
        self.report["steps"].append({"step": name, "ok": bool(ok), "detail": str(detail),
                                     "at_seconds": round(time.time() - self.started, 2)})

    # Records an error text.
    def error(self, text):
        if str(text) not in self.report["errors"]:
            self.report["errors"].append(str(text))

    # Writes report.json through a temporary file and a rename.
    def write_report(self):
        os.makedirs(self.out, exist_ok=True)
        self.report["errors"].extend(m for m in self.catcher.messages if m not in self.report["errors"])
        target = os.path.join(self.out, "report.json")
        with open(target + ".tmp", "w", encoding="utf-8") as handle:
            json.dump(self.report, handle, indent=2)
        os.replace(target + ".tmp", target)

    # Stops the experiment with a result word and writes the report.
    def finish(self, result):
        if self.finished:
            return
        self.finished = True
        self.report["result"] = result
        self.report["seconds"] = round(time.time() - self.started, 2)
        self.write_report()
        print("BlenderKit experiment finished: %s. Report: %s" % (result, os.path.join(self.out, "report.json")))

    # Looks for the add-on and records its module name and version. Returns false when it is not installed.
    def find_addon(self):
        if self.looked:
            return self.report["addon_module"] is not None
        self.looked = True
        import addon_utils
        modules = {m.__name__: m for m in addon_utils.modules()}
        names = [n for n in list(bpy.context.preferences.addons.keys()) + list(modules)
                 if n.rsplit(".", 1)[-1] == ADDON_NAME]
        if not names:
            self.step("find_addon", False, "add-on not installed")
            return False
        name = names[0]
        self.report["addon_module"] = name
        try:
            info = addon_utils.module_bl_info(modules[name]) or {}
            version = info.get("version")
            self.report["addon_version"] = ".".join(str(p) for p in version) if isinstance(version, (tuple, list)) \
                else str(version or "")
        except Exception as error:
            self.error("Could not read the add-on version: %s" % error)
        self.step("find_addon", True, name)
        return True

    # Checks that the add-on is enabled in the user's settings. It is never switched on here, so saved
    # preferences stay as they are.
    def enable_addon(self):
        enabled = self.report["addon_module"] in bpy.context.preferences.addons
        self.step("addon_enabled", enabled, "enabled" if enabled else "not enabled, enable it in Preferences first")
        return enabled

    # Says only whether an API key is stored. The key itself is never kept or printed.
    def check_signed_in(self):
        addon = bpy.context.preferences.addons.get(self.report["addon_module"])
        signed_in = False
        try:
            signed_in = bool(str(getattr(addon.preferences, "api_key", "")).strip())
        except Exception as error:
            self.error("Could not check the sign in: %s" % error)
        self.report["signed_in"] = signed_in
        self.step("signed_in", signed_in, "a key is stored" if signed_in else "no key stored, sign in first")
        return signed_in

    # Imports one module of the add-on, or returns None.
    def addon_module(self, short):
        try:
            return importlib.import_module("%s.%s" % (self.report["addon_module"], short))
        except Exception as error:
            self.error("Could not load the add-on module %s: %s" % (short, error))
            return None

    # Asks the add-on's own download operator to fetch the asset by its base id.
    def start_download(self):
        operator = getattr(bpy.ops.scene, "blenderkit_download", None)
        if operator is None:
            self.step("start_download", False, "the add-on has no scene.blenderkit_download operator")
            return False
        self.download_started = time.time()
        try:
            result = operator(asset_base_id=self.asset_base_id)
        except Exception as error:
            self.error("The download operator failed: %s" % error)
            self.step("start_download", False, "operator raised an error")
            return False
        self.step("start_download", "CANCELLED" not in result, "operator returned %s" % sorted(result))
        return "CANCELLED" not in result

    # Runs the add-on's own timer functions once. In a window Blender runs them itself, in background mode it does not.
    def pump(self):
        if not self.report["background"] or not self.report["signed_in"]:
            return
        for module_name, function in (("timer", "client_communication_timer"), ("tasks_queue", "queue_worker"),
                                      ("bg_blender", "bg_update")):
            module = self.addon_module(module_name)
            function = getattr(module, function, None)
            if function is None:
                continue
            try:
                function()
            except Exception as error:
                self.error("%s.%s raised: %s" % (module_name, function.__name__, error))

    # Returns the number of downloads the add-on says are still running, or None when it cannot tell.
    def active_downloads(self):
        module = self.addon_module("download")
        tasks = getattr(module, "download_tasks", None)
        return len(tasks) if tasks is not None else None

    # Finds .blend files that appeared in the add-on's download folder since the download started.
    def new_files(self):
        addon = bpy.context.preferences.addons.get(self.report["addon_module"])
        folder = getattr(getattr(addon, "preferences", None), "global_dir", "")
        found = []
        if folder and os.path.isdir(folder):
            for root, _dirs, files in os.walk(folder):
                for name in files:
                    path = os.path.join(root, name)
                    if name.lower().endswith(".blend") and os.path.getmtime(path) >= self.download_started - 1:
                        found.append(path)
        return found

    # Copies a file that arrived into the output folder, the only copy this script makes.
    def collect(self, files):
        os.makedirs(self.out, exist_ok=True)
        for path in files:
            target = os.path.join(self.out, os.path.basename(path))
            shutil.copyfile(path, target)
            self.report["files"].append({"downloaded_to": path.replace("\\", "/"),
                                         "copied_to": target.replace("\\", "/"), "bytes": os.path.getsize(path)})
        self.report["file_arrived"] = bool(files)

    # Does the checks before the download. Returns false when the experiment cannot go on.
    def prepare(self):
        if not self.find_addon():
            self.finish("addon_not_installed")
            return False
        if not self.enable_addon():
            self.finish("addon_not_enabled")
            return False
        if not self.check_signed_in():
            self.finish("not_signed_in")
            return False
        return True

    # One round of waiting. Returns true while the experiment should go on.
    def poll(self):
        if self.finished:
            return False
        self.pump()
        elapsed = time.time() - self.download_started
        files = self.new_files()
        if files:
            self.step("file_arrived", True, "%d file(s) after %.1f s" % (len(files), elapsed))
            self.collect(files)
            self.finish("downloaded")
            return False
        if self.catcher.messages and self.active_downloads() == 0 and elapsed > 10:
            self.step("download_ended", False, "no active download and the add-on logged errors")
            self.finish("failed")
            return False
        if elapsed > self.timeout:
            self.step("wait", False, "no file after %.0f s" % self.timeout)
            self.finish("timeout")
            return False
        return True

    # Starts the download after the checks. Returns false when it did not start.
    def begin(self, settle=None):
        if not self.prepare():
            return False
        if settle is not None:
            settle()
        if not self.start_download():
            self.finish("download_not_started")
            return False
        return True


# Waits with a loop that runs the add-on's timer functions by hand, for Blender without a window.
def run_background(experiment):
    # Gives the add-on's client a few seconds to start before the download is asked for.
    def settle():
        start = time.time()
        while time.time() - start < START_DELAY_SECONDS:
            experiment.pump()
            time.sleep(POLL_SECONDS)

    if not experiment.begin(settle):
        return
    while experiment.poll():
        time.sleep(POLL_SECONDS)


# Waits with a Blender timer, for Blender with a window. Quits Blender when done.
def run_windowed(experiment):
    state = {"begun": False}

    # One timer tick: start after a short delay, then poll until finished.
    def tick():
        if not state["begun"]:
            state["begun"] = True
            if not experiment.begin():
                bpy.ops.wm.quit_blender()
                return None
            return POLL_SECONDS
        if experiment.poll():
            return POLL_SECONDS
        bpy.ops.wm.quit_blender()
        return None

    bpy.app.timers.register(tick, first_interval=START_DELAY_SECONDS)


# Runs the experiment in the way that fits how Blender was started.
def main():
    options = parse_args()
    experiment = Experiment(options.asset_base_id, options.out, options.timeout)
    try:
        if bpy.app.background:
            run_background(experiment)
        else:
            run_windowed(experiment)
    except Exception as error:
        experiment.error("The experiment stopped on an error: %s" % error)
        experiment.finish("script_error")


main()
