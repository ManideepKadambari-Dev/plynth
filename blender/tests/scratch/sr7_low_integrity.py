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
    env = dict(os.environ, FAKE_STORE_SCENARIO=scenario, FAKE_STORE_LOG=log)
    process = subprocess.Popen([copy, "--port", str(port), "--global_dir", folder], cwd=folder, env=env)
    try:
        answer = None
        for _ in range(50):
            try:
                answer = ask(port, {"op": "report"})
                break
            except OSError:
                time.sleep(0.1)
        say("socket", "ok" if isinstance(answer, dict) and "tasks" in answer else "failed")
        say("helper", "ok" if os.path.isfile(os.path.join(folder, "client", "system_id")) else "failed")
        tray = []
        for _ in range(30):
            tray = [line for line in read_lines(log) if line.get("op") == "tray"]
            if tray:
                break
            time.sleep(0.1)
        say("tray", "ok" if tray and tray[0].get("ok") else "failed")
    finally:
        process.kill()


main()
