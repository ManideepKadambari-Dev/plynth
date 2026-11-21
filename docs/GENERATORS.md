# Writing generators

This page explains how to add to Plynth without changing the program. Today it covers opening shapes, importers and the Blender worker registries. Generators and roof types are added to this page in later releases.

For the commands see the [Command line guide](CLI_GUIDE.md). For the screens see the [User guide](USER_GUIDE.md).

## Opening shapes

An opening shape is the outline of the cut a door, window or plain opening makes in a wall. Every opening in `plan.json` names its shape by an id, such as `"shape": "round_arch"`.

### The built-in shapes

| Id | Name | What it looks like | Size it needs |
|---|---|---|---|
| `rectangle` | Rectangle | The whole box | Any |
| `round_arch` | Round arch | Straight sides, then a half circle on top | Height at least half the width |
| `flat_arch` | Flat arch | Straight sides, then a shallow curve that rises a sixth of the width above its ends | Height at least a sixth of the width |
| `pointed_arch` | Pointed arch | Straight sides, then two arcs of radius equal to the width that meet in a point on top | Height at least 0.866 times the width |
| `circle` | Circle | The ellipse that touches the four sides of the box. A circle when width equals height | Any |

`plynth opening shapes` lists them with their size rules. An opening that is too low for its shape is refused, and the message gives the rule.

Every shape is drawn inside the opening's box. The box is as wide as the opening and as high as the opening, with its bottom at the sill. The shape never goes outside it. Curved parts are cut into short straight pieces, no more than 7.5 degrees apart and no more than 5 mm off the true curve, so the curve looks smooth at any size.

### Adding your own shape with a file

A custom shape is one small JSON file. You do not change or rebuild Plynth.

**Where it goes.** In the `shapes` folder of the Library, named after the shape's id:

```
Library/
  shapes/
    keyhole.shape.json
    trefoil.shape.json
```

Only files whose names end in `.shape.json` are read. Files are read in name order. Subfolders are ignored. A missing `shapes` folder is fine and simply means there are no custom shapes.

**When it is read.** Plynth reads the `shapes` folder of the library when the library opens, in the app and in the `opening` commands. Files it refuses are reported in the status bar. The library is set up in the setup check (see [Setting up Plynth](SETUP.md)). Without a library only the five built-in shapes exist. Nothing in Plynth has to be edited to add a shape: it shows up in the app's shape list and in `plynth opening shapes`, after the built-in ones, with `built_in` false.

**The format.** The file holds one object. Its shape is described by `schema/shape.schema.json`.

| Field | Meaning |
|---|---|
| `format` | The text `plynth-shape`. |
| `version` | The number `1`. |
| `id` | The id stored in `plan.json`. Lower case letters, digits and `_`, starting with a letter, at most 64 characters. It must differ from every other shape, including the built-in ids `rectangle`, `round_arch`, `flat_arch`, `pointed_arch` and `circle`. |
| `name` | The name shown in the shape list. 1 to 64 characters. |
| `outline` | The outline as a list of points, each point a list of two numbers `[x, y]`. |

Any other field is refused.

**The unit box.** The outline is drawn in a box that is 1 wide and 1 high. `x` runs from 0 at the left to 1 at the right. `y` runs from 0 at the bottom to 1 at the top. The left and right are as you see the wall from its right side. Plynth stretches the box to each opening's width and height, so one file gives the same look for any size. A point at `[0.5, 1]` is always the middle of the top edge.

A curve is made of many short straight pieces. You write the pieces. 20 to 60 points make a smooth circle. The file can hold up to 1000 points.

**Example.** `samples/shapes/keyhole.shape.json` is a keyhole: a round head on a narrower, tapering base. This is the start of it, with most of the points left out:

```json
{
  "format": "plynth-shape",
  "version": 1,
  "id": "keyhole",
  "name": "Keyhole",
  "outline": [
    [0.2, 0],
    [0.8, 0],
    [0.68, 0.458773],
    [0.725245, 0.470021],
    [0.768283, 0.48372],
    [0.5, 1],
    [0.32, 0.458773]
  ]
}
```

The first two points are the bottom corners of the base. The next point is where the base meets the head on the right, and the points after it go round the head anticlockwise (seen with y up) back to the base on the left. Open the sample file to see all 62 points. Copy it, change `id`, `name` and the points, and save it as `<id>.shape.json`.

**The rules.** A file is refused, with a message that names the place in the file, when:

- it is not valid JSON, or is not one object;
- `format` is not `plynth-shape` or `version` is not 1;
- a field is missing, or there is a field that is not in the table above;
- `id` is not a well formed lower case name, or is already taken by another shape;
- `name` is empty or longer than 64 characters;
- the outline has fewer than 3 or more than 1000 points, a point is not two numbers, or a number is below 0 or above 1;
- the outline does not touch all four sides of the box: its smallest and largest `x` must be 0 and 1, and its smallest and largest `y` must be 0 and 1;
- the outline crosses itself, such as a bow tie;
- the outline has no area.

