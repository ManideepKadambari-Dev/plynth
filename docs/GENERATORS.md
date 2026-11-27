# Writing generators

This page explains how to add to Plynth without changing the program. It covers generators, opening shapes, roof types, stair types, importers, exporters, group rules, the Blender worker registries, build steps, opening fills, export steps and quality checks.

For the commands see the [Command line guide](CLI_GUIDE.md). For the screens see the [User guide](USER_GUIDE.md).

## Generators

A generator is one Python file that Blender runs during the build to make a piece of the building from a few values. Adding one is adding one file to the `generators` folder of your library. Plynth reads only its declaration and never runs the file itself. Only the build in Blender runs it.

`samples/generators/column.py` is a full example. `railing.py` is a second one, and `archimesh_column.py`, `home_builder_cabinets.py`, `bool_tool_screen.py` and `bagapie_fence.py` show generators that use an add-on.

### The file

The file name without `.py` is the generator's id: lower case letters, digits and `_`, starting with a letter. Files whose name starts with `_`, and subfolders, are skipped, so a generator can keep helper modules there. A file may be at most 1 MB.

The first thing in the file, after blank lines and `#` comment lines, is a docstring. Its first line is `plynth-generator` and the rest is one JSON object, the declaration:

```python
"""plynth-generator
{"format": "plynth-generator", "version": 1, "name": "Column",
 "description": "A round or square column from the floor up.",
 "addons": [],
 "size": {"width": {"param": "diameter"}, "depth": {"param": "diameter"}, "height": {"param": "height"}},
 "params": [
  {"id": "diameter", "label": "Diameter", "type": "number", "unit": "m", "default": 0.3, "min": 0.05, "max": 2.0},
  {"id": "height", "label": "Height", "type": "number", "unit": "m", "default": "ceiling_height", "min": 0.1, "max": 20},
  {"id": "shape", "label": "Shape", "type": "choice", "default": "round",
   "choices": [{"id": "round", "label": "Round"}, {"id": "square", "label": "Square"}]},
  {"id": "material", "label": "Material", "type": "material", "default": "white_paint"}]}
"""
```

The full rules are in `schema/generator.schema.json`.

- `name` is shown in the app. `description` is one or two plain sentences for the picker.
- `addons` lists the short names of the Blender add-ons it needs, such as `archimesh` or `bagapie`. Leave it empty when it needs none.
- `params` holds 1 to 32 values. Each has an `id`, a `label`, a `type` and a `default`, and may have a `help` sentence for the tooltip.
- `size` says how large the result is, with `width` along its own x, `depth` along its own y and `height` up. Each side is a fixed length in metres, or `{"param": "<id>", "times": 1, "plus": 0}` for a number or integer parameter. The plan draws this footprint, so fit and overlap checks work without running Blender.

### Parameter types

| Type | Values | Extra fields |
|---|---|---|
| `number` | A number. With `"unit": "m"` the default may be `ceiling_height`, the ceiling height of the floor at the time the item is placed | `unit` (`none`, `m` or `deg`), `min`, `max`, `step` |
| `integer` | A whole number | `min`, `max`, `step` |
| `boolean` | `true` or `false` | |
| `choice` | One of the choice ids | `choices`: a list of `{"id", "label"}` |
| `material` | A materials catalog id, or `null` for the neutral look | |

Lengths are always metres and angles degrees. The app shows a box for each parameter, and the command line sets one with `--param id=value`.

### generate(gen)

The file defines `generate(gen)`. It makes the mesh around the origin, with its bottom centre at (0, 0, 0) and its front toward -Y, and returns the objects it made. The build joins them into one object, moves it to the item's place and turn, and puts it on the item's floor.

| Name | What it is |
|---|---|
| `gen.params` | The values, by parameter id. Numbers are floats and integers are ints |
| `gen.size` | The declared size as (width, depth, height) in metres |
| `gen.item` | The id of the item in the plan |
| `gen.seed` | A number that stays the same for the item, for anything random |
| `gen.mesh_object(name, verts, faces)` | Makes a mesh object from vertices and faces and returns it |
| `gen.link(obj)` | Takes an object made another way, such as by an add-on, into the generator's collection |
| `gen.material(param_id)` | The Blender material of a material parameter, named after its slot in the export |
| `gen.bridge(short_name)` | The bridge of an add-on the declaration lists, with the add-on switched on |
| `gen.log(text)` | Writes a line to the build log |

When the generator gives no material to a face, the first material parameter is used, or a neutral material when it has none. Everything else the generator made is removed after the build has taken the result.

### Using an add-on

List the add-on in `addons` and ask for its bridge. A bridge is a small Plynth module in `blender/plynth/bridges/` that calls the add-on with fixed settings and hands back plain objects:

```python
def generate(gen):
    p = gen.params
    api = gen.bridge("archimesh").api
    made = api.column(height=p["height"], width=p["width"] / 1.3, shape=p["shape"], plates=True,
                      plate_width=p["width"])
    for obj in made:
        gen.link(obj)
        obj.data.materials.clear()
        obj.data.materials.append(gen.material("material"))
    return made
```

A new bridge registers itself with `register(addon, modules, api)` from `plynth.bridges`, so adding one is adding one file there. Without the add-on switched on, the generator shows as unavailable in the picker, the command line refuses to place it, and an item that uses it is left out of the build with the warning `generator_unavailable`. The build only ever switches an add-on on in its own Blender run, never in your saved Blender preferences.

### Rules and checks

- A declaration that breaks a rule makes the file refused. `plynth generator list` names each refused file and why, and the app leaves it out.
- An error inside `generate(gen)` is the build problem `generator_failed`, with the last line of the error.
- A result that reaches more than 1 cm past the declared size is the warning `generator_size`.
- When you change a file, placed items keep their stored values. **Update** in the app, or `plynth item params <id> --fit`, keeps the values that still fit and defaults the rest.
- A generator is code that runs inside Blender with your rights. Only use generators from people you trust.

### Trying one

