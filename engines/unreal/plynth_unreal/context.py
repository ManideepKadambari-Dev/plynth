import hashlib
import json
import os

from . import ue

# Changes when the way assets are made changes, so every asset is made again once after an update.
MAKER_VERSION = "plynth-unreal-1"


class Context:
    # Holds the job, the result being filled and what the steps learn along the way.
    def __init__(self, job, result):
        self.job = job
        self.result = result
        self.building = job["building"]
        self.content_path = job["content_path"]
        self.source_folder = job["source_folder"].replace("\\", "/").rstrip("/")
        self.meshes_folder = self.content_path + "/Meshes"
        self.kit_folder = self.content_path + "/Kit"
        self.textures_folder = self.content_path + "/Textures"
        self.materials_folder = self.content_path + "/Materials"
        self.data_folder = self.content_path + "/Data"
        self.pack = job["pack_materials"]
        self.expected = set()      # package paths of every asset this job keeps
        self.mesh_assets = {}      # mesh asset name to its package path, for the job's meshes and kit pieces
        self.parents = {}          # blend to the parent material
        self.instances = {}        # slot name, lower case, to its material instance
        self.ownership_checked = False

    # Returns the absolute path of a file the job names inside source_folder.
    def source(self, relative):
        return self.source_folder + "/" + relative

    # Returns the package path of the asset for a job mesh.
    def mesh_path(self, asset_name):
        return self.meshes_folder + "/" + asset_name

    # Returns the package paths of every static mesh of the job: the meshes, then the kit pieces.
    def all_mesh_paths(self):
        paths = [self.mesh_path(mesh["asset"]) for mesh in self.job["meshes"]]
        if self.job["kit"] is not None:
            paths += [self.kit_folder + "/" + piece for piece in self.job["kit"]["pieces"]]
        return paths

    # Marks an asset as one this job keeps, tags it as this building's and records what happened to it.
    def claim(self, asset, path, action, source_hash=None):
        self.expected.add(path)
        if action in ("created", "updated"):
            ue.set_tag(asset, ue.TAG_BUILDING, self.building)
            ue.set_tag(asset, ue.TAG_PROJECT, self.job["project_folder"])
            ue.set_tag(asset, ue.TAG_JOB, self.job["job_id"])
            if source_hash is not None:
                ue.set_tag(asset, ue.TAG_SOURCE, source_hash)
        self.result.asset(path, ue.kind_of(asset), action)

    # Marks an asset that was changed after it was claimed as updated, unless it was just created.
    def touched(self, asset, path):
        if self.result.asset_action(path) == "unchanged":
            ue.set_tag(asset, ue.TAG_JOB, self.job["job_id"])
            self.result.asset(path, ue.kind_of(asset), "updated")

    # Returns True when an asset already holds what the source hash describes.
    def unchanged(self, asset, source_hash):
        return asset is not None and ue.get_tag(asset, ue.TAG_SOURCE) == source_hash


# Returns a short fingerprint of a file's bytes and any extra values that change what is made from it.
def file_hash(path, *extra):
    digest = hashlib.sha1()
    digest.update(MAKER_VERSION.encode("utf-8"))
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    for value in extra:
        digest.update(json.dumps(value, sort_keys=True).encode("utf-8"))
    return digest.hexdigest()


# Returns a short fingerprint of values, for assets that are made from the job alone.
def value_hash(*values):
    digest = hashlib.sha1()
    digest.update(MAKER_VERSION.encode("utf-8"))
    for value in values:
        digest.update(json.dumps(value, sort_keys=True).encode("utf-8"))
    return digest.hexdigest()


# Returns a folder path in a form that compares equal for the same folder, whatever its slashes or case.
def same_folder_key(path):
    return os.path.normcase(os.path.normpath(str(path).replace("\\", "/"))).rstrip("\\/")
