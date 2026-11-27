from .. import ue
from ..registry import step


# Makes Content/Plynth, the building folder and its Meshes folder when they are missing.
@step("folders", 20, "import_failed")
def run(ctx):
    for folder in ("/Game/Plynth", ctx.content_path, ctx.meshes_folder):
        ue.make_folder(folder)
