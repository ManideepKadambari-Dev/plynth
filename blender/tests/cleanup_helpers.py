import os
import shutil
import zipfile

import bpy

import helpers
from plynth import scene

CHAIR_SOURCE = os.path.join(helpers.REPO_FOLDER, "samples", "models", "chair")
BACKGROUND = [0.0549, 0.0667, 0.0863]


class CleanupTest(helpers.TempFolderTest):
    # Copies the sample chair folder to a temporary place and returns its path.
    def copy_chair(self):
        target = self.path("in", "chair")
        shutil.copytree(CHAIR_SOURCE, target)
        return target

    # Builds the params of a cleanup_asset job. Extra keyword arguments replace the defaults.
    def cleanup_params(self, source_file, source_format, **overrides):
        params = {
            "source": {"file": source_file.replace("\\", "/"), "format": source_format},
            "asset_id": "chair",
            "output_folder": self.path("item"),
            "scale": 1.0,
            "rotate_z": 0,
            "triangle_warning": 500000,
            "thumbnail": {"render": False, "size": 64, "background": BACKGROUND},
        }
        params.update(overrides)
        return params

    # Runs the cleanup of one file and returns the result.
    def cleanup(self, source_file, source_format, **overrides):
        return self.run_task("cleanup_asset", self.cleanup_params(source_file, source_format, **overrides))

    # Opens the cleaned asset.blend of the last run.
    def open_asset(self):
        bpy.ops.wm.open_mainfile(filepath=self.path("item", "asset.blend"))
        return scene.scene_objects()

    # Returns the sorted warning codes of a result.
    def codes(self, result):
        return sorted(w["code"] for w in result["warnings"])

    # Makes a zip with the given (name in zip, file on disk) pairs.
    def make_zip(self, name, entries):
        path = self.path("in", name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with zipfile.ZipFile(path, "w") as archive:
            for arcname, source in entries:
                archive.write(source, arcname)
        return path
