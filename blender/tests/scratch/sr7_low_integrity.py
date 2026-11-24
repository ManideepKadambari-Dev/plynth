"""S-R 7, run by the hidden core test "[s_r_7]" in a Blender started at low integrity.

    blender -b ... --python sr7_low_integrity.py -- <labelled folder> <file outside it> <fake helper exe>

It writes inside the labelled folder, tries the file outside it, starts the fake helper from a copy inside the
labelled folder, asking it for a tray icon, and talks to it over 127.0.0.1. The icon may fail to appear; its outcome
is printed, and the helper must still answer and take one fake download to its end. The helper is left running so
the test can see that the end of the job takes it away. It prints one "SR7 <check>: <result>" line for each check.
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import time

USER_VARIABLES = ("BLENDER_USER_RESOURCES", "BLENDER_USER_CONFIG", "BLENDER_USER_SCRIPTS", "BLENDER_USER_EXTENSIONS",
                  "BLENDER_USER_DATAFILES", "APPDATA", "LOCALAPPDATA", "USERPROFILE", "HOME", "XDG_DATA_HOME",
                  "PLYNTH_BLENDER_PRIVATE", "PLYNTH_CONFIG_DIR")

# Prints one result line.
def say(name, result):
    print("SR7 %s: %s" % (name, result), flush=True)


# Tries to write a small file. True when it worked.
def can_write(path):
    try:
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("probe")
        return True
    except OSError:
        return False


# Sends one JSON line to the helper and returns its answer.
def ask(port, request):
    with socket.create_connection(("127.0.0.1", port), timeout=5) as connection:
        connection.sendall((json.dumps(request) + "\n").encode("utf-8"))
        data = b""
        while not data.endswith(b"\n"):
            part = connection.recv(4096)
            if not part:
                break
            data += part
    return json.loads(data.decode("utf-8"))


# The JSON lines of the helper's log.
def read_lines(path):
    if not os.path.isfile(path):
        return []
    with open(path, "r", encoding="utf-8") as handle:
        return [json.loads(line) for line in handle if line.strip()]


# The environment of the fake helper: every user folder variable points at a folder made inside the labelled
# folder, and no other Blender user variable is passed on.
def helper_env(folder):
    env = {name: value for name, value in os.environ.items() if not name.upper().startswith("BLENDER_USER_")}
    for name in USER_VARIABLES:
        env[name] = os.path.join(folder, "env", name.lower())
        os.makedirs(env[name], exist_ok=True)
    return env


# The text of a file, or a note that it is missing or unreadable.
def read_text(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except OSError as error:
        return "<%s>" % error


# Prints what the helper left behind, so a failed run can be understood from the output alone.
def tell_helper(process, log, errors, last_error):
    code = process.poll()
    print("SR7 detail exit: %s" % ("still running" if code is None else code), flush=True)
    print("SR7 detail last error: %r" % (last_error,), flush=True)
    print("SR7 detail log:\n%s" % read_text(log), flush=True)
    print("SR7 detail stderr:\n%s" % read_text(errors), flush=True)


# Runs the checks.
def main():
    folder, outside, helper = sys.argv[sys.argv.index("--") + 1:][:3]
    import bpy
    say("python", "ok" if bpy.app.background else "not background")
    say("inside", "ok" if can_write(os.path.join(folder, "inside.txt")) else "denied")
    say("outside", "ok" if can_write(outside) else "denied")
    copy = os.path.join(folder, "client", "bin", "helper.exe")
    os.makedirs(os.path.dirname(copy), exist_ok=True)
    shutil.copyfile(helper, copy)
    with socket.socket() as spare:
        spare.bind(("127.0.0.1", 0))
        port = spare.getsockname()[1]
    log = os.path.join(folder, "helper.log")
    source = os.path.join(folder, "source.blend")
    with open(source, "wb") as handle:
        handle.write(b"BLENDER-v502 SR7 source")
    downloads = os.path.join(folder, "downloads")
    os.makedirs(downloads, exist_ok=True)
    scenario = os.path.join(folder, "helper.json")
    with open(scenario, "w", encoding="utf-8") as handle:
        json.dump({"tray": True, "linger_s": 60, "download": {"source": source, "partial_ms": 200}}, handle)
    env = helper_env(folder)
    env.update(FAKE_STORE_SCENARIO=scenario, FAKE_STORE_LOG=log)
    errors = os.path.join(folder, "helper.err")
    with open(errors, "wb") as error_file:
        process = subprocess.Popen([copy, "--port", str(port), "--global_dir", folder], cwd=folder, env=env,
                                   stderr=error_file)
    # The helper is left running on purpose: the test checks that the job's end takes it away.
    print("SR7 helper pid: %d" % process.pid, flush=True)
    last_error = None

    # Asks the helper once, keeping the error. None when it did not answer.
    def try_ask(request):
        nonlocal last_error
        try:
            return ask(port, request)
        except (OSError, ValueError) as error:
            last_error = error
            return None

    answer = None
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline and process.poll() is None:
        answer = try_ask({"op": "report"})
        if answer is not None:
            break
        time.sleep(0.1)
    socket_ok = isinstance(answer, dict) and "tasks" in answer
    say("socket", "ok" if socket_ok else "failed")
    helper_ok = os.path.isfile(os.path.join(folder, "client", "system_id"))
    say("helper", "ok" if helper_ok else "failed")

    # The icon may fail to appear at low integrity. Its outcome is printed, and the helper must go on without it.
    tray = []
    for _ in range(50):
        try:
            tray = [line for line in read_lines(log) if line.get("op") == "tray"]
        except ValueError:
            tray = []
        if tray:
            break
        time.sleep(0.1)
    if not tray:
        say("icon", "no outcome noted")
    elif tray[0].get("ok"):
        say("icon", "shown")
    else:
        say("icon", "not shown, window %s, error %s" % (tray[0].get("window"), tray[0].get("error")))
    again = try_ask({"op": "report"}) if process.poll() is None else None
    after_ok = process.poll() is None and isinstance(again, dict) and "tasks" in again
    say("after icon", "ok" if after_ok else "failed")

    # One fake download, taken to its end: a finished task whose file holds the source's bytes.
    request = {"op": "asset_download", "data": {
        "asset_data": {"name": "SR7 Item", "id": "sr7-version", "assetBaseId": "sr7-base", "files": []},
        "download_dirs": [downloads]}}
    started = try_ask(request) if after_ok else None
    download_ok = False
    if isinstance(started, dict) and started.get("task_id"):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline and process.poll() is None and not download_ok:
            report = try_ask({"op": "report"}) or {}
            for task in report.get("tasks", []):
                if task.get("task_id") != started["task_id"] or task.get("status") != "finished":
                    continue
                paths = task.get("result", {}).get("file_paths", [])
                if paths and os.path.isfile(paths[0]) and os.path.dirname(os.path.dirname(os.path.abspath(paths[0]))) \
                        == os.path.abspath(downloads):
                    with open(paths[0], "rb") as handle, open(source, "rb") as original:
                        download_ok = handle.read() == original.read()
            if not download_ok:
                time.sleep(0.1)
    say("download", "ok" if download_ok else "failed")
    if not (socket_ok and helper_ok and after_ok and download_ok):
        tell_helper(process, log, errors, last_error)


main()
