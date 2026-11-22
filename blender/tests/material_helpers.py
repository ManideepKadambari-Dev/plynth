import json
import os
import shutil
import zipfile

import numpy as np

import helpers
from plynth.materials import read_pixels, to_bytes, write_png
from schema_check import load_schema, validate

SAMPLES = os.path.join(helpers.REPO_FOLDER, "samples", "materials")
BACKGROUND = [0.0549, 0.0667, 0.0863]
CHANNELS = ("base_color", "normal", "roughness", "metallic", "opacity", "occlusion")


# Returns a noisy RGB image of size x size pixels as bytes, the same for a given seed.
def noise_image(size=64, seed=1):
    return np.random.RandomState(seed).randint(0, 256, (size, size, 3)).astype(np.uint8)


# Returns a smooth RGB image that rises from left to right, which has a hard seam where it repeats.
def gradient_image(size=64):
    row = np.linspace(20, 235, size).astype(np.uint8)
    return np.repeat(np.repeat(row[None, :, None], size, axis=0), 3, axis=2)


# Returns a smooth image that wraps without a seam.
def tileable_image(size=64):
    angle = np.arange(size) / size * 2 * np.pi
    plane = 0.5 + 0.4 * np.sin(angle)[None, :] * np.cos(angle)[:, None]
    return np.repeat((plane * 255).astype(np.uint8)[:, :, None], 3, axis=2)


# Returns an image of one colour.
def flat_image(rgb, size=32):
    return np.tile(np.array(rgb, dtype=np.uint8), (size, size, 1))


class MaterialTest(helpers.TempFolderTest):
    # Writes png files into a folder of the temporary area. files maps a name to an image array or to bytes.
    def make_set(self, name, files):
        folder = self.path("in", name)
        os.makedirs(folder, exist_ok=True)
        for filename, content in files.items():
            target = os.path.join(folder, filename)
            if isinstance(content, (bytes, str)):
                mode = "wb" if isinstance(content, bytes) else "w"
                with open(target, mode) as handle:
                    handle.write(content)
            else:
                write_png(target, content)
        return folder

    # Writes a material.json into a folder.
    def write_sidecar(self, folder, **fields):
        data = {"format": "plynth-material-source", "version": 1}
        data.update(fields)
        with open(os.path.join(folder, "material.json"), "w", encoding="utf-8") as handle:
            json.dump(data, handle)

    # Copies a sample material folder into the temporary area and returns the new path.
    def copy_sample(self, material_id):
        target = self.path("in", material_id)
        shutil.copytree(os.path.join(SAMPLES, material_id), target)
        return target

    # Builds the params of a cleanup_material job. Keyword arguments replace the defaults.
    def clean_params(self, path, kind="texture_set", maps=None, values=None, normal="auto", **overrides):
        params = {
            "source": {"kind": kind, "path": path.replace("\\", "/"), "material": None, "maps": maps or {},
                       "values": values or {}, "normal": normal},
            "output_folder": self.path("out"),
            "tile_size": None,
            "bake_size": 1024,
            "thumbnail": {"render": False, "size": 128, "background": BACKGROUND},
        }
        params.update(overrides)
        return params

    # Runs one cleanup and returns the result. The job is checked against the job schema first, unless
    # check_job is false (to try a job the schema would refuse). A set made by a test gets a small tile so
    # its few pixels are not reported as low resolution.
    def clean(self, path, check_job=True, **kwargs):
        if "tile_size" not in kwargs and not os.path.abspath(path).startswith(os.path.abspath(SAMPLES)):
            kwargs["tile_size"] = {"width": 0.1, "height": 0.1}
        params = self.clean_params(path, **kwargs)
        self.out_folder = params["output_folder"]
        if check_job:
            job_file, _ = self.write_job("cleanup_material", params)
            with open(job_file, "r", encoding="utf-8") as handle:
                problems = validate(json.load(handle), load_schema("blender-job"))
            self.assertEqual(problems, [], "job does not match the schema")
        return self.run_task("cleanup_material", params)

    # Cleans and checks that it worked and the result matches the schema. Returns the result.
    def clean_ok(self, path, **kwargs):
        result = self.clean(path, **kwargs)
        self.assertTrue(result["ok"], result["error"])
        self.assert_result_valid(result)
        return result

    # Returns the sorted warning codes of a result.
    def codes(self, result):
        return sorted(w["code"] for w in result["warnings"])

    # Returns the path of a made map by channel.
    def map_path(self, result, channel):
        return os.path.join(self.out_folder, *result["output"]["maps"][channel]["file"].split("/"))

    # Returns the bytes of a file.
    def read_bytes(self, path):
        with open(path, "rb") as handle:
            return handle.read()

    # Returns the pixels of a file as bytes, shape (height, width, 4), top row first.
    def pixels(self, path):
        return to_bytes(read_pixels(path))

    # Makes a zip of a folder with every file at the top, and returns its path.
    def zip_folder(self, folder, name="set.zip", prefix=""):
        target = self.path("in", name)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with zipfile.ZipFile(target, "w") as archive:
            for current, _, files in os.walk(folder):
                for filename in files:
                    full = os.path.join(current, filename)
                    archive.write(full, prefix + os.path.relpath(full, folder).replace("\\", "/"))
        return target