Copy the file into `Library\generators\`, then choose **Catalog > Reload generators** in the app, or run `plynth generator list`. `plynth generator info <id>` shows its parameters and default size, and `plynth place --generator <id> --room <room>` places it. Build to see the result.

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

## Adding a roof type

A roof type turns one roof section into finished faces. Roof sections are worked out from the floors, so a type never sees walls, only an outline. The five built-in types are `flat`, `gable`, `hip`, `shed` and `sawtooth`. `plynth roof types` lists them with the settings each uses.

A roof type is one C++ file. Nothing else is edited.

1. Add a file to `core/src/roofs/types/`, for example `MansardRoof.cpp`. Copy `ShedRoof.cpp` as a start, because it is the shortest.
2. Write a class that derives from `RoofType` (`core/include/plynth/core/Roofs.h`) and gives:
   - `id()`, the name stored in `plan.json`: lower case letters, digits and `_`, starting with a letter. It must differ from every other roof type.
   - `name()`, the short name shown in the app, and `sortKey()`, which orders the list.
   - `usesPitch()`, `usesDirection()`, `usesOverhang()` and `usesParapet()`. The app shows only the fields a type uses, and `plynth roof types` lists them.
   - `fits(input)`. Return an empty status when the type can roof this section. Otherwise return a status with a plain sentence, and the build uses `fallback()` and reports `roof_type_fallback` with that sentence. The hip roof does this for a section that is not convex.
   - `fallback()`, the id of the type to use then. `flat` for a type that fits every section.
   - `build(input)`, the faces.
3. At the bottom of the file, make one object at namespace scope: `const RoofTypeRegistrar registrar(std::make_unique<MansardRoof>());`. That registers the type when the program starts.
4. Run CMake again. The build picks up every file in that folder and links it into the app, the command line and the tests.

**What `build` gets.** A `RoofInput` in world coordinates, in metres. `section` is the area of the section. `eaves` is the outline the roof may cover: the section pushed outward by the overhang, minus the footprint of the floor above. `above` is the footprint of the floor above, empty on the top floor. Edges that lie along it are closed by that floor's wall, so give them no edge face and no parapet; `roofparts::freeParts` returns the parts of an edge that are free. `base` is the height the roof sits on, the top of the slab. `settings` holds the type, pitch, overhang, parapet and direction. `direction` is the direction already worked out: from `settings.direction` when it is set, and along the longer side of the section when it is automatic.

**What `build` returns.** A list of `RoofFace`. Every face is one flat polygon in world coordinates, with an optional list of holes. A face is tagged with the part it is, so each part gets the right material:

| Part | What it is |
|---|---|
| `covering` | The top surface. It gets the roof or flat roof material. |
| `underside` | The surface under the eaves. |
| `edge` | The thickness round the edge. |
| `gable` | A vertical triangle or trapezoid that closes the end of a pitched roof. |
| `parapet` | Every face of the low wall round a flat roof. |
| `glazing` | Vertical glass, as on a sawtooth roof. |

The points of a face must lie on one plane, and the outline must run anticlockwise as seen from the side the face looks toward. Blender does not fix a face that is wrong. It only makes a mesh from what it is given, so keep every rule in the C++ file. `core/src/roofs/RoofParts.h` has helpers for the common work: cutting a polygon by a line, shrinking it, flat and vertical faces, a face on a plane, gable fills, and `pitchedFaces`, which makes the covering, underside, edge and gable faces of a roof made of planes. The hip roof gets its faces by letting the lowest plane win where planes meet.

Add a test next to the roof tests. Check that every face is flat within 1e-6 m, that the faces cover the eaves outline, and that the type falls back as it should on a section it does not fit. Nothing else changes: the type shows up in `plynth roof types`, in the Properties panel, in the roof sections on the plan and in the build.

## Adding a stair type

A stair type turns a rise, a width and a turn into steps. Core places the steps on the plan, works out the hole in the floors and checks for problems. The four built-in types are `straight`, `l_shape`, `u_shape` and `spiral`. `plynth stair types` lists them.

A stair type is one C++ file.

1. Add a file to `core/src/stairs/types/`, for example `WinderStair.cpp`. Copy `StraightStair.cpp` as a start.
2. Write a class that derives from `StairType` (`core/include/plynth/core/Stairs.h`), or from `BuiltInStair` in `core/src/stairs/StairSupport.h`, which handles the turn for you. It gives:
   - `id()`, `name()` and `sortKey()` as for a roof type.
   - `usesTurn()`, true when the type has a left or right. The app hides the Turn field otherwise.
   - `shape(input)`, the steps.
3. At the bottom of the file, make one object at namespace scope: `const StairTypeRegistrar registrar(std::make_unique<WinderStair>());`.
4. Run CMake again.

**What `shape` gets.** A `StairInput` with the `rise` to climb, the `width`, the `turn`, the `riserCount` and the `riser` height. The number of risers is worked out in core: at most 0.19 m each, all equal, and one tread fewer than there are risers, because the last riser steps onto the upper floor.

**The frame.** Steps are in the stair's own frame, in metres. `u` runs across the stair to the right of the climb, `v` runs forward along the climb, and `z` runs up from the level of the lower floor. The point (0, 0) is the middle of the front edge of the bottom step. Core turns and moves the frame onto the plan.

**What `shape` returns.** A `StairShape`: a list of steps from the bottom up, and a walk line from (0, 0) to where the climb reaches the upper floor. The plan draws the walk line as an arrow.

Each `StairStep` has a `part` (`step`, `landing` or `column`), an outline, a `bottom` and a `top`. The outline is one convex polygon with the points anticlockwise. Blender raises it from `bottom` to `top` as a closed solid, with no gap and no overlap. A step in a straight flight is a box from the floor up to its tread, so the stair is solid underneath. Cut curves with `arcSegmentCount`, so every type follows the same smoothness rule.

The footprint of the stair, which is the hole in the slab and the ceilings, is the union of the step outlines. Add a test next to the stair tests: every outline has a positive area and is convex, the last step reaches the rise, the walk line ends at the arrival, and the number of steps matches the risers.

## Adding an importer

An importer is one way assets come into the library. Today there are three: `local_file` for your own files, `fab_zip` for zips downloaded from Fab and `blenderkit` for BlenderKit addresses. This section is for developers.

An importer only decides **what Blender should read**. Everything else is shared by every route: the work folder, the Blender cleanup job, `info.json`, the index, the thumbnail and removing a zip afterwards. That is why a new importer is small.

### The steps

1. Make one new file in `core/src/catalog/importers/`, for example `FabZipImporter.cpp`. CMake finds every `.cpp` in that folder and builds it into the OBJECT library `plynth_importers`, so you do not edit `CMakeLists.txt`. An OBJECT library is used so the linker cannot drop a file that nothing refers to by name.
2. In it, write a class that derives from `Importer` (`core/include/plynth/core/catalog/Importer.h`) and implements:

| Function | What it does |
|---|---|
| `id()` | A stable lower case id, such as `"fab_zip"`. A taken or badly formed id is refused and the importer is dropped. |
| `name()` | A short name for the Import materials window. |
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
| `format` | A format id: `obj`, `fbx`, `gltf`, `blend` or `zip` from `AssetJobs.h`, or `fab` from `FabZip.h`. It must match a file format module in `blender/plynth/importers/`. A new format also adds its id to the `format` list of `cleanup_asset` in `schema/blender-job.schema.json`. |
| `sourceKind` | Written to `info.json`, for example `local_file`. |
| `reference` | Written to `info.json`: the original file name or the address. |
| `defaultName` | The name when the user gave none. |
| `license` | The license when the user gave none. Empty means `unknown`. When `source.guessed` is true, give none of your own (see "Adding a source"). |
| `originalIsZip` | True when the original is a zip that should go to the Recycle Bin after a successful import, unless the user keeps it. |

### Picking between importers

`pickImporter` asks every importer for a score and takes the highest. The local file importer gives 10 for its endings and for folders that hold a model. An importer that recognises a particular kind of zip can give more, and so win for those zips without any change to the local one. This is the reason scores exist.

### What does not change

No existing code is edited: not the app, not the CLI, not `AssetImport.cpp`. The one exception is a new worker format, which adds its id to the schema as said above. The new importer shows up in `plynth assets importers`, in the file dialog, and in drag and drop. A test finds an importer by id from the test program, so if the registration trick ever breaks, a test fails.

Read `core/src/catalog/importers/LocalFileImporter.cpp` as the worked example. Tests for importers use the fake Blender (`tests/tools/fake_blender`) and need no real Blender: see `tests/core/ImporterTests.cpp`.

## Adding a material importer

A material importer is one way materials come into the library. Today there are four: `texture_set` for a folder, zip or image file of texture maps, `blend_material` for one material of a `.blend` file, `fab_zip` for Fab texture zips and `blenderkit` for BlenderKit material addresses. This section is for developers.

Material importers are a registry of their own, separate from the asset importers, so the two catalogs share nothing. The idea is the same: an importer only decides **what Blender should read**. Matching image files to maps by their names, unpacking zips and reading `material.json` are done by the Blender worker. Core may read a zip's list of files to decide what to take, as the Fab importer does (`core/include/plynth/core/ZipListing.h`), but it never unpacks one. Everything after that is shared: the work folder, the cleanup job, `info.json`, the index, the thumbnail and removing a zip.

### The steps

1. Make one new file in `core/src/catalog/material_importers/`, for example `FabMaterialImporter.cpp`. CMake finds every `.cpp` in that folder and builds it into the OBJECT library `plynth_importers`, so you do not edit `CMakeLists.txt`.
2. In it, write a class that derives from `MaterialImporter` (`core/include/plynth/core/catalog/MaterialImporter.h`) and implements `id()`, `name()`, `description()`, `fileSuffixes()`, `score(source)` and `plan(source, workFolder, out)`. They work as for an asset importer, with a plan that holds these fields:

| Field | Meaning |
|---|---|
| `workerSource` | Which material source of the worker reads it: `texture_set`, `blend` or `fab`. It must match a file in `blender/plynth/materials/sources/`. A new one also adds its id to the `kind` list of `cleanup_material` in `schema/blender-job.schema.json`. |
| `path` | The folder, zip, image file or `.blend` the worker reads. |
| `sourceKind` | Written to `info.json`, for example `texture_set`. |
| `reference` | Written to `info.json`: the original file or folder name, or the address. |
| `defaultName` | The name when neither the user nor `material.json` gave one. |
| `license` | The license when neither gave one. Empty means `unknown`. |
| `originalIsZip` | True when the original is a zip that goes to the Recycle Bin after a successful import, unless the user keeps it. |

3. At the bottom of the file, register it with one line:

```cpp
const MaterialImporterRegistrar registrar(std::make_unique<FabMaterialImporter>());
```

The highest score wins and ties go to the lower id, as for assets. A new importer shows up in `plynth materials importers`, the file dialog of the Import materials window and the inbox. No existing code is edited, except the schema line for a new worker material source. Read `TextureSetImporter.cpp` as the worked example. Tests use the fake Blender, see `tests/core/MaterialImportTests.cpp`.

## Blender worker registries

Blender does the heavy work in the background. Its side is Python, in `blender/plynth/`, and it follows the same idea as the C++ side: **a registry, and one file for each entry**. This section is for developers.

### How a file registers itself

`blender/plynth/registry.py` has a small `Registry` class and thirteen registries: `TASKS`, `IMPORTERS`, `CLEANUP_STEPS`, `CHECKS`, `EXPORTERS`, `BUILD_STEPS`, `OPENING_FILLS`, `MATERIAL_SOURCES`, `BAKERS`, `MATERIAL_CHECKS`, `EXPORT_STEPS`, `QUALITY_CHECKS` and `ADDON_BRIDGES`. Each has `register(id, thing)`, which refuses a duplicate id, plus `get`, `all` and `ids`.

Each package (`tasks/`, `importers/`, `cleanup/`, `checks/`, `exporters/`, `building/`, `fills/`, `exporting/`, `quality/`, `bridges/`) is loaded with `discover(package)`, which imports every module in the folder in name order. A module registers itself on its last line as it is imported. So adding an entry is adding one file. Nothing else is edited: no list, no import. Only the Python standard library and `bpy` are used.

### Tasks

A task is what a job asks for. Files in `plynth/tasks/`: `probe`, `cleanup_asset`, `render_thumbnail`, `build`, `cleanup_material`, `export`, `blenderkit_fetch`. A task is a function `run(ctx)` registered with `TASKS.register("name", run)`. It reads `ctx.params`, fills `ctx.output`, adds warnings with `ctx.warn(code, message)`, and raises `WorkerError(code, message)` to fail with a named error. The C++ side must also know the task name and its parameters and output: see `core/include/plynth/core/BlenderJob.h` and `catalog/AssetJobs.h`.

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

Steps run in the order of their numbers: import_source 10, remove_extras 20, user_transform 30, apply_transforms 40, set_pivot 50, name_objects 60, collect_textures 70, measure 80, run_checks 90, save_blend 100, export_fbx 105, render_thumbnail 110. The gaps let a new step slot in between. Each step sends a progress line before it runs. Steps should be safe to run twice on an already clean asset, because `render_thumbnail` and rescan open assets that were cleaned before. The conventions every cleaned asset follows are in `core/include/plynth/core/catalog/AssetInfo.h`.

### Checks

One file for each check in `plynth/checks/`, run by the `run_checks` step. A check is a function `run(ctx)` registered with `CHECKS.register("polycount", run)`. It reads the job parameters and the measured values, and adds warnings with `ctx.warn`. A check never refuses an import. Look at `checks/polycount.py` for the smallest example.

### The job and result contract

C++ and Python talk through files, described in `schema/blender-job.schema.json` and `schema/blender-result.schema.json`.

- Plynth writes `job.json`: `format`, `version`, `task`, `job_id`, `result_file` and `params`. The folder holding `result_file` is the job's work folder.
- For every task but a store fetch it starts `blender --background --factory-startup -noaudio --python-exit-code 3 --python plynth_worker.py -- --job <file>`, with Blender's user folders (`BLENDER_USER_RESOURCES`, `_CONFIG`, `_SCRIPTS`, `_EXTENSIONS`, `_DATAFILES`) set to a private folder of Plynth's, so these jobs never write your own Blender folders. Of these, only the add-on probe reads your saved settings, as plain data (see [Setting up Plynth](SETUP.md)). A store fetch (`blenderkit_fetch`) is the one exception: it starts in the background without `--factory-startup`, with your own settings and enabled add-ons. The entry point `plynth_worker.py` reads the job, finds the task in the registry and runs it.
- While running, the worker prints progress lines: `PLYNTH:{"progress":{"step":"set_pivot","index":4,"count":11,"message":"Setting the pivot"}}`. Use `jobs.progress(...)`.
- At the end it writes `result.json` through a temporary file and a rename: `ok`, `error`, `blender_version`, `warnings`, `elapsed_ms` and `output`. The worker never lets an exception escape without trying to write a result. An uncaught error exits with code 3 and C++ reports `blender_failed` with the end of the log.

Error codes a task may raise: `bad_job`, `unknown_task`, `unsupported_format`, `import_failed`, `no_geometry`, `several_models`, `unpack_failed`, `save_failed`, `render_failed`, `export_failed`, `preferences_unreadable`, `internal`, and for store fetches `addon_missing`, `not_signed_in`, `download_failed`, `download_timeout`, `wrong_asset_kind`. Warning codes: `high_polycount`, `scale_suspicious`, `missing_texture`, `removed_objects`, `unsupported_object`, `fbx_failed`. The `build` task reports its problems in the result's `output.problems` instead, see "Adding a build step".

### Adding a build step

The `build` task makes the building out of a job that core prepares. It does nothing itself except run **build steps** in order. Each step is one file in `blender/plynth/building/`. Look at `bevel.py` for a small one and `openings.py` for a bigger one.

A step has an `ORDER` number, a sentence shown as progress, and `run(ctx)`:

```python
from . import Step, report
from ..registry import BUILD_STEPS

