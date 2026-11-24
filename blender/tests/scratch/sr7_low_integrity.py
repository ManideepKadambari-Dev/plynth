"""S-R 7, run by the hidden core test "[s_r_7]" in a Blender started at low integrity.

    blender -b ... --python sr7_low_integrity.py -- <labelled folder> <file outside it> <fake helper exe>

It writes inside the labelled folder, tries the file outside it, starts the fake helper from a copy inside the
labelled folder with a tray icon and talks to it over 127.0.0.1. It prints one "SR7 <check>: <result>" line for each check.
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
    scenario = os.path.join(folder, "helper.json")
    with open(scenario, "w", encoding="utf-8") as handle:
        json.dump({"tray": True, "linger_s": 60}, handle)
    env = helper_env(folder)
    env.update(FAKE_STORE_SCENARIO=scenario, FAKE_STORE_LOG=log)
    errors = os.path.join(folder, "helper.err")
    with open(errors, "wb") as error_file:
        process = subprocess.Popen([copy, "--port", str(port), "--global_dir", folder], cwd=folder, env=env,
                                   stderr=error_file)
    try:
        answer = None
        last_error = None
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            try:
                answer = ask(port, {"op": "report"})
                break
            except (OSError, ValueError) as error:
                last_error = error
                if process.poll() is not None:
                    break
                time.sleep(0.1)
        socket_ok = isinstance(answer, dict) and "tasks" in answer
        say("socket", "ok" if socket_ok else "failed")
        helper_ok = os.path.isfile(os.path.join(folder, "client", "system_id"))
        say("helper", "ok" if helper_ok else "failed")
        tray = []
        for _ in range(30):
            try:
                tray = [line for line in read_lines(log) if line.get("op") == "tray"]
            except ValueError:
                tray = []
            if tray:
                break
            time.sleep(0.1)
        tray_ok = bool(tray) and bool(tray[0].get("ok"))
        say("tray", "ok" if tray_ok else "failed")
        if not (socket_ok and helper_ok and tray_ok):
            tell_helper(process, log, errors, last_error)
    finally:
        process.kill()
        process.wait(timeout=10)


main()
