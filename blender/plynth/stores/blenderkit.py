"""Every touch point with the BlenderKit add-on in a fetch, in one place.

The names are those of add-on 3.21.7.261119 as the source check lists them. They are not a published
interface, so each one is looked up before the fetch uses the add-on, and a missing one fails the fetch
closed. The worker never calls a BlenderKit address itself, never reads out the key and never puts what the
add-on or its helper prints into a result: every failure is a fixed sentence, at most with the add-on's
version, an exception's type name or a word of REFUSAL_WORDS.
"""
import importlib
import json
import os
import random
import re
import shutil
import socket
import tempfile
import time
from urllib.parse import urlsplit

import bpy

from ..jobs import WorkerError, refusal

ADDON_NAME = "blenderkit"
OPERATOR = "blenderkit_download"
ADDON_CHOICE = "scene.blenderkit_addon_choice"
KINDS = ("model", "material")
RESOLUTIONS = {"0.5k": "512", "1k": "1024", "2k": "2048", "4k": "4096", "8k": "8192", "original": "ORIGINAL"}
REQUEST_RESOLUTIONS = {"512": "resolution_0_5K", "1024": "resolution_1K", "2048": "resolution_2K",
                       "4096": "resolution_4K", "8192": "resolution_8K", "ORIGINAL": "blend"}
LICENSES = {"royalty_free": "royalty_free", "cc_zero": "cc0", "cc0": "cc0"}
HOSTS = ("blendkit.com", "blenderkit.com")
UUID = re.compile(r"^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$")
BLEND_FILE_TYPE = re.compile(r"^(blend|resolution_[0-9a-z_]+)$")
PAGE_ADDRESS = "https://www.blendkit.com/asset-gallery-detail/%s/"
PARTIAL_PREFIX = ".blendkit-download-"
SKIPPED_FOLDERS = {"client", "temp", "tmp", "thumbnails", "__pycache__"}
TARGET_NAME = "PlynthMaterialTarget"
PORT_COUNT = 3
SEARCH_TIMEOUT = 15.0
PUMP_SECONDS = 0.25
READY_SECONDS = 60.0
REFUSED_TEXT = "Refused by the fetch"
REFUSAL_WORDS = ("search_id", "base_id", "kind", "version_id", "helper", "request", "folders", "global_dir",
                 "behaviour", "unpack", "resolution", "files", "host")

PARTS = {
    "client_lib": ("asset_download", "blocking_request", "refresh_token", "oauth2_logout", "is_using_inplace_client",
                   "is_client_process_alive", "start_blenderkit_client", "get_client_directory",
                   "get_client_log_path", "get_port", "applying_client_settings"),
    "download": ("try_finished_append", "handle_download_task", "download_tasks", "cancel_running_downloads"),
    "global_vars": ("CLIENT_PORTS", "CLIENT_ACCESSIBLE", "client_process", "PREFS"),
    "paths": ("get_download_dirs", "get_config_dir_path", "dirs_exist_dict", "get_asset_directory_name"),
    "persistent_preferences": ("load_preferences_from_JSON", "get_preferences_path"),
    "search": ("get_search_simple",),
    "timer": ("client_communication_timer", "handle_task"),
    "utils": ("get_preferences_as_dict",),
    "bkit_oauth": ("ensure_token_refresh", "logout", "clean_login_data", "write_tokens"),
}
REFUSALS = (("client_lib", "refresh_token"), ("bkit_oauth", "ensure_token_refresh"), ("bkit_oauth", "logout"),
            ("client_lib", "oauth2_logout"), ("bkit_oauth", "clean_login_data"), ("bkit_oauth", "write_tokens"))
PREFERENCE_FIELDS = ("api_key", "api_key_refresh", "api_key_timeout", "preferences_lock", "global_dir",
                     "directory_behaviour", "unpack_files", "create_asset_library", "resolution",
                     "use_clipboard_scan", "thread_communication")

NOT_INSTALLED = "The BlenderKit add-on is not installed. Install it in Blender from Preferences, Get Extensions, " \
                "and sign in."
NOT_RUNNING = "The BlenderKit add-on is installed but not running. Open Blender once with the BlenderKit add-on " \
              "enabled, then try again."