ORDER = 27


# Warns about a floor whose walls have a very large number of triangles.
def run(ctx):
    for floor_id, walls in ctx.state.get("walls_objects", {}).items():
        triangles = sum(len(face.vertices) - 2 for face in walls.data.polygons)
        if triangles > 200000:
            report(ctx, "heavy_walls", "warning", "The walls of this floor are very heavy.", floor_id, floor_id)


BUILD_STEPS.register("heavy_walls", Step(ORDER, "Checking the walls", run))
```

Drop that file in `building/` and the step runs between `opening_fills` (26) and `floors` (30). Nothing else is edited. The task finds every file, sorts the steps by `ORDER`, prints one progress line per step, times each one, and lists the steps and their times in the result.

What a step can use:

- `ctx.params` is the job: the plan (`plan`), the shapes core worked out (`floors`, each with `wall_solids`, `openings`, `rooms` and `items`, all in world coordinates in metres), the `assets` list and the `settings`. The shapes are described at the top of `core/include/plynth/core/BuildJob.h`. A step never works out a shape again: core already did, with the same code the plan view uses.
- `ctx.state` is a dictionary the steps share. `walls_objects` maps a floor id to its walls object, and `collections` maps a floor id to the collection its objects go into.
- `report(ctx, code, severity, message, floor, obj)` records a problem for one object and carries on. `severity` is `"error"` when the object was left out or is incomplete and `"warning"` when it was built and should be checked. `obj` is the plan id the app marks on the plan. The code uses lower case letters and `_`.
- `count(ctx, name)` adds to one of the numbers in the report: `openings_cut`, `fills`, `rooms`, `items`, `wall_solids`.
- `meshes.py` has the shared mesh code: polygons with holes to faces, prisms, volume, and a manifold check. `surfaces.py` has the surface names and their neutral looks. Name every object `<floor>_<what>`, for example `f1_Walls`, and give it the custom properties `plynth_id` and `plynth_kind`. `meshes.make_object` does both.

An exception that escapes `run` fails the whole build with the error `internal`. Catch what you expect and use `report` instead.

The order numbers. The gaps are for new steps, and the reserved ones belong to later releases.

| Order | Step | What it does |
|---|---|---|
| 5 | `prepare_scene` | An empty scene in metres, one collection for each floor |
| 10 | `walls` | One walls mesh for each floor, every face tagged with its wall, side and room |
| 20 | `openings` | Cuts every opening with a Boolean, lays thresholds through doorways |
| 24 | `bevel` | Rounds the wall edges a little |
| 25 | `triangulate` | Splits wall faces with more than four corners into triangles |
| 26 | `opening_fills` | Fits a door or window into each opening |
| 30 | `floors` | A floor and a ceiling for each room |
| 35 | `slabs` | The slab above each floor that has one, with its edges, undersides, ledge tops and the sides of the stair wells |
| 40 | `stairs` | One solid step at a time for every stair |
| 50 | `roofs` | A mesh for each roof section, by its type: covering, underside, edge, gables, parapets and glazing |
| 60 | `assets` | Places every item as a copy of its asset |
| 70 | `generators` | Runs the generator of each generator item and puts what it makes at the item's place as one mesh. One whose add-on cannot be switched on is left out with a warning |
| 80 | `materials` | The library material of every surface, built from its image maps, with UVs at the material's tile size. A surface without one gets its neutral look |
| 90 | reserved | Trim |
| 100 | reserved | Set dressing |
| 110 | `collision` | A closed convex mesh named `<object>_Collision_<NN>` for each collision shape core worked out, and one convex hull for each furniture item. They go in a `Collision` collection and are left out of `building.fbx`. The plain export leaves them out too |
| 120 | `save_blend` | Saves `building.blend` |
| 130 | `export_fbx` | Makes the item copies real objects, exports `building.fbx` and writes `materials.json` beside it |
| 135 | `preview_parts` | Writes the preview folder for the 3D view: for each floor one file for the floor, one for its slab and one for its roofs, and `parts.json` that lists them. Collision is left out |

A few things to know when you choose a number:

- A step that changes the walls mesh goes after `openings` (20) if it needs the holes, and before `triangulate` (25) if its result should be split too.
- `triangulate` is there because the 3D view loads the FBX, and its loader drops wall faces that have a hole joined to the edge by a thin slit. Splitting those faces into triangles keeps them.
- Steps after `save_blend` (120) are not in `building.blend`. `export_fbx` (130) turns the items into real objects after the blend is saved, so the blend keeps the cheap copies. `preview_parts` (135) runs last and only writes files for the 3D view.

### How the openings are cut

The `openings` step is the one most likely to break, so it checks itself. Every opening is cut with Blender's Boolean modifier, one opening at a time. It tries the fast Manifold solver first and the slower Exact solver if that fails. A cut counts only if two things are true afterwards: the walls are still one closed solid, and the volume that was removed is between half and double the volume the opening's outline should remove. Otherwise the walls are put back as they were and the opening gets the problem `cut_failed`. Without the second check, a bad Exact cut at a T junction once removed the walls of a whole apartment and still looked like a success.

### Adding an opening fill

A **fill** is what stands in an opening once it is cut: the door leaf, the window glass, the frames. `door.py` and `window.py` in `blender/plynth/fills/` are the built-in ones. A fill is one file with a `build(ctx, floor, opening)` function and a list of opening types it serves:

```python
import bmesh

