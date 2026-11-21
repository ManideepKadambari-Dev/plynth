# Writing generators

This page explains how to add to Plynth without changing the program. Today it covers one thing, opening shapes. Generators, importers and roof types are added to this page in later releases.

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

**When it is read.** Plynth reads the folder when it starts, once the Library folder is set up. The Library arrives with the asset catalog in a later release, so for now Plynth does not look in any folder and the five built-in shapes are the ones you can use. The file format below is final. A file you write now will work unchanged when the Library arrives. Nothing in Plynth has to be edited to add a shape: it shows up in the app's shape list and in `plynth opening shapes`, after the built-in ones, with `built_in` false.

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

### Other things you will be able to add

Later releases add the same kind of page for generators (small Blender scripts), importers, exporters and roof types. Each one will be added to this page, with its file format and its rules.