Do not repeat the first point at the end. You can go round clockwise or anticlockwise, and start anywhere: Plynth turns the outline anticlockwise if it needs to. A refused file does not stop the others from loading.

**Sizes.** A custom shape takes any size, so it has no size rule. Make the outline something that still looks right when it is wide or tall.

**Fit and checking.** An opening with a custom shape is checked like any other for position, height and overlap, using its box. A plan that names a custom shape that this machine does not have still opens. The opening is kept. Until the shape file is there, `opening list` shows it as not fitting, with the message that the shape is not known.

### Adding a built-in shape (for developers)

A shape that is part of Plynth is one C++ file. Nothing else is edited.

1. Add a file to `core/src/openings/shapes/`, for example `TrefoilShape.cpp`. Copy `RoundArchShape.cpp` as a start.
2. Write a class that gives the id, the name, a one sentence size rule, a sort key, whether a size fits, and the outline for a size. The outline is in the opening's own box, `u` from minus half the width to half the width and `v` from 0 to the height, anticlockwise, simple, touching all four sides. Cut curves with `arcSegmentCount` so every shape follows the same smoothness rule.
3. At the bottom of the file, make one object at namespace scope: `const OpeningShapeRegistrar registrar(std::make_unique<TrefoilShape>());`. That registers the shape when the program starts.
4. Run CMake again. The build picks up every file in that folder by itself, and links the shapes into the app, the command line and the tests.

A shape that is registered this way shows up in `plynth opening shapes` and in the app's shape list. Add a test next to the existing shape tests that checks its outline for a normal size and for the smallest size it allows.

## Adding an importer

An importer is one way assets come into the library. Today there is one, for your own files (`local_file`). BlenderKit and Fab zips come later, as more importers. This section is for developers.

An importer only decides **what Blender should read**. Everything else is shared by every route: the work folder, the Blender cleanup job, `info.json`, the index, the thumbnail and removing a zip afterwards. That is why a new importer is small.

### The steps

1. Make one new file in `core/src/catalog/importers/`, for example `FabZipImporter.cpp`. CMake finds every `.cpp` in that folder and builds it into the OBJECT library `plynth_importers`, so you do not edit `CMakeLists.txt`. An OBJECT library is used so the linker cannot drop a file that nothing refers to by name.
2. In it, write a class that derives from `Importer` (`core/include/plynth/core/catalog/Importer.h`) and implements:

| Function | What it does |
|---|---|
| `id()` | A stable lower case id, such as `"fab_zip"`. A taken or badly formed id is refused and the importer is dropped. |
| `name()` | A short name for the import screen. |
| `description()` | One plain sentence on what it takes. |
| `fileSuffixes()` | The endings it reads, lower case with the dot. They fill the file dialog and `importableSuffixes()`. Empty for an importer that takes web addresses. |
| `score(source)` | How well it handles this source. 0 means not at all. The highest score wins, and ties go to the lower id. |
| `plan(source, workFolder, out)` | Decides what Blender reads. Fills the `ImportPlan` (below). Returns `InvalidValue` with a plain sentence when the source cannot be used after all. |

3. At the bottom of the file, register it at namespace scope with one line:

```cpp
const ImporterRegistrar registrar(std::make_unique<FabZipImporter>());
```

That line runs when the program starts and adds the importer to the list, which is kept in a function local static so the order of files does not matter.

### What `plan` fills

| Field | Meaning |
|---|---|
| `file` | The file Blender reads. It may be inside the work folder, for a file the importer made or unpacked. |
| `format` | A format id from `AssetJobs.h` (`obj`, `fbx`, `gltf`, `blend`, `zip`). It must match a file format module in `blender/plynth/importers/`. |
| `sourceKind` | Written to `info.json`, for example `local_file`. |
| `reference` | Written to `info.json`: the original file name or the address. |
| `defaultName` | The name when the user gave none. |
| `license` | The license when the user gave none. Empty means `unknown`. |
| `originalIsZip` | True when the original is a zip that should go to the Recycle Bin after a successful import, unless the user keeps it. |

### Picking between importers

`pickImporter` asks every importer for a score and takes the highest. The local file importer gives 10 for its endings and for folders that hold a model. An importer that recognises a particular kind of zip can give more, and so win for those zips without any change to the local one. This is the reason scores exist.

### What does not change

No existing code is edited: not the app, not the CLI, not `AssetImport.cpp`. The new importer shows up in `plynth assets importers`, in the file dialog, and in drag and drop. A test finds an importer by id from the test program, so if the registration trick ever breaks, a test fails.

Read `core/src/catalog/importers/LocalFileImporter.cpp` as the worked example. Tests for importers use the fake Blender (`tests/tools/fake_blender`) and need no real Blender: see `tests/core/ImporterTests.cpp`.

## Blender worker registries

