"""Reports of the fake add-on. As the real one, a report needs bpy.context.window."""
import logging

import bpy

bk_logger = logging.getLogger("blenderkit")


# Shows a report and logs its text.
def add_report(text="", timeout=5, type="INFO", details=""):
    _screen = bpy.context.window.screen
    bk_logger.info("report %s %s", text, details)