from . import FillEntry
from . import common
from .. import meshes
from ..registry import OPENING_FILLS

SURFACES = ["Glass"]


# Closes a plain opening with a thin glass pane inside the inner outline.
def build(ctx, floor, opening):
    place = common.placer(floor, opening)
    bm = bmesh.new()
    meshes.add_prism_uv(bm, opening["inner_outline"], [], -0.003, 0.003, place, 0)
    return [common.finish(ctx, floor, opening, bm, "Pane", "window", SURFACES)]


OPENING_FILLS.register("pane", FillEntry(("plain",), build))
```

Put it in `blender/plynth/fills/`, and the `opening_fills` step (26) finds it. Nothing else is edited.

- `opening` holds the opening's exact shape in the opening's own flat frame: `outline` and `inner_outline` as lists of `(u, v)` points, where `u` runs along the wall from the middle of the opening and `v` runs up from the sill, in metres. `thickness` is the wall's thickness. `hinge`, `swing`, `sill`, `type` and `shape` come from the plan. `inner_outline` is the outline pulled in by the frame width (5 cm). It is empty when the opening is too small for a frame, and the step then reports `fill_failed` for you.
- `common.placer(floor, opening)` gives `place(u, v, w)`, which turns a point of that frame into a world point (`w` is across the wall). `meshes.add_prism_uv` extrudes a polygon, with holes, between two `w` values. `common.frame_band` makes the band between the outline and the inner outline, and `common.add_box` a box. `common.finish` makes the object, names it `<floor>_<opening>_<Suffix>`, and gives it its surface slots.
- A fill follows the exact outline, so arches and custom shapes get matching doors and windows. On a curved wall a fill is flat, in the plane that touches the wall at the middle of the opening.
- The surface names in `SURFACES` must exist in `surfaces.py`. Add a new surface and its neutral look there if you need one.
- If two fills serve the same type, the one whose file name sorts first is used. An opening whose type no fill serves is left empty without a problem, which is how `plain` openings work by default.
- A fill that raises an error gives the problem `fill_failed` for that opening, and the build goes on.
- Fills are skipped when `fill_openings` is false in the job's settings.

Fitting catalog door and window assets into the opening's frame is planned for a later release. The built-in fills are the plain default.

### Adding a material source, a baker or a material check

The `cleanup_material` task (`blender/plynth/tasks/cleanup_material.py`) makes the image maps of a material in four steps: "Reading the source", "Making the maps", "Checking the maps" and "Rendering the thumbnail". The first three each use a registry, with one file per entry in `blender/plynth/materials/`, found by the task itself. Nothing else is edited.

**A material source** reads where a material comes from and says, for each map, what feeds it. One file in `materials/sources/`, with `load(ctx, source)` that returns a `Loaded`: a `ChannelPlan` for each channel (`channel`, `kind`, `file`, `value`, `node`), the picked material's name, what a `material.json` said, and the tile size. The kind says what the baker should do: `file`, `directx`, `gloss`, `packed`, `alpha`, `value`, `color`, or `node` for something only a render can make.

```python
from .. import SourceEntry
from ...registry import MATERIAL_SOURCES


