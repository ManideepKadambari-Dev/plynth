"""The fake add-on's side of its helper. It talks to the fake helper program (tests/tools/fake_store_helper)
with one JSON line per connection on 127.0.0.1. As the real one it tries the ports in order and moves to the
first that answers, sends the key in every request, starts the helper from a copy in <global_dir>/client
and logs the helper's output there.
"""
import json
import os
import shutil
import socket
import subprocess

from . import global_vars

applying_client_settings = False
HERE = os.path.dirname(os.path.abspath(__file__))
TIMEOUT = (0.1, 1)
SECRET_TEXT = "SECRET-ADDRESS in the helper's answer"


# The port in use.
def get_port():
    return global_vars.CLIENT_PORTS[0]


# Moves a port to the front.
def reorder_ports(port=""):
    if port in global_vars.CLIENT_PORTS:
        global_vars.CLIENT_PORTS.remove(port)
        global_vars.CLIENT_PORTS.insert(0, port)


# The helper's folder.
def get_client_directory():
    pinned = global_vars.scenario().get("client_dir")
    if pinned:
        return pinned
    return os.path.join(global_vars.PREFS.get("global_dir", ""), "client")


# The helper's log, which follows the port.
def get_client_log_path():
    port = get_port()
    name = "default.log" if port == global_vars.scenario().get("standard_ports", ["41501"])[0] else "client-%s.log" % port
    return os.path.join(get_client_directory(), name)


# The copy of the helper that runs, and its version.
def get_client_binary_path():
    return os.path.join(get_client_directory(), "bin", "v1.0.0", "bk_client.exe"), "v1.0.0"


# The helper that comes with the add-on, read only.
def get_preinstalled_client_path():
    return os.path.join(HERE, "client", "bk_client.exe")


# Whether the helper runs from the add-on's own folder.
def is_using_inplace_client():
    return bool(global_vars.scenario().get("inplace"))


# Copies the helper into the client folder and starts it with its output in the log.
def start_blenderkit_client():
    exe, _version = get_client_binary_path()
    if is_using_inplace_client():
        exe = get_preinstalled_client_path()
    else:
        os.makedirs(os.path.dirname(exe), exist_ok=True)
        if not os.path.exists(exe):
            shutil.copyfile(get_preinstalled_client_path(), exe)
    log_path = get_client_log_path()
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    log = open(log_path, "wb")
    command = [exe, "--port", get_port(), "--global_dir", global_vars.PREFS.get("global_dir", ""), "--version", "3.21.7"]
    global_vars.note("client_started", port=get_port(), exe=exe)
    global_vars.client_process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                                  creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))


# Whether the helper this Blender started still runs.
def is_client_process_alive():
    process = global_vars.client_process
    return process is not None and process.poll() is None


# Sends one request to the first port that answers. Raises when none does.
def _send(request, timeout=1.0):
    request = dict(request)
    request["api_key"] = global_vars.PREFS.get("api_key", "")
    last = None
    ports = global_vars.CLIENT_PORTS
    if global_vars.scenario().get("ignore_port_change"):
        ports = global_vars.scenario()["standard_ports"]
    for port in list(ports):
        try:
            with socket.create_connection(("127.0.0.1", int(port)), timeout=timeout) as connection:
                connection.sendall((json.dumps(request) + "\n").encode("utf-8"))
                data = b""
                while not data.endswith(b"\n"):
                    part = connection.recv(65536)
                    if not part:
                        break
                    data += part
            reorder_ports(port)
            return json.loads(data.decode("utf-8"))
        except (OSError, ValueError) as error:
            last = error
    raise ConnectionError("no helper answered: %s" % last)


# The reports of the helper: the tasks to handle.
def get_reports(app_id=""):
    return _send({"op": "report", "app_id": app_id})


# Asks the helper to download an item. data holds asset_data, download_dirs, PREFS and the key. It waits as
# long as TIMEOUT says, as the real one does. The scenario can make it raise before sending ("download_raises",
# an error name) or answer badly after sending ("download_answer": "no_task" or "not_json").
def asset_download(data):
    hostile = global_vars.scenario()
    global_vars.note("asset_download_timeout", value=list(TIMEOUT))
    if hostile.get("download_raises"):
        raise type(hostile["download_raises"], (Exception,), {})(SECRET_TEXT)
    global_vars.note("asset_download_sent", base_id=data.get("asset_data", {}).get("assetBaseId"))
    answer = _send({"op": "asset_download", "data": data}, TIMEOUT[1])
    if hostile.get("download_answer") == "no_task":
        return {"error": SECRET_TEXT}
    if hostile.get("download_answer") == "not_json":
        raise type("JSONDecodeError", (ValueError,), {})(SECRET_TEXT)
    return answer


# A request that waits for its answer. The operator's search uses it with a short timeout.
def blocking_request(url, method="GET", headers=None, json_data=None, timeout=1, **kwargs):
    return _send({"op": "search", "url": url, "asset_base_id": (json_data or {}).get("asset_base_id", "")}, timeout)


# Asks the helper to refresh the sign in.
def refresh_token(refresh_token, old_api_key):
    global_vars.note("refresh_token")
    return _send({"op": "refresh_token", "refresh_token": refresh_token})


# Asks the helper to sign out, which revokes key and refresh token.
def oauth2_logout():
    global_vars.note("oauth2_logout")
    return _send({"op": "oauth2_logout", "refresh_token": global_vars.PREFS.get("api_key_refresh", "")})


# Sends settings, which the helper saves in its settings file.
def send_settings(values):
    if applying_client_settings:
        return None
    global_vars.note("settings_posted")
    return _send({"op": "settings", "values": values})


# Cancels one download.
def cancel_download(task_id):
    global_vars.note("cancel_download", task_id=task_id)
    return _send({"op": "cancel", "task_id": task_id})


# Asks the helper to stop.
def shutdown_client():
    global_vars.note("shutdown_client")
    return _send({"op": "shutdown"})


# Lets the last program go.
def unsubscribe_addon():
    return _send({"op": "unsubscribe"})
