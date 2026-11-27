import json
import os
import re

from .errors import LinkError

JOB_FORMAT = "plynth-unreal-job"
JOB_VERSION = 1
CHANNELS = ("base_color", "normal", "roughness", "metallic", "opacity", "occlusion")
BLENDS = ("opaque", "masked", "translucent")

ASSET_NAME = re.compile(r"^[A-Za-z0-9_-]+$")
JOB_ID = re.compile(r"^link-[0-9a-f]{8}$")
CONTENT_PATH = re.compile(r"^/Game/Plynth/[A-Za-z0-9_-]+$")
GROUP = re.compile(r"^[a-z][a-z0-9_]*$")
MESH_FILE = re.compile(r"^[A-Za-z0-9_-]+\.fbx$")
KIT_FILE = re.compile(r"^[A-Za-z0-9_-]+_Kit\.fbx$")
TEXTURE_FILE = re.compile(r"^Textures/[A-Za-z0-9_.-]+$")
DATA_FILE = re.compile(r"^[A-Za-z0-9_.-]+$")
LEVEL_TAG = re.compile(r"^Plynth:[A-Za-z0-9_-]+$")

TOP_KEYS = ("format", "version", "job_id", "plynth_version", "building", "project_folder", "content_path",
            "source_folder", "pack_materials", "grouped", "meshes", "kit", "textures", "materials", "data_files",
            "level", "checks", "result_file")


