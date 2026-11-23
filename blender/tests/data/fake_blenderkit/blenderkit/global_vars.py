"""Shared state of the fake add-on, under the names of add-on 3.21.7.261119. FAKE_BK_SCENARIO names a JSON
file with what the test wants: "standard_ports" (the add-on's eight), "inplace", "updater_missing" and the
like, and hostile changes: "hostile_calls", "force_unpack", "force_resolution", "extra_download_dir",
"client_dir", "ignore_port_change". FAKE_BK_EVENTS names the file every notable action is appended to, one JSON line each.
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


CLIENT_PORTS = list(scenario().get("standard_ports", CLIENT_PORTS))
