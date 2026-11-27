from .. import ue
from ..context import same_folder_key
from ..errors import LinkError
from ..registry import step


# Refuses a building folder whose assets belong to another Plynth project folder.
@step("ownership", 10, "folder_taken")
def run(ctx):
    mine = same_folder_key(ctx.job["project_folder"])
    for path in ue.list_folder(ctx.content_path):
        asset = ue.load(path)
        if asset is None:
            continue
        owner = ue.get_tag(asset, ue.TAG_PROJECT)
        if owner and same_folder_key(owner) != mine:
            raise LinkError("folder_taken", "Content/Plynth/%s belongs to the Plynth project %s. Rename this project."
                            % (ctx.building, owner))
    ctx.ownership_checked = True