NOT_SIGNED_IN = "Sign in to BlenderKit inside Blender first: open Blender, go to Preferences, Add-ons, BlenderKit, " \
                "and log in."
EXPIRED = "The BlenderKit sign in has expired. Open Blender, sign in to BlenderKit again, then try again."
NOT_KNOWN = "The %s is not a version these downloads work with."
OUTSIDE = "The %s would use a folder outside Plynth's fetch folder, so the download was not started."
IN_PLACE = "The %s runs its helper from its own folder, so the download was not started."
NO_PORTS = "No free local ports were found for the helper of the %s."
NO_HELPER = "The helper of the %s did not start."
OTHER_HELPER = "The %s reached a helper that this download did not start, so the download was not started."
RESTARTED = "The helper of the %s stopped during the download."
ENDED_EARLY = "The helper of the %s ended before it answered."
GUARD_REFUSED = "The fetch guard refused to start the helper of the %s."
START_FAILED = "The helper of the %s could not be started (%s)."
GUARD_REFUSED_TEXT = "Refused by the fetch guard"
BAD_REQUEST = "The %s asked for a download that does not match the item (%s), so it was not sent."
OTHER_ITEM = "BlenderKit answered with another item than the one asked for. Copy the address from the item's page."
NOT_STARTED = "The %s did not start the download. Check the item in BlenderKit inside Blender."
OPERATOR_FAILED = "The %s could not start the download (%s)."
NO_FILE = "The %s did not deliver the item. Check the item in BlenderKit inside Blender."
NO_MATERIAL = "The downloaded file holds no material with the item's name."
TIMED_OUT = "Nothing arrived from BlenderKit after %d seconds (%s). Try again, or check the item in BlenderKit " \
            "inside Blender."
OTHER_TAB = {
    "model": "This BlenderKit item is a model. Import it in the Assets window or with plynth assets import.",
    "material": "This BlenderKit item is a material. Import it in the Materials window or with plynth materials import.",
}
KNOWN_KINDS = {"hdr": "an HDR", "scene": "a scene", "brush": "a brush", "addon": "an add-on",
               "nodegroup": "a node group", "printable": "a printable item"}
ONLY_TWO = "Only models and materials can be imported."


# A field of a dict or of an object, or None.
def field(value, name):
    if isinstance(value, dict):
        return value.get(name)
    return getattr(value, name, None)


# The last dotted part of a module name, so bl_ext.user_default.blenderkit gives blenderkit.
def short_name(module_name):
    return module_name.rsplit(".", 1)[-1]


# The add-on's version as text, or "unknown".
def version_of(module):
    try:
        import addon_utils
        version = (addon_utils.module_bl_info(module) or {}).get("version")
        if isinstance(version, (tuple, list)):
            return ".".join(str(int(part)) for part in version[:3])
    except Exception:
        pass
    return "unknown"


# The sentence for an item of another kind than the one asked for.
def kind_sentence(actual):
    actual = str(actual or "").strip().lower()
    if actual in OTHER_TAB:
        return OTHER_TAB[actual]
    if actual in KNOWN_KINDS:
        return "This BlenderKit item is %s. %s" % (KNOWN_KINDS[actual], ONLY_TWO)
    return "This BlenderKit item is not a model or a material. %s" % ONLY_TWO


# Whether a download address is https on one of BlenderKit's hosts or a name under them.
def allowed_address(url):
    if not isinstance(url, str):
        return False
    try:
        parts = urlsplit(url)
        host = (parts.hostname or "").lower()
        port = parts.port
    except ValueError:
        return False
    if parts.scheme.lower() != "https" or parts.username or parts.password or port not in (None, 443):
        return False
    return any(host == name or host.endswith("." + name) for name in HOSTS)


# Whether a local port can be taken now.
def port_is_free(port):
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.bind(("127.0.0.1", port))
        return True
    except OSError:
        return False


# The names of the materials in a .blend, read without loading anything.
def material_names(blend):
    try:
        with bpy.data.libraries.load(blend) as (data_from, _data_to):
            return [str(name) for name in data_from.materials]
    except Exception:
        return []


