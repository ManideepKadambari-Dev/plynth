from .. import ue
from ..registry import step


# Saves the building's changed assets, so they and their Plynth tags are on disk. The level is left unsaved.
@step("save", 120, "import_failed")
def run(ctx):
    ue.save_folder(ctx.content_path)