# Reads the job file and checks it has the form of schema/unreal-job.schema.json. Raises bad_job when not.
def read(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            job = json.load(handle)
    except (OSError, ValueError) as error:
        raise LinkError("bad_job", "The job file %s could not be read: %s" % (path, error))
    validate(job)
    return job


# Returns the result file and job id named by a job that may be broken, so even a bad job gets an answer.
def answer_address(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            raw = json.load(handle)
    except (OSError, ValueError):
        return None, ""
    if not isinstance(raw, dict):
        return None, ""
    result_file = raw.get("result_file")
    job_id = raw.get("job_id")
    return (result_file if isinstance(result_file, str) and result_file else None,
            job_id if isinstance(job_id, str) else "")


# Raises bad_job naming the first field that breaks the schema.
def fail(where, what):
    raise LinkError("bad_job", "The job file is not valid: %s %s." % (where, what))


# Checks an object has exactly the keys given.
def keys(value, where, names):
    if not isinstance(value, dict):
        fail(where, "is not an object")
    for name in names:
        if name not in value:
            fail(where, "has no field %s" % name)
    for name in value:
        if name not in names:
            fail(where, "has an unknown field %s" % name)


# Checks a value is text matching the pattern.
def text(value, where, pattern=None, min_length=0, max_length=None):
    if not isinstance(value, str):
        fail(where, "is not text")
    if len(value) < min_length:
        fail(where, "is empty")
    if max_length is not None and len(value) > max_length:
        fail(where, "is too long")
    if pattern is not None and not pattern.match(value):
        fail(where, "has the wrong form")


# Checks a value is an asset name.
def asset_name(value, where):
    text(value, where, ASSET_NAME, 1, 128)


# Checks a value is an asset name or null.
def optional_asset(value, where):
    if value is not None:
        asset_name(value, where)


# Checks a value is true or false.
def boolean(value, where):
    if not isinstance(value, bool):
        fail(where, "is not true or false")


# Checks a value is a number from 0 to 1.
def unit_number(value, where):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not 0.0 <= value <= 1.0:
        fail(where, "is not a number from 0 to 1")


# Checks a value is a list of three numbers.
def xyz(value, where):
    if not isinstance(value, list) or len(value) != 3:
        fail(where, "is not three numbers")
    for number in value:
        if isinstance(number, bool) or not isinstance(number, (int, float)):
            fail(where, "is not three numbers")


# Checks a file name is not only dots, so it always names a file inside its folder.
def plain_file(name, where):
    if name.replace(".", "") == "" or name.split("/")[-1].replace(".", "") == "":
        fail(where, "is not a file name")


# Checks the whole job.
def validate(job):
    keys(job, "the job", TOP_KEYS)
    if job["format"] != JOB_FORMAT:
        fail("format", "is not %s" % JOB_FORMAT)
    if isinstance(job["version"], bool) or job["version"] != JOB_VERSION:
        fail("version", "is not %d" % JOB_VERSION)
    text(job["job_id"], "job_id", JOB_ID)
    text(job["plynth_version"], "plynth_version")
    asset_name(job["building"], "building")
    text(job["project_folder"], "project_folder", min_length=1)
    text(job["content_path"], "content_path", CONTENT_PATH)
    if job["content_path"] != "/Game/Plynth/" + job["building"]:
        fail("content_path", "is not /Game/Plynth/<building>")
    text(job["source_folder"], "source_folder", min_length=1)
    boolean(job["pack_materials"], "pack_materials")
    boolean(job["grouped"], "grouped")
    validate_meshes(job["meshes"])
    validate_kit(job["kit"])
    validate_textures(job["textures"])
    validate_materials(job["materials"])
    if not isinstance(job["data_files"], list):
        fail("data_files", "is not a list")
    for index, name in enumerate(job["data_files"]):
        text(name, "data_files[%d]" % index, DATA_FILE)
        plain_file(name, "data_files[%d]" % index)
    keys(job["level"], "level", ("place_actors", "tag"))
    boolean(job["level"]["place_actors"], "level.place_actors")
    text(job["level"]["tag"], "level.tag", LEVEL_TAG)
    validate_checks(job["checks"])
    text(job["result_file"], "result_file", min_length=1)
    if not job["pack_materials"] and (job["textures"] or job["materials"]):
        fail("textures and materials", "are not empty while pack_materials is false")
    if not os.path.isdir(job["source_folder"]):
        fail("source_folder", "is not a folder on this computer")


# Checks the meshes list.
def validate_meshes(meshes):
    if not isinstance(meshes, list) or not meshes:
        fail("meshes", "is not a list with at least one mesh")
    assets = set()
    for index, mesh in enumerate(meshes):
        where = "meshes[%d]" % index
        keys(mesh, where, ("group", "file", "asset", "actor"))
        text(mesh["group"], where + ".group", GROUP)
        text(mesh["file"], where + ".file", MESH_FILE)
        asset_name(mesh["asset"], where + ".asset")
        asset_name(mesh["actor"], where + ".actor")
        if mesh["asset"].lower() in assets:
            fail(where + ".asset", "is listed twice")
        assets.add(mesh["asset"].lower())


# Checks the kit, which is null or a file with its pieces.
def validate_kit(kit):
    if kit is None:
        return
    keys(kit, "kit", ("file", "pieces"))
    text(kit["file"], "kit.file", KIT_FILE)
    if not isinstance(kit["pieces"], list):
        fail("kit.pieces", "is not a list")
    for index, piece in enumerate(kit["pieces"]):
        asset_name(piece, "kit.pieces[%d]" % index)


# Checks the textures list.
def validate_textures(textures):
    if not isinstance(textures, list):
        fail("textures", "is not a list")
    for index, texture in enumerate(textures):
        where = "textures[%d]" % index
        keys(texture, where, ("file", "asset", "channel", "srgb"))
        text(texture["file"], where + ".file", TEXTURE_FILE)
        plain_file(texture["file"], where + ".file")
        asset_name(texture["asset"], where + ".asset")
        if texture["channel"] not in CHANNELS:
            fail(where + ".channel", "is not a known channel")
        boolean(texture["srgb"], where + ".srgb")


# Checks the materials list.
def validate_materials(materials):
    if not isinstance(materials, list):
        fail("materials", "is not a list")
    for index, material in enumerate(materials):
        where = "materials[%d]" % index
        keys(material, where, ("slot", "asset", "blend", "two_sided", "maps", "values"))
        asset_name(material["slot"], where + ".slot")
        asset_name(material["asset"], where + ".asset")
        if material["blend"] not in BLENDS:
            fail(where + ".blend", "is not opaque, masked or translucent")
        boolean(material["two_sided"], where + ".two_sided")
        keys(material["maps"], where + ".maps", CHANNELS)
        for channel in CHANNELS:
            optional_asset(material["maps"][channel], "%s.maps.%s" % (where, channel))
        values = material["values"]
        keys(values, where + ".values", ("base_color", "roughness", "metallic", "opacity"))
        color = values["base_color"]
        if not isinstance(color, list) or len(color) != 3:
            fail(where + ".values.base_color", "is not three numbers")
        for number in color:
            unit_number(number, where + ".values.base_color")
        for name in ("roughness", "metallic", "opacity"):
            unit_number(values[name], "%s.values.%s" % (where, name))


# Checks the checks object, which is null outside the headless test.
def validate_checks(checks):
    if checks is None:
        return
    keys(checks, "checks", ("expected_bounds", "screenshot"))
    bounds = checks["expected_bounds"]
    if bounds is not None:
        keys(bounds, "checks.expected_bounds", ("min", "max"))
        xyz(bounds["min"], "checks.expected_bounds.min")
        xyz(bounds["max"], "checks.expected_bounds.max")
    if checks["screenshot"] is not None:
        text(checks["screenshot"], "checks.screenshot", min_length=1)