class Fetch:
    # One download of one item through the add-on: what was asked, the add-on's parts and what happened.
    def __init__(self, base_id, kind, resolution, private_folder, output_folder):
        self.base_id = base_id
        self.kind = kind
        self.resolution = RESOLUTIONS[resolution]
        self.private = os.path.abspath(private_folder)
        self.root = os.path.normcase(os.path.realpath(self.private)).rstrip("\\/")
        self.output = os.path.abspath(output_folder)
        self.module_name = None
        self.version = "unknown"
        self.parts = {}
        self.ports = []
        self.kind_folders = []
        self.refusal = None
        self.task_id = None
        self.noted = None
        self.helper_started = False
        self.restarted = False
        self.start_problem = None
        self.process = None

    # A short text naming the add-on and its version, for messages.
    @property
    def label(self):
        return "BlenderKit add-on %s" % self.version

    # Raises download_failed with a sentence that may name the add-on.
    def fail(self, sentence):
        raise WorkerError("download_failed", sentence % self.label if "%s" in sentence else sentence)

    # Whether a path lies inside the fetch folder, after links are followed.
    def inside(self, path):
        try:
            return os.path.normcase(os.path.realpath(os.fspath(path))).startswith(self.root + os.sep)
        except (OSError, TypeError, ValueError):
            return False

    # Refuses an output folder outside the fetch folder.
    def check_output(self):
        if not self.inside(self.output):
            raise WorkerError("bad_job", "The params need an output_folder inside the private_folder.")

    # Finds the enabled add-on. Raises addon_missing when it is not installed or did not start here.
    def find_addon(self):
        try:
            import addon_utils
            modules = {module.__name__: module for module in addon_utils.modules()}
        except Exception:
            modules = {}
        enabled = [name for name in bpy.context.preferences.addons.keys() if short_name(name) == ADDON_NAME]
        if not enabled:
            installed = any(short_name(name) == ADDON_NAME for name in modules)
            raise WorkerError("addon_missing", NOT_RUNNING if installed else NOT_INSTALLED)
        self.module_name = enabled[0]
        self.version = version_of(modules.get(self.module_name))

    # Imports the add-on's modules the fetch uses and checks every name it needs. Fails closed on a missing one.
    def load_parts(self):
        for short, names in PARTS.items():
            try:
                module = importlib.import_module("%s.%s" % (self.module_name, short))
            except Exception:
                self.fail(NOT_KNOWN)
            if any(not hasattr(module, name) for name in names):
                self.fail(NOT_KNOWN)
            self.parts[short] = module
        prefs = self.preferences()
        if prefs is None or any(not hasattr(prefs, name) for name in PREFERENCE_FIELDS):
            self.fail(NOT_KNOWN)

    # The add-on's preferences object, or None.
    def preferences(self):
        entry = bpy.context.preferences.addons.get(self.module_name)
        return getattr(entry, "preferences", None)

    # Seals Blender through the guard and puts the fetch's own functions in the add-on: the lookup and the
    # request gate through the seal, the refusals of refresh, logout and token writes, then the search timeout,
    # the task filter, the append step and the helper start. Raises the guard's error when a step fails.
    def seal(self, guard):
        client_lib, search = self.parts["client_lib"], self.parts["search"]
        replacements = ((search, "get_search_simple", self.lookup(search.get_search_simple)),
                        (client_lib, "asset_download", self.gate(client_lib.asset_download)))
        refusals = tuple((self.parts[short], name) for short, name in REFUSALS)
        guard.seal(operators=(ADDON_CHOICE,), replacements=replacements, refusals=refusals)
        own = ((client_lib, "blocking_request", self.slow_request(client_lib.blocking_request)),
               (self.parts["timer"], "handle_task", self.download_tasks_only()),
               (self.parts["download"], "try_finished_append", self.note_append()),
               (client_lib, "start_blenderkit_client", self.start_once(client_lib.start_blenderkit_client)))
        for module, name, function in own:
            setattr(module, name, function)
        if any(getattr(module, name, None) is not function for module, name, function in own):
            raise guard.GuardError("seal_failed")

    # Points the home, app data and XDG folders that child programs get into the fetch folder, and refuses a
    # helper that would run from the add-on's own folder.
    def point_children_inside(self):
        home = os.path.join(self.private, "home")
        folders = {"USERPROFILE": home, "HOME": home, "APPDATA": os.path.join(home, "AppData", "Roaming"),
                   "LOCALAPPDATA": os.path.join(home, "AppData", "Local"),
                   "XDG_DATA_HOME": os.path.join(home, ".local", "share"),
                   "XDG_CONFIG_HOME": os.path.join(home, ".config"), "XDG_CACHE_HOME": os.path.join(home, ".cache")}
        for name, folder in folders.items():
            os.makedirs(folder, exist_ok=True)
            os.environ[name] = folder
        if self.parts["client_lib"].is_using_inplace_client():
            self.fail(IN_PLACE)

    # Gives the add-on three free ports above 49151 that are none of its own, so this Blender never meets the
    # user's helper and the user's Blender never meets this one.
    def choose_ports(self):
        global_vars = self.parts["global_vars"]
        standard = {str(port) for port in global_vars.CLIENT_PORTS}
        chooser = random.SystemRandom()
        ports = []
        for _ in range(500):
            if len(ports) == PORT_COUNT:
                break
            port = str(chooser.randint(49152, 65535))
            if port not in standard and port not in ports and port_is_free(int(port)):
                ports.append(port)
        if len(ports) < PORT_COUNT:
            self.fail(NO_PORTS)
        current = global_vars.CLIENT_PORTS
        if isinstance(current, list):
            current[:] = ports
        else:
            global_vars.CLIENT_PORTS = list(ports)
        if [str(port) for port in global_vars.CLIENT_PORTS] != ports:
            self.fail(NOT_KNOWN)
        self.ports = ports

    # Reads the sign in once: loads the saved file when no key is held, refuses a missing or expired sign in,
    # then forgets the refresh token in memory.
    def read_sign_in(self):
        prefs = self.preferences()
        if not str(prefs.api_key or "").strip():
            self.load_saved_sign_in()
            prefs = self.preferences()
        has_key = bool(str(prefs.api_key or "").strip())
        has_refresh = bool(str(prefs.api_key_refresh or "").strip())
        try:
            timeout = int(prefs.api_key_timeout or 0)
        except (TypeError, ValueError):
            timeout = 0
        if not has_key:
            raise WorkerError("not_signed_in", NOT_SIGNED_IN)
        if has_refresh and 0 < timeout < time.time():
            raise WorkerError("not_signed_in", EXPIRED)
        prefs.preferences_lock = True
        prefs.api_key_refresh = ""

    # Runs the add-on's loader of its saved file, only when that file is a JSON object with keep_preferences
    # true, and with the helper settings flag on so the loader posts nothing.
    def load_saved_sign_in(self):
        saved = self.parts["persistent_preferences"]
        client_lib = self.parts["client_lib"]
        try:
            path = saved.get_preferences_path()
            if not os.path.isfile(path):
                return
            with open(path, "r", encoding="utf-8") as handle:
                data = json.load(handle)
        except Exception:
            return
        if not isinstance(data, dict) or data.get("keep_preferences") is not True:
            return
        before = client_lib.applying_client_settings
        client_lib.applying_client_settings = True
        try:
            saved.load_preferences_from_JSON()
        except Exception:
            pass
        finally:
            client_lib.applying_client_settings = before

    # Sets the preferences the download uses, in memory with the lock on, makes the add-on's copy of them and
    # checks that every folder the add-on and its helper would use lies inside the fetch folder.
    def set_preferences(self):
        prefs = self.preferences()
        prefs.preferences_lock = True
        values = (("global_dir", self.private), ("directory_behaviour", "GLOBAL"), ("unpack_files", False),
                  ("create_asset_library", False), ("use_clipboard_scan", False), ("thread_communication", False),
                  ("resolution", self.resolution))
        for name, value in values:
            setattr(prefs, name, value)
        if not prefs.preferences_lock or any(getattr(prefs, name) != value for name, value in values):
            self.fail(NOT_KNOWN)
        global_vars, paths, client_lib = self.parts["global_vars"], self.parts["paths"], self.parts["client_lib"]
        global_vars.PREFS = self.parts["utils"].get_preferences_as_dict()
        paths.dirs_exist_dict.clear()
        folders = list(paths.get_download_dirs(self.kind))
        others = [paths.get_config_dir_path(), client_lib.get_client_directory(), client_lib.get_client_log_path(),
                  tempfile.gettempdir()]
        if not folders or not all(self.inside(folder) for folder in folders + others):
            self.fail(OUTSIDE)

    # Clears the add-on's records of used items in the startup scene, in memory, so none is taken as evidence.
    def clear_scene_records(self):
        try:
            if "assets used" in bpy.context.scene:
                del bpy.context.scene["assets used"]
        except Exception:
            pass

    # The lookup inside the operator: raises when the item found is not the one asked for, before the operator
    # can branch on its kind.
    def lookup(self, original):
        def get_search_simple(parameters, *args, **kwargs):
            if field(parameters, "asset_base_id") != self.base_id:
                self.refuse(WorkerError("download_failed", OTHER_ITEM), "search_id")
            results = original(parameters, *args, **kwargs)
            if results:
                self.check_found(results[0])
            return results
        return get_search_simple

    # Refuses a found item with another base id, another kind or a version id that is not a uuid.
    def check_found(self, item):
        if field(item, "assetBaseId") != self.base_id:
            self.refuse(WorkerError("download_failed", OTHER_ITEM), "base_id")
        actual = str(field(item, "assetType") or "").lower()
        if actual != self.kind:
            self.refuse(WorkerError("wrong_asset_kind", kind_sentence(actual)), "kind")
        if not isinstance(field(item, "id"), str) or not UUID.match(field(item, "id")):
            self.refuse(WorkerError("download_failed", OTHER_ITEM), "version_id")

    # Keeps the first refusal for the result, prints its fixed word and stops the add-on's call with a
    # PermissionError.
    def refuse(self, error, word):
        if self.refusal is None:
            self.refusal = error
            refusal(word if word in REFUSAL_WORDS else "request")
        raise PermissionError(13, REFUSED_TEXT)

    # Refuses a download request that does not match the item, naming the fixed word in the sentence.
    def refuse_request(self, word):
        self.refuse(WorkerError("download_failed", BAD_REQUEST % (self.label, word)), word)

    # The request gate: checks the download request and raises before it leaves when anything is not as asked.
    def gate(self, original):
        def asset_download(data, *args, **kwargs):
            self.check_request(data)
            answer = original(data, *args, **kwargs)
            self.task_id = field(answer, "task_id")
            return answer
        return asset_download

    # Checks one download request and takes zip and other files that are no .blend out of its file list.
    # The add-on writes the resolution into the request under the server's name, resolution_2K for 2048, in
    # the request and in its copy of the preferences, so both names of the asked size pass and no other size.
    def check_request(self, data):
        if self.task_id is not None or not self.helper_is_ours() or not self.parts["global_vars"].CLIENT_ACCESSIBLE:
            self.refuse(WorkerError("download_failed", OTHER_HELPER % self.label), "helper")
        asset = field(data, "asset_data")
        prefs = field(data, "PREFS")
        if not isinstance(data, dict) or not isinstance(asset, dict) or not isinstance(prefs, dict):
            self.refuse_request("request")
        self.check_found(asset)
        folders = data.get("download_dirs")
        if not isinstance(folders, list) or not folders or not all(isinstance(f, str) and self.inside(f)
                                                                   for f in folders):
            self.refuse_request("folders")
        try:
            same_root = os.path.normcase(os.path.realpath(prefs.get("global_dir"))).rstrip("\\/") == self.root
        except (OSError, TypeError, ValueError):
            same_root = False
        if not same_root:
            self.refuse_request("global_dir")
        if prefs.get("directory_behaviour") != "GLOBAL":
            self.refuse_request("behaviour")
        if "unpack_files" not in prefs:
            self.refuse_request("unpack")
        for values in (data, prefs):
            if any("unpack" in str(name).lower() and value for name, value in values.items()):
                self.refuse_request("unpack")
        allowed = (self.resolution, REQUEST_RESOLUTIONS[self.resolution])
        if prefs.get("resolution") not in allowed or data.get("resolution") not in allowed:
            self.refuse_request("resolution")
        files = asset.get("files")
        if not isinstance(files, list):
            self.refuse_request("files")
        kept = [entry for entry in files if isinstance(entry, dict)
                and BLEND_FILE_TYPE.match(str(entry.get("fileType") or "").lower())]
        if not kept:
            self.refuse_request("files")
        if not all(allowed_address(entry.get("downloadUrl")) for entry in kept):
            self.refuse_request("host")
        asset["files"] = kept
        self.kind_folders = [os.path.normcase(os.path.realpath(folder)) for folder in folders]

    # The add-on's waiting request with at least the fetch's search timeout.
    def slow_request(self, original):
        def blocking_request(url, method="GET", headers=None, json_data=None, timeout=None, **kwargs):
            try:
                wait = max(float(timeout), SEARCH_TIMEOUT)
            except (TypeError, ValueError):
                wait = SEARCH_TIMEOUT
            return original(url, method, headers, json_data, wait, **kwargs)
        return blocking_request

    # The task handler of the timer: passes download tasks to the add-on and drops every other task.
    def download_tasks_only(self):
        def handle_task(task, *args, **kwargs):
            if field(task, "task_type") == "asset_download":
                return self.parts["download"].handle_download_task(task)
            return None
        return handle_task

    # The append step: notes the delivered path and the named fields of the item for this base id and changes
    # nothing. A call without file_paths is refused.
    def note_append(self):
        def try_finished_append(asset_data=None, **kwargs):
            given = kwargs.get("file_paths")
            if not isinstance(given, (list, tuple)) or not given or not isinstance(given[-1], str):
                raise PermissionError(13, REFUSED_TEXT)
            if field(asset_data, "assetBaseId") != self.base_id:
                raise PermissionError(13, REFUSED_TEXT)
            author = field(asset_data, "author")
            fields = {"id": field(asset_data, "id"), "assetType": field(asset_data, "assetType"),
                      "name": field(asset_data, "name"), "displayName": field(asset_data, "displayName"),
                      "author": field(author, "fullName") if isinstance(author, dict) else None,
                      "license": field(asset_data, "license")}
            self.noted = (given[-1], fields)
        return try_finished_append

    # The helper start: the first one goes through, a second one is a restart and is refused.
    def start_once(self, original):
        def start_blenderkit_client(*args, **kwargs):
            if self.helper_started:
                if self.process is None:
                    self.start_problem = self.start_problem or ENDED_EARLY % self.label
                else:
                    self.restarted = True
                raise PermissionError(13, REFUSED_TEXT)
            self.helper_started = True
            try:
                return original(*args, **kwargs)
            except Exception as error:
                refused = isinstance(error, PermissionError) and GUARD_REFUSED_TEXT in str(error)
                self.start_problem = GUARD_REFUSED % self.label if refused else \
                    START_FAILED % (self.label, type(error).__name__)
                raise
        return start_blenderkit_client

    # Whether the helper in use is the living child this Blender started, on one of the fetch's ports.
    def helper_is_ours(self):
        global_vars, client_lib = self.parts["global_vars"], self.parts["client_lib"]
        try:
            alive = bool(client_lib.is_client_process_alive())
            port = str(client_lib.get_port())
        except Exception:
            return False
        if not self.helper_started or not alive or port not in self.ports:
            return False
        return self.process is None or global_vars.client_process is self.process

    # Runs the add-on's helper timer once, the only timer the fetch runs. A restart of the helper ends the fetch.
    def pump(self):
        try:
            self.parts["timer"].client_communication_timer()
        except Exception:
            pass
        if self.start_problem:
            raise WorkerError("download_failed", self.start_problem)
        if self.restarted:
            self.fail(RESTARTED)

    # Runs the timer until this Blender's own helper has answered a first report.
    def wait_for_helper(self):
        end = time.monotonic() + READY_SECONDS
        global_vars = self.parts["global_vars"]
        while True:
            self.pump()
            if global_vars.CLIENT_ACCESSIBLE:
                if not self.helper_is_ours():
                    self.fail(OTHER_HELPER)
                self.process = global_vars.client_process
                return
            if time.monotonic() > end:
                self.fail(NO_HELPER)
            time.sleep(PUMP_SECONDS)

    # A throwaway mesh object for a material download to target. Returns its name.
    def make_target(self):
        mesh = bpy.data.meshes.new(TARGET_NAME)
        target = bpy.data.objects.new(TARGET_NAME, mesh)
        try:
            bpy.context.scene.collection.objects.link(target)
        except Exception:
            pass
        return target.name

    # Calls the add-on's download operator with the id, no object active and, for a material, a target.
    def start_download(self):
        try:
            bpy.context.view_layer.objects.active = None
        except Exception:
            pass
        options = {"asset_base_id": self.base_id}
        if self.kind == "material":
            options["target_object"] = self.make_target()
        try:
            result = getattr(bpy.ops.scene, OPERATOR)(**options)
        except Exception as error:
            if self.refusal is not None:
                raise self.refusal
            raise WorkerError("download_failed", OPERATOR_FAILED % (self.label, type(error).__name__))
        if self.refusal is not None:
            raise self.refusal
        if "FINISHED" not in result or self.task_id is None:
            self.fail(NOT_STARTED)

    # Runs the timer until the add-on notes the delivered file. Fails when the task ends without one or the
    # helper stops, and cancels the downloads after wait_seconds.
    def wait_for_file(self, wait_seconds):
        started = time.monotonic()
        download = self.parts["download"]
        while True:
            self.pump()
            if self.noted is not None:
                return self.checked_file()
            if self.task_id not in download.download_tasks:
                self.fail(NO_FILE)
            if not self.helper_is_ours():
                self.fail(RESTARTED)
            if time.monotonic() - started > wait_seconds:
                try:
                    download.cancel_running_downloads()
                except Exception:
                    pass
                raise WorkerError("download_timeout", TIMED_OUT % (wait_seconds, self.label))
            time.sleep(PUMP_SECONDS)

    # The noted path, when it is a .blend that exists in this item's folder under the folder of its kind.
    def checked_file(self):
        path, fields = self.noted
        if not path.lower().endswith(".blend") or not self.inside(path) or not os.path.isfile(path):
            self.fail(NO_FILE)
        name, version_id = fields.get("name"), fields.get("id")
        if not isinstance(name, str) or not isinstance(version_id, str) or not UUID.match(version_id):
            self.fail(NO_FILE)
        item = self.parts["paths"].get_asset_directory_name(name, version_id)
        folder = os.path.normcase(os.path.dirname(os.path.realpath(path)))
        expected = {os.path.normcase(os.path.join(kind_folder, item)) for kind_folder in self.kind_folders}
        if not isinstance(item, str) or not item or folder not in expected:
            self.fail(NO_FILE)
        return path

    # Copies the .blend and the folders beside it into the output folder and returns the fetch output.
    def copy_out(self, blend):
        os.makedirs(self.output, exist_ok=True)
        file = os.path.basename(blend)
        shutil.copyfile(blend, os.path.join(self.output, file))
        source = os.path.dirname(blend)
        for folder, folders, files in os.walk(source):
            folders[:] = [name for name in folders if name.lower() not in SKIPPED_FOLDERS
                          and not os.path.islink(os.path.join(folder, name))]
            if os.path.normcase(folder) == os.path.normcase(source):
                continue
            relative = os.path.relpath(folder, source)
            for name in files:
                lower = name.lower()
                path = os.path.join(folder, name)
                if lower.endswith((".blend", ".blend1", ".tmp", ".part")) or lower.startswith(PARTIAL_PREFIX) \
                        or os.path.islink(path):
                    continue
                target = os.path.join(self.output, relative, name)
                os.makedirs(os.path.dirname(target), exist_ok=True)
                shutil.copyfile(path, target)
        _path, fields = self.noted
        item_name = str(fields.get("name") or "").strip()
        shown = str(fields.get("displayName") or "").strip() or item_name or None
        material = None
        if self.kind == "material":
            names = material_names(os.path.join(self.output, file))
            if item_name in names:
                material = item_name
            elif len(names) == 1:
                material = names[0]
            else:
                self.fail(NO_MATERIAL)
        author = str(fields.get("author") or "").strip() or None
        return {"file": file, "format": "blend", "kind": self.kind, "name": shown, "author": author,
                "license": LICENSES.get(str(fields.get("license") or "").strip().lower()),
                "reference": PAGE_ADDRESS % self.base_id, "material": material}