Blender does the heavy work in the background. Its side is Python, in `blender/plynth/`, and it follows the same idea as the C++ side: **a registry, and one file for each entry**. This section is for developers.

### How a file registers itself

`blender/plynth/registry.py` has a small `Registry` class and four registries: `TASKS`, `IMPORTERS`, `CLEANUP_STEPS` and `CHECKS`. Each has `register(id, thing)`, which refuses a duplicate id, plus `get`, `all` and `ids`.

Each package (`tasks/`, `importers/`, `cleanup/`, `checks/`) is loaded with `discover(package)`, which imports every module in the folder in name order. A module registers itself on its last line as it is imported. So adding an entry is adding one file. Nothing else is edited: no list, no import. Only the Python standard library and `bpy` are used.

### Tasks

A task is what a job asks for. Files in `plynth/tasks/`: `probe`, `cleanup_asset`, `render_thumbnail`. A task is a function `run(ctx)` registered with `TASKS.register("name", run)`. It reads `ctx.params`, fills `ctx.output`, adds warnings with `ctx.warn(code, message)`, and raises `WorkerError(code, message)` to fail with a named error. The C++ side must also know the task name and its parameters and output: see `core/include/plynth/core/BlenderJob.h` and `catalog/AssetJobs.h`.

### File format importers

One file for each format in `plynth/importers/`, such as `obj.py`:

```python
import bpy

from . import ImporterEntry
from ..registry import IMPORTERS


# Imports an OBJ file with Blender's default axes, so the file's front becomes -Y.
def load(ctx, path):
    bpy.ops.wm.obj_import(filepath=path)


IMPORTERS.register("obj", ImporterEntry((".obj",), load))
```

The entry holds the file endings it reads and a `load(ctx, path)` function that brings the model into the scene. The id is the format id the C++ importer puts in `ImportPlan::format`. The zip entry unpacks safely into the work folder (it refuses absolute paths, `..` and drive names, and stops above 4 GB), picks the one model inside and hands it to the entry for its ending.

### Cleanup steps

One file for each step in `plynth/cleanup/`. A step has an `ORDER` number, a sentence shown as progress, and `run(ctx)`:

```python
ORDER = 50

def run(ctx):
    ...

CLEANUP_STEPS.register("set_pivot", Step(ORDER, "Setting the pivot", run))
```

Steps run in the order of their numbers: import_source 10, remove_extras 20, user_transform 30, apply_transforms 40, set_pivot 50, name_objects 60, collect_textures 70, measure 80, run_checks 90, save_blend 100, render_thumbnail 110. The gaps let a new step slot in between. Each step sends a progress line before it runs. Steps should be safe to run twice on an already clean asset, because `render_thumbnail` and rescan open assets that were cleaned before. The conventions every cleaned asset follows are in `core/include/plynth/core/catalog/AssetInfo.h`.

### Checks

One file for each check in `plynth/checks/`, run by the `run_checks` step. A check is a function `run(ctx)` registered with `CHECKS.register("polycount", run)`. It reads the job parameters and the measured values, and adds warnings with `ctx.warn`. A check never refuses an import. Look at `checks/polycount.py` for the smallest example.

### The job and result contract

C++ and Python talk through files, described in `schema/blender-job.schema.json` and `schema/blender-result.schema.json`.

- Plynth writes `job.json`: `format`, `version`, `task`, `job_id`, `result_file` and `params`. The folder holding `result_file` is the job's work folder.
- It starts `blender --background --factory-startup -noaudio --python-exit-code 3 --python plynth_worker.py -- --job <file>`. The entry point `plynth_worker.py` reads the job, finds the task in the registry and runs it.
- While running, the worker prints progress lines: `PLYNTH:{"progress":{"step":"set_pivot","index":4,"count":11,"message":"Setting the pivot"}}`. Use `jobs.progress(...)`.
- At the end it writes `result.json` through a temporary file and a rename: `ok`, `error`, `blender_version`, `warnings`, `elapsed_ms` and `output`. The worker never lets an exception escape without trying to write a result. An uncaught error exits with code 3 and C++ reports `blender_failed` with the end of the log.

Error codes a task may raise: `bad_job`, `unknown_task`, `unsupported_format`, `import_failed`, `no_geometry`, `several_models`, `unpack_failed`, `save_failed`, `render_failed`, `internal`. Warning codes: `high_polycount`, `scale_suspicious`, `missing_texture`, `removed_objects`, `unsupported_object`, `eevee_failed`.

### Testing

The Python tests run inside Blender: `blender -b --factory-startup --python blender/tests/run_tests.py -- [pattern]`. `ctest -L blender` runs them when Blender is found. Every registry has a test that refuses a duplicate id. The sample chair in `samples/models/chair` is the model most tests use.

## Other things you will be able to add

Later releases add the same kind of page for generators (small Blender scripts), exporters and roof types. Each one will be added to this page, with its file format and its rules.
