"""Timers of the fake add-on. Background Blender registers none, so only a caller runs them."""
from . import bkit_oauth, client_lib, download, global_vars, utils


# Starts the helper when none of this Blender's runs. Returns whether one was started.
def _maybe_start_client(force=False):
    if client_lib.is_client_process_alive() and not force:
        return False
    client_lib.start_blenderkit_client()
    return True


# Polls the helper and handles every task it hands out. A task that raises aborts the rest of the batch.
def client_communication_timer():
    try:
        answer = client_lib.get_reports("fake")
    except ConnectionError:
        global_vars.CLIENT_ACCESSIBLE = False
        _maybe_start_client()
        return 0.1
    global_vars.CLIENT_ACCESSIBLE = True
    for task in answer.get("tasks", []):
        handle_task(task)
    download.drop_silent_tasks()
    return 0.1


# Hands one task to the part of the add-on it is for.
def handle_task(task):
    kind = task.get("task_type")
    if task.get("status") == "error" and "Invalid token" in str(task.get("message", "")):
        task_error_overdrive(task)
    if kind == "asset_download":
        download.handle_download_task(task)
    elif kind == "oauth2_logout":
        bkit_oauth.clean_login_data()
    elif kind == "login":
        bkit_oauth.write_tokens(task.get("api_key", ""), task.get("refresh_token", ""), task)
    elif kind == "settings":
        global_vars.note("settings_applied")
        prefs = utils.get_prefs()
        prefs.send_usage_data = bool(task.get("send_usage_data", True))
        utils.save_prefs_to_file()
    elif kind == "categories":
        global_vars.note("categories_written")
        utils.save_prefs_to_file()
    else:
        global_vars.note("task_other", task_type=kind)


# Refreshes the sign in after an "Invalid token." error.
def task_error_overdrive(task):
    prefs = utils.get_prefs()
    client_lib.refresh_token(prefs.api_key_refresh, prefs.api_key)