# Reads a material from my own format.
def load(ctx, source):
    ...


MATERIAL_SOURCES.register("my_format", SourceEntry(("my_format",), load))
```

The kinds in `SourceEntry` are the `source.kind` values of the job that this source reads. Core must also name the same kind in the plan of a material importer (see "Adding a material importer").

**A baker** makes the map of one channel, in order. The task asks each baker, lowest `ORDER` first, whether it `can_bake` the channel, and the first yes makes it. A channel that no baker can make is the error `bake_failed`. Two bakers exist. `direct` (order 10) copies an image, flips a DirectX normal, inverts glossiness, splits a packed image or writes a small constant image for a plain number. `eevee_render` (order 20) draws any other channel with Eevee: a flat tile one tile size across under a camera that looks straight down, the channel's node output routed to an emission shader, one sample, and a view transform that changes no values. Blender's own bake operator is never used, because it needs Cycles.

```python
from .. import BakerEntry
from ...registry import BAKERS

ORDER = 15


# Says whether this baker can make the channel.
def can_bake(ctx, plan):
    return plan.kind == "my_kind"


# Writes the map into the textures folder and returns its entry, or None when there is no map.
def bake(ctx, plan, textures):
    ...


BAKERS.register("my_baker", BakerEntry(ORDER, can_bake, bake))
```

**A material check** runs after the maps are made, and warns. One file in `materials/checks/`, with `run(ctx, found)`. `found.maps` are the finished maps, `found.paths` their absolute paths and `found.tile_size` the tile size. A check never fails an import. The three that exist are `map_sizes` (10), `seams` (20) and `resolution` (30).

```python
from .. import CheckEntry
from ...registry import MATERIAL_CHECKS

