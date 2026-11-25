"""Shared state of the fake add-on, under the names of add-on 3.21.7.261119. FAKE_BK_SCENARIO names a JSON
file with what the test wants: "standard_ports" (the eight of the add-on), "inplace", "updater_missing" and the
like, and hostile changes: "hostile_calls" (a list of the groups "named", "early" and "addon_utils"),
"force_unpack", "force_resolution" (in the request and its preferences), "force_pref_resolution" (in the
preferences only), "drop_resolution", "extra_download_dir", "client_dir", "ignore_port_change", and failures:
"download_raises" (an error name), "download_answer" ("no_task" or "not_json"), "operator_raises". FAKE_BK_EVENTS
names the file every notable action is appended to, one JSON line each. Markers are files beside it, one for
each named action that must never run (marker_path).
"""
import json
import os

VERSION = (3, 21, 7, 261119)
CLIENT_PORTS = ["41501", "41502", "41503", "41504", "41505", "41506", "41507", "41508"]
CLIENT_ACCESSIBLE = False
CLIENT_RUNNING = False
client_process = None
PREFS = {}
DATA = {}
EARLY_CALLABLES = {}


# The test's scenario, or an empty one.
def scenario():
    path = os.environ.get("FAKE_BK_SCENARIO")
    if not path:
        return {}
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


# Appends one event line for the tests. A refused write is noted nowhere, as the real add-on would lose it.
def note(event, **fields):
    path = os.environ.get("FAKE_BK_EVENTS")
    if not path:
        return
    fields["event"] = event
    try:
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(json.dumps(fields) + "\n")
    except OSError:
        pass


# The path of the marker of an action, beside the events file, or None without one.
def marker_path(name):
    path = os.environ.get("FAKE_BK_EVENTS")
    return os.path.join(os.path.dirname(path), name + ".marker") if path else None


# Writes the marker of an action. A refused write leaves none, as for note.
def mark(name):
    path = marker_path(name)
    if not path:
        return
    try:
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(name)
    except OSError:
        pass


CLIENT_PORTS = list(scenario().get("standard_ports", CLIENT_PORTS))
