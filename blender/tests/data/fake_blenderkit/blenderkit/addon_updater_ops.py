"""The fake add-on's updater. register makes its folder when it is missing and does not catch a failure."""
import os

from . import global_vars

FOLDER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "blenderkit_updater")


# Makes the updater folder when it is missing.
def register(bl_info=None):
    if not os.path.isdir(FOLDER):
        global_vars.note("updater_folder_made")
        os.makedirs(FOLDER)