ORDER = 40


# Warns when the material has no roughness image.
def run(ctx, found):
    if found.maps["roughness"]["from"] != "file":
        ctx.warn("no_roughness_image", "The roughness is a single value.")


MATERIAL_CHECKS.register("roughness_file", CheckEntry(ORDER, run))
```

Worker codes of this task: errors `import_failed`, `no_base_color`, `several_materials`, `bake_failed`, `unpack_failed`, `save_failed`; warnings `ambiguous_map`, `unused_image`, `map_size_mismatch`, `visible_seams`, `low_resolution` and `picked_material`. The files and conventions every cleaned material follows are described at the top of `core/include/plynth/core/catalog/MaterialInfo.h`. Image names are matched in `materials/names.py`, a plain table you can extend. The tests are `blender/tests/test_materials.py` and `test_materials_blend.py`.

### Adding an exporter

An exporter decides which FBX files an export writes and which built objects go into each. It lives in core, because core knows the project. It is one file, and nothing else is edited.

1. Make one new file in `core/src/exporters/`, for example `GroupedExporter.cpp`. CMake finds every `.cpp` in that folder and builds it into the OBJECT library `plynth_exporters`, so you do not edit `CMakeLists.txt`.
2. In it, write a class that derives from `Exporter` (`core/include/plynth/core/Exporter.h`) and implements `id()`, `name()`, `description()` and `groups(project, out)`. The id is the project's export mode name, so `single` serves the single mode. `groups` fills a list of `ExportGroup`, one for each file: its `id`, its `file` name, and which kinds of objects or plan ids it takes. An empty list of kinds or ids does not narrow the group. A group with no members, kinds or ids takes everything only when no group of the export is marked `rest`, as in a single export. When the project cannot be exported this way, return `InvalidValue` with a plain sentence.
3. At the bottom of the file, register it:

```cpp
const ExporterRegistrar registrar(std::make_unique<GroupedExporter>());
```

`SingleExporter.cpp` is the worked example, about twenty lines. `GroupedExporter.cpp` is the exporter of the grouped mode. It does not list the groups itself. It asks every group rule (next section). `exportFileStem(name)` turns a project name into a safe file name. The export runs the exporter's groups through the same Blender task and the same checks, and every group is written with the same FBX profile.

On the Blender side, the file format is a registry too. One file in `blender/plynth/exporters/` registers an `ExporterEntry(extension, export)`. `export(objects, filepath, path_mode)` writes the given objects, or the whole scene when `objects` is `None`. `exporters/__init__.py` has `write_file`, which writes under a temporary name and renames the result, so a failed export leaves nothing behind. `fbx.py` is the one profile Plynth uses for `asset.fbx`, `building.fbx` and the export.

### Adding a group rule

A grouped export writes one file for each group. The groups are made by group rules, and each rule is one file in core. Core knows the plan, so the rules live there and not in Blender. A new rule needs no other edit.

1. Make one new file in `core/src/exporters/`, named `GroupRule<Name>.cpp`. The same glob that finds exporters builds it, so `CMakeLists.txt` stays as it is.
2. In it, write a class that derives from `ExportGroupRule` (`core/include/plynth/core/ExportGroups.h`) and implements three functions:
   - `id()`: a stable lower case name, such as `doors`.
   - `order()`: where its groups go in the list. Lower numbers come first. The built-in rules are `structure` (10), `surfaces` (20) and `furniture` (30).
   - `addGroups(project, out)`: add `ExportGroup` entries to `out`, each with an `id`, a `name` and its `members`. Do not fill in `file`. Core names the files afterwards from the project name and the group name.
3. At the bottom, register it.

Here is a rule that puts every door in a group of its own. Its order is 5, so it runs before the Walls group:

```cpp
#include "plynth/core/ExportGroups.h"

namespace plynth {

namespace {

// Puts every door in a group of its own.
class DoorsRule final : public ExportGroupRule {
public:
    std::string_view id() const override { return "doors"; }
    int order() const override { return 5; }

