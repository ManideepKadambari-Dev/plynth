"""The fake add-on's background job timer. It writes into the user's home when run."""
import os

from . import global_vars


# Notes that it ran and writes a marker in the home folder.
def bg_update():
    global_vars.note("bg_update_ran")
    with open(os.path.join(os.path.expanduser("~"), "bk_bg_update.txt"), "w", encoding="utf-8") as handle:
        handle.write("ran")
    return 0.5