    // One member for each door.
    void addGroups(const Project& project, std::vector<ExportGroup>& out) const override
    {
        ExportGroup group;
        group.id = "doors";
        group.name = "Doors";
        for (const Floor& floor : project.plan.floors) {
            for (const Opening& opening : floor.openings) {
                if (opening.type == OpeningType::Door) group.members.push_back({std::string(kKindDoor), opening.id});
            }
        }
        if (!group.members.empty()) out.push_back(std::move(group));
    }
};

const ExportGroupRuleRegistrar registrar(std::make_unique<DoorsRule>());

}

}
```

The example was written for this page and has not been built.

What to know when you write one:

- **A member is a kind and a plan id.** Every built object carries a `plynth_kind` and a `plynth_id`, for example `door` and `o3`, or `item` and `i1`. A member with an empty id takes every object of that kind. `ExportGroups.h` lists the kinds as `kKind...` constants, including kinds that later releases make (`generator`, `light`). A member for a kind nothing has is harmless. An item has one member, never two: `item` for an asset, `generator` for an item a generator makes, both with the item's id.
- **An object goes to one group.** The Blender side picks the group by this order: a group with the exact member, then a group with the kind as an empty id member, then the first group with no members whose `kinds` and `ids` filters take it, then the group marked `rest`. When a group is marked `rest`, a group with no members, kinds or ids is skipped in the third step, so an empty group takes nothing. If a rule gives a member that an earlier rule already has, core drops it from the later group. A group that loses all its members to earlier rules is dropped, and only one group keeps `rest`.
- **List only what the build makes.** Give a member only for objects the plan says exist, such as a door for each door opening. Then the review shows what an export will really write.
- **Edits come on top.** Whatever your rule makes, the user can rename, merge and move its groups, and the edits are applied to your groups by id. Keep ids stable from one build to the next, so a user's edit keeps finding its group.
- **Collision follows its object.** Collision meshes (`<object>_Collision_<NN>`) only matter to the Unreal Engine link, which is coming. They go into the file of the object they belong to, so a rule never mentions collision.

`GroupRuleStructure.cpp` and `GroupRuleFurniture.cpp` are the readable examples. The tests are `tests/core/ExportGroupsTests.cpp`. On the Blender side, the one helper that sorts objects into groups is `blender/plynth/exporting/groups.py` (`assign`). The writer and the `groups` check both call it, so they always agree.

### Adding a quality check

A quality check is one file in `blender/plynth/quality/`. It has an `ORDER`, a phase, a name shown in the Export window, `run(ctx)`, and the export profiles it runs for. The plain export uses `PLAIN`. Checks about engine rules, such as collision and the modular kit, name only `UNREAL`, the profile of the coming Unreal Engine link. The export task finds every file, so nothing else is edited.

```python
from . import PLAIN, UNREAL, QualityCheck, finding, result
from ..registry import QUALITY_CHECKS

ORDER = 95


# Fails for a mesh with more than 100 materials.
def run(ctx):
    findings = []
    for obj in ctx.state["meshes"]:
        if len(obj.data.materials) > 100:
            findings.append(finding("%s has too many materials." % obj.name, obj.name, obj.get("plynth_id")))
    if findings:
        return result("fail", "Some meshes have too many materials.", findings)
    return result("pass", "No mesh has too many materials.")


QUALITY_CHECKS.register("material_count", QualityCheck(ORDER, "scene", "Material count", run, (PLAIN, UNREAL)))
```

- The phase is `"scene"` for a check that looks at the objects before the FBX is written, or `"file"` for one that reads the written files back.
- `result(status, message, findings)` gives the answer. The status is `pass`, `warning`, `fail` or `skipped`. Only `fail` blocks an export, and only when it is not forced. At most 50 findings are kept.
- `finding(message, object, plan_id)` makes one finding. The plan id is what the app selects when you click the finding, so give one when the object comes from the plan.
- `ctx.state["meshes"]` holds the building and furniture objects, `ctx.state["collision"]` the collision objects (empty in a plain export). `ctx.state["profile"]` is the export profile, `plain` or `unreal`. `ctx.params` is the export job, with the `limits` and the `expected_bounds` that core worked out.
- A check never stops the export. Do not raise an error for a problem you can report.

Look at `naming.py` for a bigger one. Two checks came with grouped export. `groups.py` (105, scene) fails for an object that no group takes and warns about a group with no object. It uses `assign` from `exporting/groups.py`, the same helper the writer uses, so the check and the files cannot disagree. `kit_file.py` (120, file) reads the kit file back and fails when a piece is missing, moved or has no collision. It says "skipped" when the job has no kit, so a check can stay quiet when its part of the export was not asked for. A new check shows up in the Export window and in the report at once. Core only needs a change if the check needs a number it does not pass yet.

### Adding an export step

The `export` task, like the build, only runs steps in order. Each step is one file in `blender/plynth/exporting/`. The steps are `open_blend` (10), `prepare` (20), `name_objects` (30), `kit_pieces` (35), `textures` (40), `checks` (50), `write_fbx` (60), `write_kit` (65) and `file_checks` (70).

```python
from . import Step
from ..registry import EXPORT_STEPS

ORDER = 45


# Does one thing between collecting textures and running the checks.
def run(ctx):
    ctx.state["my_note"] = len(ctx.state["meshes"])


EXPORT_STEPS.register("my_step", Step(ORDER, "Doing my step", run))
```

Drop the file in `exporting/` and the step runs between `textures` (40) and `checks` (50). `kit_pieces.py` and `write_kit.py` are real examples. `kit_pieces` (35) makes the modular kit's meshes from the outlines core sends in the job. `write_kit` (65) writes them to their own FBX after the group files. Both do nothing when the job has no `kit`, which is how an optional step stays out of the way. The sentence is shown as progress. `ctx.state` is shared between the steps. A step may fail the whole job by raising `WorkerError`, which is meant for real errors such as a missing file. A quality problem is a check, not a step. The export never saves `building.blend`.

### Testing

The Python tests run inside Blender: `blender -b --factory-startup --python blender/tests/run_tests.py -- [pattern]`. `ctest -L blender` runs them when Blender is found and gives Blender a scratch folder as its user folders. `--factory-startup` alone does not keep Blender away from your own folders, so when you run the command by hand, first point the five user variables at an empty scratch folder (PowerShell):

```
$s = "$env:TEMP\plynth-blender-scratch"
New-Item -ItemType Directory -Force $s | Out-Null
$env:BLENDER_USER_RESOURCES = $s
$env:BLENDER_USER_CONFIG = $s
$env:BLENDER_USER_SCRIPTS = $s
$env:BLENDER_USER_EXTENSIONS = $s
$env:BLENDER_USER_DATAFILES = $s
```

Set these in the same window, and use the same window to run the command. The run starts a second Blender with user folders of its own and a prepared extensions folder, and an add-on is only ever switched on in such a folder. The tests that need add-ons skip unless `PLYNTH_TEST_ADDONS` names a folder with copies of them, laid out like an extensions folder (`<repository>\<add-on>\blender_manifest.toml`). The tests never take add-ons from your own Blender folders: they refuse a folder inside them. Every registry has a test that refuses a duplicate id. The sample chair in `samples/models/chair` is the model most tests use.

## Adding a source

A source is a place assets and materials come from, such as BlenderKit or Fab. It is called an **integration**. This section is for developers. It adds one card to the Sources window, its options and its commands, and it needs no edit of existing code. The one exception: a new worker format or material source adds its id to a list in `schema/blender-job.schema.json` (see the two tables above). It is built from the pieces above: an importer, a material importer, and, when the source is not a file on disk, a fetch task and a worker format or material source in Python.

The rule behind the design: Plynth never talks to a store itself. A source either takes files you downloaded, or asks a store's own Blender add-on, which you installed and signed in to, to download. It never calls a private web address, never shows, logs or copies a key and never bundles what it brings.

### The core side

1. **The importers.** Write an asset importer, a material importer or both, as in the two sections above. Each is one file. An importer that takes a web address returns no endings from `fileSuffixes()` and scores 0 for anything that is not its address (`BlenderKitImporter.cpp` scores 30 for an address it accepts). An importer for zips can score above the local one for a zip it recognises, as `FabZipImporter.cpp` does with 20.
2. **The integration.** In the same file, write a class that derives from `Integration` (`core/include/plynth/core/catalog/Integrations.h`). It gives an id, a name, a description, its place in the tab (`order()`), its importer ids, whether it takes addresses, the setup check item it depends on (empty for none), its official site, and an optional `note()`, one plain sentence shown with the card, as BlenderKit shows its reminder about your own Blender settings. It also gives its options: `defaultOptions()` lists each saved option with its default, and `checkOptions()` refuses an unknown key or a bad value. Three more parts make the source work everywhere without code of its own:

   - `optionFields()`: one field for each key of `defaultOptions()`, in the same order, with a label, a one sentence help and a kind: a choice from a list, a whole number from a minimum to a maximum, or a license. The tab draws the card's options from it.
   - `storeSourceKinds()`: the source kinds of its items when they come from a store, such as `blenderkit` and `fab_zip`. `isStoreSource` reads them from the registry, so such items are never copied into packs. Leave it empty when the items are the user's own files.
   - `checkAddress(text)`: for a source that takes addresses, the sentence the card shows beside a pasted text it cannot use. BlenderKit returns what its address parser says.

   At the bottom, register it:

```cpp
const IntegrationRegistrar integrationRegistrar(std::make_unique<MyIntegration>());
```

3. **The options.** A saved option applies to every import from the integration. It is kept in `integrations.json` in the library, and only what the user changed is saved. An option for one import only (such as `model` for Fab) is refused when saving: `checkOptions()` gets `OptionUse::Saved` or `OptionUse::Import` to tell them apart. At import time `optionsForImporter` puts the request's options over the saved ones over the defaults and hands the result to the importer's `plan` as `ImportSource::options`.
4. **The CLI and the tab.** `plynth library integrations`, `library integration show|set|reset` and the Sources window list every registered integration by themselves. The tab draws the card from what the class returns and the options from `optionFields()`, so no app code is needed. The card's import button for files asks `integrationImporter` for the best scoring of the source's own importers and names it in the request, so the item gets this source's kind and options even when another importer would score higher. The CLI treats a text as an address when a registered importer takes it.
5. **The license.** When the request names no importer and gives no `license` option, Plynth only guessed the source, and `ImportSource::guessed` is true. The plan then gives no license of its own, so the item gets `unknown`. Fab does this for a zip that only looks like a Fab download. When the user picked the source, use the saved license option.
6. **A choice for the user.** When a plan refuses because the user must choose, for example between the models of a pack, put the candidates in the plan's `details` as a `models` list. The app's **Pick the model...** button is offered from that list, not from the sentence.

### The fetch, when the source is not a file

An importer for a source that is not a file gives its plan no `file`, and puts a **fetch** in the plan instead: a worker task and its parameters (`ImportFetch`, `core/include/plynth/core/catalog/ImportFetch.h`). The import runs the fetch first, in its own Blender run, and then the usual cleanup on what it fetched. It is a separate run because a fetch may need the user's own add-ons and sign in, which the clean start of every other job leaves out, and the cleanup must always run clean so that no add-on can change its result. A BlenderKit fetch always runs in the background, with no window.

A fetch task writes into the `output_folder` it is given and answers with the same output for every store (the `fetchOutput` part of `schema/blender-result.schema.json`): the file, its format, whether it is a `model` or a `material`, and the name, author, license and address when the store gives them. `core/include/plynth/core/catalog/StoreFetch.h` explains how the answer is checked and put into the plan. Its failures are `blender_failed` with the task's own error code and a plain sentence.

### The Python side

- **A fetch task.** One file in `blender/plynth/tasks/`, a function `run(ctx)` registered with `TASKS.register`, as under "Tasks". `blenderkit_fetch.py` is the worked example. Everything that touches the store's add-on sits in one module (`blender/plynth/stores/blenderkit.py`), so an add-on update changes one file, and every failure names the add-on's version.
- **A worker format or material source**, when the fetched files need a reader of their own. A model format is one file in `blender/plynth/importers/` registered with `IMPORTERS.register`. A material source is one file in `blender/plynth/materials/sources/` registered with `MATERIAL_SOURCES.register`. Either one also adds its id to its list in `schema/blender-job.schema.json`: `format` of `cleanup_asset` or `kind` of `cleanup_material`. `importers/fab.py` and `materials/sources/fab.py` are the examples: both read a small source file the importer wrote, unpack only the entries it lists, and match textures to materials by file name.

### Tests

Test the importers and options with the fake Blender (`tests/tools/fake_blender`), which can also stand in for a fetch task. Test the address parsing and the options as plain functions. Test the Python side in a real Blender with a small fake add-on and scratch user folders (`blender/tests/tools/make_fake_blenderkit_config.py` sets every variable), never with the real store or your own settings. Keep downloaded items and zips out of the repository.

