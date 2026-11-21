# Command line guide

Plynth can be driven from a terminal, a script or a coding assistant. Every command prints one JSON answer, so a program can read the result without parsing text meant for people.

This page covers the commands that exist today: version, help, creating and reading a project, setting the scale, grid and default sizes, floors, points, walls (straight and curved), rounded corners, doors, windows and other openings, rooms, finding walls and openings in the plan image with `detect`, and the edit session that keeps the app and the command line from overwriting each other. More commands arrive in later releases.

The plan and project files are now at format version 2. Older projects open and upgrade by themselves. See "File format version 2" below.

## Running a command

Two programs sit side by side in the Plynth folder.

| Program | What it is for |
|---|---|
| `plynth.com` | The one to use from a terminal. It waits for the command to finish, so the answer appears before the prompt comes back and the exit code is set. |
| `plynth.exe` | The app. It also runs commands, but a terminal does not wait for it, so the prompt can come back before the answer is printed. Use it from scripts that capture its output. |

Typing `plynth` in a terminal finds `plynth.com` first. The terminal inside Plynth is set up so `plynth` works there without any path.

The first word decides what happens:

- A command word (`version`, `help`, `project`, `scale`, `point`, `wall`, `opening`, `floor`, `room`, `detect`, `session`) runs the command and prints the answer. No window opens.
- A project folder or an app option (`--image`, `--screenshot`, `--size`, `--terminal`), or no argument at all, opens the window. `plynth.com` starts the window and returns at once.
- Anything else prints an `unknown_command` answer and exits with code 2. For example `plynth bogus`.

To open a folder that is literally named after a command word, such as `project`, `room`, `opening`, `floor` or `detect`, write `.\project`, `.\room`, `.\opening`, `.\floor` or `.\detect`. Without the `.\` Plynth runs the command instead. `opening` and `floor` are new command words, so a folder with one of those names that worked before now needs the `.\` too. The same goes for `--project`: write `--project .\opening`.

### Which project a command works on

Like git, a command works on the project in the current folder. Add `--project <folder>` to use another one. It works on every command except `version` and `help`.

```
cd D:\Plans\MyApartment
plynth project info

plynth project info --project D:\Plans\MyApartment
```

## The answer

One JSON object on one line, then a line break, on standard output. Nothing is written to standard error. Add `--pretty` anywhere to indent the answer.

Success:

```json
{"ok":true,"command":"point add","result":{"id":"p3","floor":"f1","x":1.5,"y":2.0,"corner_radius":0.0,"corner_radius_used":0.0,"walls":[]}}
```

Failure:

```json
{"ok":false,"command":"wall add","error":{"code":"not_found","message":"There is no point p9."}}
```

- `ok` is true or false.
- `command` is the command that ran, such as `point add`. It is empty when the command could not be recognised.
- `result` holds the values on success. Every command's result is listed below.
- `error.code` is a short name a program can test. `error.message` is one plain sentence for a person.
- `error.where` is added only when a project file is damaged. It is a JSON pointer to the bad value, such as `/floors/0/walls/2/to`.

Paths in answers are absolute and use forward slashes. Lengths are in metres. The shape is described by `schema/cli-answer.schema.json`.

Help text from the command parser is never printed. `--help` or `-h` on any command gives the same answer as `help`.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | The command worked. |
| 1 | The command ran and refused, or failed. The answer has the error. |
| 2 | The command was not understood: unknown command, unknown option, missing option or a bad value for an option. The answer has the error. |

The app itself, when you give it `--screenshot`, uses its own codes. 0 means the picture was saved. 1 means the picture could not be saved. 2 means bad options. 3 means the picture was saved but the project or the image could not be loaded. 4 means the terminal shell could not start.

## Error codes

| Code | When |
|---|---|
| `bad_arguments` | A required option is missing, a value is not a number, or an option is unknown. Exit 2. |
| `unknown_command` | The command word is not one of the commands. Exit 2. |
| `invalid_value` | A number or name is out of range, for example a scale of 0. Also used when an opening does not fit its wall, a wall is bent too far, or a height would leave an opening sticking out. The message says what is wrong. |
| `not_found` | A floor, point, wall, room, opening or file does not exist, or there is no closed area at the position given to `room add`, or there is no wall at the position given to `opening add`. |
| `already_exists` | The thing to create is already there, for example `project new` in a folder that holds a project, a wall between two points that already have one, or a room where one already is. |
| `in_use` | Removing something would leave another item pointing at nothing, for example a wall that a room uses. Add `--cascade` to remove what depends on it as well (see `point remove`). |
| `not_a_project` | The folder has no `project.json`. |
| `cannot_read` | A file or folder could not be opened. |
| `cannot_write` | A file or folder could not be written. |
| `bad_file` | A project file is not valid or breaks the format rules. `error.where` says where. |
| `unsupported_version` | The file was written by a newer version of Plynth than this one. Older files are upgraded, not refused. |
| `image_error` | The plan image was refused: missing, empty, damaged, too large or not PNG, JPEG or BMP. `detect` also uses it when the image cannot be decoded or processed. |
| `locked` | The project is open in the app and no edit session is active, or the session ended. See "Working while the app is open". |
| `unsaved_changes` | The app has unsaved changes, so an edit session cannot begin. |
| `internal_error` | Something unexpected went wrong inside Plynth. Exit 1. The message says what. |

## Commands

### version

Shows the program version and the file format versions it reads and writes.

```
plynth version
```

```json
{"ok":true,"command":"version","result":{"version":"0.0.1","plan_format":2,"project_format":2}}
```

`plan_format` and `project_format` are the versions this program writes. It reads these and older ones.

### help

Lists every command with its options. The list is built from the real commands, so it is always current.

```
plynth help
```

```json
{"ok":true,"command":"help","result":{"commands":[{"command":"project new","description":"Create a project folder, optionally with a plan image.","options":[{"name":"--image","value":"file","required":false,"description":"Plan image to use for the first floor (PNG, JPEG or BMP)."}]}]}}
```

The example is shortened. The real answer lists every command.

Each option has `name`, `value` (what to type, such as `metres` or `id`), `positional`, `required` and `description`. `positional` is true for a value you write without a name, such as the id in `point move p3 --x 2 --y 1`. Options with `positional` false are written with their `--name`.

### project new

Creates a project folder. The folder is made when it does not exist. If the folder already holds a project, nothing is changed and the answer is `already_exists`.

| Option | Meaning |
|---|---|
| `--project <folder>` | Where to create it. Default: the current folder. |
| `--image <file>` | Plan image for the first floor (PNG, JPEG or BMP). It is copied into the project's `plans` folder. |
| `--export single\|grouped` | Export mode. Default: `single`. |
| `--grid <metres>` | Grid size. Default: 0.5. |
| `--name <text>` | Project name. Default: the folder name. |

```
plynth project new --project D:\Plans\MyApartment --image plan.png
```

```json
{"ok":true,"command":"project new","result":{"folder":"D:/Plans/MyApartment","name":"MyApartment","export_mode":"single","grid_size":0.5,"defaults":{"ceiling_height":3.0,"door_width":0.9,"door_height":2.1,"window_width":1.2,"window_height":1.2,"window_sill":0.9},"has_autosave":false,"plan":{"version":2,"pixels_per_metre":null,"floors":[{"id":"f1","name":"Floor 1","image":"plans/plan.png","image_found":true,"ceiling_height":3.0,"points":0,"walls":0,"rooms":0,"openings":0}]}}}
```

Without `--image` the floor has no image and `image` is `null`. `defaults` holds the default sizes, see `project set`.

### project info

Shows the settings and contents of a project. It changes nothing. The result is the same as for `project new`.

- `defaults` are the default sizes of new openings and of the ceiling of new floors, in metres.
- `plan.version` is the format version of the plan file as it will be saved, always 2 here.
- `pixels_per_metre` is `null` until the scale is set.
- `image_found` says whether the image file the plan points at exists.
- `has_autosave` says whether the app left a recovery copy in the project. The command line never reads, writes or removes it.

```
plynth project info
```

### project set

Changes the grid size or the default sizes. Give at least one option. Options you leave out keep their value. Several options in one command run as one step: if one is refused, none is applied.

| Option | Meaning |
|---|---|
| `--grid <metres>` | Distance between grid lines. Above zero. |
| `--door-width <metres>` | Width of a new door and of a new plain opening. Default: 0.9. |
| `--door-height <metres>` | Height of a new door and of a new plain opening. Default: 2.1. |
| `--window-width <metres>` | Width of a new window. Default: 1.2. |
| `--window-height <metres>` | Height of a new window. Default: 1.2. |
| `--window-sill <metres>` | How far above the floor a new window starts. Default: 0.9. |
| `--new-floor-height <metres>` | Ceiling height of floors made from now on. Default: 3.0. It does not change floors that exist. Use `floor set` for those. |

```
plynth project set --door-height 2.0 --window-sill 0.8
```

```json
{"ok":true,"command":"project set","result":{"folder":"D:/Plans/MyApartment","name":"MyApartment","export_mode":"single","grid_size":0.5,"defaults":{"ceiling_height":3.0,"door_width":0.9,"door_height":2.0,"window_width":1.2,"window_height":1.2,"window_sill":0.8},"has_autosave":false,"plan":{"version":2,"pixels_per_metre":100.0,"floors":[{"id":"f1","name":"Floor 1","image":"plans/apartment-01.png","image_found":true,"ceiling_height":2.8,"points":4,"walls":4,"rooms":1,"openings":3}]}}}
```

The answer is the same as for `project info`. These sizes are what `opening add` uses when you leave out `--width`, `--height` or `--sill`. They are saved in `project.json` and are the same ones the app's Opening tool page shows.

The sizes must work together. A door can be at most as high as the ceiling, and a window's sill plus its height must be at most the ceiling height. A set that breaks this is refused with `invalid_value` and nothing is changed:

```
plynth project set --window-sill 2.5
```

```json
{"ok":false,"command":"project set","error":{"code":"invalid_value","message":"The window sill 2.50 m plus the window height 1.20 m is more than the ceiling height 3.00 m."}}
```

A grid of 0 or less is refused with `invalid_value`. The grid starts at the top left corner of the image. The app uses it for snapping, and so does `wall draw --grid`.

### scale set

Sets how many image pixels make one metre.

| Option | Meaning |
|---|---|
| `--pixels-per-metre <number>` | Required. Above zero. |
| `--keep image\|metres` | What happens to points and walls that already exist when the scale changes. Default: `image`. |

```
plynth scale set --pixels-per-metre 100
```

```json
{"ok":true,"command":"scale set","result":{"pixels_per_metre":100.0,"moved_marks":false}}
```

**Keeping marks.** Points and walls are stored in metres. When the scale changes, one of two things can be true.

- `--keep image` (the default) keeps every mark on the same spot of the plan image. Point positions are multiplied by old scale over new scale, and so are curve and opening positions along a wall. Thickness, heights, widths and corner radius are real sizes, so they do not change. Use it when you fix a scale you got wrong.
- `--keep metres` leaves every number as it is. The marks move on the image. Use it when the metre values are right and only the scale of the picture was wrong.

Setting the first scale, or clearing it, moves nothing in either mode. `moved_marks` is true when marks were moved. Undo in the app brings everything back exactly.

### scale from-points

Works out the scale from two points on the plan image and a distance you know, then sets it. Positions are image pixels, with the origin at the top left corner.

| Option | Meaning |
|---|---|
| `--x1 <pixels>` `--y1 <pixels>` | Required. First point. |
| `--x2 <pixels>` `--y2 <pixels>` | Required. Second point. |
| `--distance <metres>` | Required. The real distance between the two points. Above zero. |
| `--keep image\|metres` | Same as for `scale set`. Default: `image`. |

```
plynth scale from-points --x1 100 --y1 100 --x2 600 --y2 100 --distance 5
```

```json
{"ok":true,"command":"scale from-points","result":{"pixels_per_metre":100.0,"pixel_distance":500.0,"distance":5.0,"moved_marks":false}}
```

Points less than a pixel apart are refused with `invalid_value`.

### point add

Adds a wall corner point to a floor. Coordinates are in metres. The origin is the top left corner of the plan image, x grows to the right and y grows downward.

| Option | Meaning |
|---|---|
| `--x <metres>` | Required. Position to the right. |
| `--y <metres>` | Required. Position down. |
| `--floor <id>` | Floor id. Default: the first floor. |
| `--corner-radius <metres>` | Corner rounding. Default: 0. See "Rounded corners" under `point set`. |

```
plynth point add --x 1.5 --y 2
```

```json
{"ok":true,"command":"point add","result":{"id":"p1","floor":"f1","x":1.5,"y":2.0,"corner_radius":0.0,"corner_radius_used":0.0,"walls":[]}}
```

Ids are a letter plus a number: `p1`, `p2` and so on. An id never changes once it is given.

Every answer that shows a point has the same shape (the "point entry"). `walls` lists the ids of the walls that end at the point. It is empty for a point with no wall. `corner_radius` is the radius you set. `corner_radius_used` is the radius that really applies: 0 when the corner is not rounded, and smaller than `corner_radius` when the walls are too short for it.

### point move

Moves a point. The id is written without a name.

| Option | Meaning |
|---|---|
| `<id>` | Required. The point to move. |
| `--x <metres>` `--y <metres>` | Required. The new position. |

```
plynth point move p6 --x 9 --y 8
```

```json
{"ok":true,"command":"point move","result":{"id":"p6","floor":"f1","x":9.0,"y":8.0,"corner_radius":0.0,"corner_radius_used":0.0,"walls":["w3","w6","w7"]}}
```

The walls that end at the point follow it. An unknown id gives `not_found` and changes nothing. A move that would bend a curved wall past a half circle, or make its radius smaller than half its thickness, is refused with `invalid_value`.

Moving a point can leave an opening hanging past the end of a shortened wall. The move is still allowed. `opening list` then reports the opening with `"fits":false`, and its `problems` count goes up. See "When an opening stops fitting".

### point set

Changes a point. Today that is its corner radius.

| Option | Meaning |
|---|---|
| `<id>` | Required. The point. |
| `--corner-radius <metres>` | Required. Zero or more. 0 removes the rounding. |

```
plynth point set p3 --corner-radius 1
```

```json
{"ok":true,"command":"point set","result":{"id":"p3","floor":"f1","x":5.0,"y":7.0,"corner_radius":1.0,"corner_radius_used":1.0,"walls":["w2","w3"]}}
```

**Rounded corners.** A corner is rounded with an arc that touches the centre lines of both walls. The arc starts a little way back from the point on each wall, so the walls get shorter at that end. A corner is rounded only when:

- exactly two walls end at the point, and
- both are straight, and
- they meet at an angle between 1 and 179 degrees.

At a point with one wall, three or more walls, or a curved wall, the radius is kept but has no effect: `corner_radius_used` is 0. If the walls are too short, the radius used is made smaller so the arc takes at most half of each wall. A negative radius is refused with `invalid_value`.

Rounding a corner moves the free part of the two walls, so an opening close to the corner may stop fitting. See "When an opening stops fitting".

### point remove

Removes a point.

| Option | Meaning |
|---|---|
| `<id>` | Required. The point to remove. |
| `--cascade` | Also remove what depends on the point. |

```
plynth point remove p6 --cascade
```

```json
{"ok":true,"command":"point remove","result":{"removed":{"openings":["o1"],"rooms":["r1"],"walls":["w3","w6","w7"],"points":["p3","p6"]}}}
```

Without `--cascade`, a point that a wall uses is refused with `in_use`. With it, everything goes in one step: the openings on those walls, the walls at the point, the rooms that use those walls, and the end points of removed walls that are left with no wall. A point you placed on its own is only removed when you name it. The `removed` lists show what went. `wall remove` and `opening remove` give the same shape.

### point list

Lists the points of a floor.

| Option | Meaning |
|---|---|
| `--floor <id>` | Default: the first floor. |

```
plynth point list
```

```json
{"ok":true,"command":"point list","result":{"floor":"f1","points":[{"id":"p1","floor":"f1","x":1.0,"y":1.0,"corner_radius":0.0,"corner_radius_used":0.0,"walls":["w1","w4"]}]}}
```

The example shows one point. The real answer has all of them.

### wall add

Adds a straight wall between two existing points.

| Option | Meaning |
|---|---|
| `--from <id>` | Required. Id of the first point. |
| `--to <id>` | Required. Id of the second point. |
| `--thickness <metres>` | Default: 0.2. |
| `--height <metres>` | Default: the floor's ceiling height. The answer shows `null` when it is not set. |

```
plynth wall add --from p1 --to p2 --thickness 0.3
```

```json
{"ok":true,"command":"wall add","result":{"id":"w1","floor":"f1","from":"p1","to":"p2","thickness":0.3,"height":null,"bulge":0.0,"length":4.0,"radius":null,"height_used":3.0,"rooms":[],"openings":[]}}
```

A wall that names a point that does not exist is refused with `not_found`, and the project files are not touched. A second wall between the same two points is refused with `already_exists`. A new wall is always straight. Bend it afterwards with `wall set --bulge`.

Every answer that shows a wall has the same shape (the "wall entry"):

- `from` and `to` are the point ids. The wall's direction runs from `from` to `to`. This matters for sides, see "Left and right" below.
- `thickness` is in metres.
- `height` is the wall's own height, or `null` when it uses the ceiling height of its floor.
- `height_used` is the height that applies: the own height, or else the floor's ceiling height.
- `bulge` is how far the middle of the wall is bent away from the straight line, in metres. 0 is straight. See `wall set`.
- `length` is the length along the wall, along the curve when it is bent, in metres.
- `radius` is the radius of the arc, or `null` for a straight wall.
- `rooms` lists the ids of the rooms that use the wall.
- `openings` lists the ids of the openings on the wall.

### wall draw

Draws a wall between two places. A place is an existing point or a position in metres. Positions can snap to points and onto walls, the way clicks do in the app. When an end lands on the body of another wall, that wall is split there so the two walls meet at a shared point (a T junction). Everything is one change that can be undone as one step.

Give each end in one of two forms. Giving both forms, or neither, is `bad_arguments`.

| Option | Meaning |
|---|---|
| `--from <id>` or `--from-x <m>` `--from-y <m>` | The start: a point, or a position. |
| `--to <id>` or `--to-x <m>` `--to-y <m>` | The end: a point, or a position. |
| `--snap <metres>` | Snap radius. A position within this distance of a point snaps to the point, and one within it of a wall snaps onto the wall. Default: 0, which means the positions are used exactly. |
| `--grid` | Snap new positions to the project grid. Points and walls within the snap radius still win. |
| `--thickness <metres>` | Default: 0.2. |
| `--height <metres>` | Default: the floor's ceiling height. |
| `--floor <id>` | Default: the first floor. |

```
plynth wall draw --from-x 4 --from-y 1 --to-x 4 --to-y 5 --snap 0.1 --thickness 0.3
```

```json
{"ok":true,"command":"wall draw","result":{"id":"w7","floor":"f1","from":"p5","to":"p6","thickness":0.3,"height":null,"bulge":0.0,"length":4.0,"radius":null,"height_used":3.0,"rooms":[],"openings":[],"new_points":["p5","p6"],"split_walls":["w1","w3"]}}
```

The answer is a wall entry plus `new_points` (points that were created) and `split_walls` (walls that were cut). In the example the new wall runs across a rectangle, so the top and bottom walls were split and the new wall joins them.

To draw a closed loop, start each wall at the `to` point of the last one with `--from <id>`, and end the last wall on the first point with `--to <id>`.

### wall split

Cuts a wall in two at a distance from its start.

| Option | Meaning |
|---|---|
| `<id>` | Required. The wall to cut. |
| `--offset <metres>` | Required. Distance from the wall's `from` point. It must be inside the wall, not at an end. |

```
plynth wall split w5 --offset 1
```

```json
{"ok":true,"command":"wall split","result":{"point":{"id":"p7","floor":"f1","x":5.0,"y":1.0,"corner_radius":0.0,"corner_radius_used":0.0,"walls":["w5","w8"]},"walls":[{"id":"w5","floor":"f1","from":"p5","to":"p7","thickness":0.3,"height":null,"bulge":0.0,"length":1.0,"radius":null,"height_used":3.0,"rooms":[],"openings":[]},{"id":"w8","floor":"f1","from":"p7","to":"p2","thickness":0.3,"height":null,"bulge":0.0,"length":2.0,"radius":null,"height_used":3.0,"rooms":[],"openings":[]}]}}
```

The old wall keeps its id and runs to the new point. A new wall runs on to the old end. Rooms that used the wall get the new wall too, so they stay closed. Openings that were past the cut move to the new wall and keep their place.

A curved wall can be split. `--offset` is then measured along the curve. Both parts stay on the same circle, so they have the same radius and bend the same way, and the new point lies on the arc.

### wall set

Changes a wall. Give at least one option. Options you leave out keep their value. Several options in one command run as one step: if one is refused, none is applied (`--thickness 0.3 --bulge 9` changes nothing).

| Option | Meaning |
|---|---|
| `<id>` | Required. The wall. |
| `--thickness <metres>` | New thickness. Above zero. |
| `--height <metres\|none>` | The wall's own height, or `none` to use the floor's ceiling height again. Above zero. |
| `--bulge <metres>` | Bend the wall. Positive bends it to the right, negative to the left. 0 makes it straight. |

```
plynth wall set w3 --bulge 0.5
```

```json
{"ok":true,"command":"wall set","result":{"id":"w3","floor":"f1","from":"p3","to":"p4","thickness":0.2,"height":null,"bulge":0.5,"length":4.16463727315669,"radius":4.25,"height_used":2.8,"rooms":["r1"],"openings":["o2"]}}
```

The answer is a wall entry.

**Bulge.** A bulge turns a straight wall into a true circular arc between the same two points. The number is how far the middle of the arc sits from the middle of the straight line, in metres. A wall 4 m long with a bulge of 0.5 bows out by half a metre and has a radius of 4.25 m. Its `length` is the length along the arc, 4.16 m, and that is the length openings are measured along.

- Positive bulge bends to the right of the wall's direction, negative to the left (see "Left and right").
- A bulge smaller than 0.001 m in either direction is stored as 0, a straight wall.
- The largest bulge is half the straight length, which is a half circle. A 4 m wall can bulge at most 2 m. Anything more is refused with `invalid_value`: "The bend is too large for this wall. The most is a half circle, a bend of 2.00 m."
- The radius must stay above half the thickness, so a very thick wall cannot be bent into a tight curve. This is checked when you bend a wall, change its thickness or move one of its points.

Bending a wall keeps the offsets of its openings. If the new length is shorter, an opening can end up past the free part. See "When an opening stops fitting".

**Height.** `--height 1.1` makes a half wall. `--height none` goes back to the ceiling height. A height that would leave an opening above the top of the wall is refused with `invalid_value`, and the message names the opening:

```json
{"ok":false,"command":"wall set","error":{"code":"invalid_value","message":"The opening o1 would reach above the wall, which would be 2.00 m high."}}
```

Remove or resize the opening first, or pick a larger height.

### wall remove

Removes a wall.

| Option | Meaning |
|---|---|
| `<id>` | Required. The wall. |
| `--cascade` | Also remove what depends on it. |

```
plynth wall remove w2 --cascade
```

```json
{"ok":true,"command":"wall remove","result":{"removed":{"openings":["o1"],"rooms":["r1"],"walls":["w2"],"points":[]}}}
```

Without `--cascade`, a wall that a room or an opening uses is refused with `in_use`. With it, the openings on the wall go as well, then the rooms that use the wall, and so do end points that are left with no wall.

### wall list

Lists the walls of a floor, with `--floor <id>` (default: the first floor).

```
plynth wall list
```

```json
{"ok":true,"command":"wall list","result":{"floor":"f1","walls":[{"id":"w1","floor":"f1","from":"p1","to":"p5","thickness":0.25,"height":null,"bulge":0.0,"length":3.0,"radius":null,"height_used":3.0,"rooms":[],"openings":[]}]}}
```

### floor list

Lists the floors of the project and their ceiling heights. It needs no `--floor` option.

```
plynth floor list
```

```json
{"ok":true,"command":"floor list","result":{"floors":[{"id":"f1","name":"Floor 1","image":"plans/apartment-01.png","ceiling_height":2.8}]}}
```

`image` is `null` for a floor with no plan image. Every other command that takes `--floor` uses the ids from this list.

### floor set

Sets the ceiling height of a floor. Walls without their own height follow it.

| Option | Meaning |
|---|---|
| `--ceiling-height <metres>` | Required. Above zero. |
| `--floor <id>` | Default: the first floor. |

```
plynth floor set --ceiling-height 2.8
```

```json
{"ok":true,"command":"floor set","result":{"id":"f1","name":"Floor 1","image":"plans/apartment-01.png","ceiling_height":2.8}}
```

A ceiling height that would leave an opening above the top of a wall is refused with `invalid_value`. The message names the first opening that would not fit, and nothing is changed:

```json
{"ok":false,"command":"floor set","error":{"code":"invalid_value","message":"The opening o1 would reach above the wall w2, which would be 2.00 m high."}}
```

A wall that has its own height is not affected by the ceiling height. To change the height for new floors, use `project set --new-floor-height`.

### Left and right

Many options talk about the two sides of a wall. The rule is the same everywhere:

- A wall runs from its `from` point to its `to` point.
- Stand on the `from` point and look toward the `to` point. The side on your right hand is the **right** side. The plan is drawn with y growing downward, as on screen, so this is the right hand side as you see the plan.
- A wall that runs east has its right side to the south. A wall that runs south has its right side to the west.

A positive bulge bends a wall toward its right side. `swing` names a side. A wall seen from its right side shows its `from` point on the left.

### opening add

Adds a door, window or plain opening to a wall. An opening is a cut through the whole wall. A door also has a hinge and a swing, which only matter for doors.

Say where it goes in one of two ways. Giving both ways, or neither, is `bad_arguments`.

| Option | Meaning |
|---|---|
| `--wall <id>` `--offset <metres>` | On this wall, with its centre this far along the wall from the wall's `from` point. Along the curve when the wall is bent. |
| `--x <metres>` `--y <metres>` | At a place on the plan. Plynth finds the nearest wall and the place along it, the way a click does in the app. |
| `--snap <metres>` | With `--x` and `--y`: how far a wall may be from the place. Default: 0.3. No wall that close is `not_found`. |
| `--step <metres>` | With `--x` and `--y`: round the offset to a multiple of this. Default: 0, no rounding. The app uses 0.05. |
| `--floor <id>` | With `--x` and `--y`: the floor. Default: the first floor. |
| `--type door\|window\|plain` | Default: `door`. |
| `--shape <id>` | An opening shape, see `opening shapes`. Default: `rectangle`. |
| `--width <metres>` | Default: from the project's default sizes for the type. |
| `--height <metres>` | Default: from the project's default sizes for the type. |
| `--sill <metres>` | How far above the floor the bottom of the opening is. Default: 0 for doors and plain openings, the window sill default for windows. |
| `--hinge start\|end` | The jamb a door hangs on. `start` is the jamb nearer the wall's `from` point. Default: `start`. |
| `--swing left\|right` | The side of the wall a door opens toward. Default: `right`. |

With no size options, a door takes the default door size, a window the default window size, and a plain opening the door size with no sill. Change the defaults with `project set`.

```
plynth opening add --wall w2 --offset 2 --type door --swing right
```

```json
{"ok":true,"command":"opening add","result":{"id":"o1","floor":"f1","wall":"w2","offset":2.0,"type":"door","shape":"rectangle","width":0.9,"height":2.1,"sill":0.0,"hinge":"start","swing":"right","frame":null,"centre":{"x":5.0,"y":5.0},"fits":true,"problem":null}}
```

The same opening by place on the plan. This one snaps to wall `w4` and rounds the offset to 5 cm:

```
plynth opening add --x 1.05 --y 5 --type window --step 0.05
```

```json
{"ok":true,"command":"opening add","result":{"id":"o4","floor":"f1","wall":"w4","offset":2.0,"type":"window","shape":"rectangle","width":1.2,"height":1.2,"sill":0.9,"hinge":"start","swing":"right","frame":null,"centre":{"x":1.0,"y":5.0},"fits":true,"problem":null}}
```

A round arch window and a plain pointed arch opening on a curved wall:

```
plynth opening add --wall w3 --offset 2 --type window --shape round_arch
plynth opening add --wall w1 --offset 3.1416 --type plain --shape pointed_arch --width 1 --height 2.4
```

Every answer that shows an opening has the same shape (the "opening entry"):

| Field | Meaning |
|---|---|
| `id` | `o1`, `o2` and so on. |
| `floor`, `wall` | Where it is. |
| `offset` | Distance along the wall from its `from` point to the centre of the opening, in metres. Along the curve when the wall is bent. |
| `type`, `shape` | `door`, `window` or `plain`, and the shape id. |
| `width`, `height` | Size in metres. Width is measured along the wall. |
| `sill` | Height of the bottom above the floor. |
| `hinge`, `swing` | `start` or `end`, and `left` or `right`. Kept for every type, used for doors. |
| `frame` | Always `null` for now. It is reserved for the asset catalog. |
| `centre` | The plan position of the opening's centre on the wall's centre line, in metres. |
| `fits` | `false` when the opening no longer fits its wall. |
| `problem` | `null`, or the sentence that says why it does not fit. |

**Hinge and swing.** A door hangs on one of its two jambs and opens toward one side of the wall. `hinge` is `start` when the hinge is on the jamb nearer the wall's `from` point and `end` when it is on the other one. `swing` is the side of the wall the leaf swings toward, with the left and right rule above. In the app the leaf is drawn as a line from the hinge jamb with a quarter arc to the other jamb. A door that swings into a room has the room on its `swing` side.

**The fit rules.** `opening add`, `opening move`, `opening resize` and `opening set` refuse an opening that does not fit. They answer `invalid_value` with a sentence and leave `plan.json` exactly as it was. The rules are checked in this order:

1. The wall must exist on the floor (`not_found`).
2. Offset, width, height and sill must be numbers, width and height above zero, sill zero or more.
3. The shape must exist and be able to take the size. Each shape's rule is in `opening shapes`. An unknown shape lists the known ones: `The opening shape "keyhole" is not known. Known shapes: rectangle, round_arch, flat_arch, pointed_arch, circle.`
4. The whole width must lie inside the **free part** of the wall. The free part leaves out the rounded corner at each end, and half the thickness of the thickest other wall that meets the wall there and does not simply continue it. A wall end with nothing else touching it has no clearance. A wall that continues in a straight line past the point (within 10 degrees) leaves no clearance either. The message says how far it reaches out: `The opening reaches 0.05 m past the free part of wall w4 at its start, which begins 0.10 m from the wall's end.`
5. The top of the opening, sill plus height, must be at most the wall's height: `The opening reaches 1.00 m above the top of wall w4, which is 1.10 m high.`
6. It must not overlap another opening on the same wall: `The opening overlaps o1 on wall w2.` Overlap is measured between the boxes of the two openings, so a window above a door at the same place is allowed, and two openings that only touch are allowed.

**When an opening stops fitting.** Changing a wall can break a rule after the fact: moving a point so the wall gets shorter, bending the wall, rounding a corner, making a wall thicker, or splitting. These changes are allowed, so you are never blocked from reshaping walls. The opening is kept, and `opening list` reports it with `"fits":false`:

```json
{"id":"o1","floor":"f1","wall":"w1","offset":3.0,"type":"door","shape":"rectangle","width":0.9,"height":2.1,"sill":0.0,"hinge":"start","swing":"right","frame":null,"centre":{"x":3.0,"y":0.0},"fits":false,"problem":"The opening reaches 0.25 m past the free part of wall w1 at its end, which stops 0.00 m before the wall's end."}
```

The app shows such openings with a dashed amber outline. Fix them with `opening move` or `opening resize`, or remove them. A script should read `problems` in `opening list` after reshaping walls.

### opening move

Moves an opening along its wall, up or down, or onto another wall. What you leave out stays as it was. Give at least one of `--wall`, `--offset`, `--sill`.

| Option | Meaning |
|---|---|
| `<id>` | Required. The opening. |
| `--wall <id>` | Move it onto this wall of the same floor. |
| `--offset <metres>` | The new distance along the wall to its centre. |
| `--sill <metres>` | The new height of its bottom above the floor. |

```
plynth opening move o1 --offset 1.5 --sill 0.1
```

```json
{"ok":true,"command":"opening move","result":{"id":"o1","floor":"f1","wall":"w2","offset":1.5,"type":"door","shape":"rectangle","width":0.9,"height":2.1,"sill":0.1,"hinge":"start","swing":"right","frame":null,"centre":{"x":5.0,"y":4.5},"fits":true,"problem":null}}
```

Moving to another wall keeps the opening's hinge and swing as they are, so check them if the new wall runs the other way. A wall on another floor is `invalid_value`. The fit rules apply: moving `o1` to `--wall w3 --offset 3.5` was refused with "The opening reaches 0.05 m past the free part of wall w3 at its end, which stops 0.10 m before the wall's end."

### opening resize

Gives an opening a new width or height. The centre and the sill stay where they were unless you also give `--offset` or `--sill`. Give at least one of `--width` and `--height`.

| Option | Meaning |
|---|---|
| `<id>` | Required. The opening. |
| `--width <metres>` | The new width. |
| `--height <metres>` | The new height. |
| `--offset <metres>` | Also move the centre to this distance along the wall. |
| `--sill <metres>` | Also set the height of the bottom. |

```
plynth opening resize o2 --width 1 --height 1.5
```

```json
{"ok":true,"command":"opening resize","result":{"id":"o2","floor":"f1","wall":"w3","offset":2.0,"type":"window","shape":"round_arch","width":1.0,"height":1.5,"sill":0.9,"hinge":"start","swing":"right","frame":null,"centre":{"x":3.0,"y":7.0},"fits":true,"problem":null}}
```

### opening set

Changes the type, shape, hinge or swing of an opening. What you leave out stays. Give at least one option.

| Option | Meaning |
|---|---|
| `<id>` | Required. The opening. |
| `--type door\|window\|plain` | The new type. The size and sill are kept, not reset to the defaults of the new type. |
| `--shape <id>` | The new shape. It has to take the opening's size. |
| `--hinge start\|end` | The jamb a door hangs on. |
| `--swing left\|right` | The side a door opens toward. |

```
plynth opening set o1 --hinge end --swing left
```

```json
{"ok":true,"command":"opening set","result":{"id":"o1","floor":"f1","wall":"w2","offset":2.0,"type":"door","shape":"rectangle","width":0.9,"height":2.1,"sill":0.0,"hinge":"end","swing":"left","frame":null,"centre":{"x":5.0,"y":5.0},"fits":true,"problem":null}}
```

Setting a shape that is not registered is refused, and so is a shape whose size rule the opening breaks, such as a round arch on an opening that is 0.9 m wide and 0.4 m high.

### opening remove

Removes an opening. Nothing else is removed.

```
plynth opening remove o4
```

```json
{"ok":true,"command":"opening remove","result":{"removed":{"openings":["o4"],"rooms":[],"walls":[],"points":[]}}}
```

### opening list

Lists the openings of a floor. It changes nothing and works while the app is editing the project.

| Option | Meaning |
|---|---|
| `--floor <id>` | Default: the first floor. |
| `--wall <id>` | Only the openings on this wall. |

```
plynth opening list --wall w2
```

```json
{"ok":true,"command":"opening list","result":{"floor":"f1","openings":[{"id":"o1","floor":"f1","wall":"w2","offset":2.0,"type":"door","shape":"rectangle","width":0.9,"height":2.1,"sill":0.0,"hinge":"end","swing":"left","frame":null,"centre":{"x":5.0,"y":5.0},"fits":true,"problem":null}],"problems":0}}
```

`problems` counts the openings in the list that do not fit. A wall that is not on the floor is `not_found`.

### opening shapes

Lists the opening shapes that Plynth knows. It needs no project, so it does not take `--project`.

```
plynth opening shapes
```

```json
{"ok":true,"command":"opening shapes","result":{"shapes":[{"id":"rectangle","name":"Rectangle","built_in":true,"size_rule":""},{"id":"round_arch","name":"Round arch","built_in":true,"size_rule":"The height must be at least half the width."},{"id":"flat_arch","name":"Flat arch","built_in":true,"size_rule":"The height must be at least a sixth of the width."},{"id":"pointed_arch","name":"Pointed arch","built_in":true,"size_rule":"The height must be at least 0.866 times the width."},{"id":"circle","name":"Circle","built_in":true,"size_rule":""}]}}
```

`size_rule` is empty when any size works. Custom shapes are listed after the built-in ones with `built_in` false. The command line does not read the custom shape folder yet. See [Writing generators](GENERATORS.md) for the file format and for when the folder is read.

### Rooms

A room is a closed area made of walls, with an optional type such as Living room. The room commands work on the walls you already drew. Curved walls and rounded corners are followed: a room closed by a wall bent into a half circle has the area of that half circle.

The script way to make rooms: draw the walls, run `room find` to see the closed areas, then run `room add` with a position inside each area you want.

The `room` word is a command, so a project folder named `room` has to be given as `--project .\room`, and opened from a terminal as `.\room`.

A room entry looks like this:

```json
{"id":"r1","floor":"f1","type":"living","type_name":"Living room","walls":["w1","w7","w6","w4"],"closed":true,"area":23.0706,"label":{"x":4.15,"y":5.06}}
```

- `type` is the type id, or `null` when the room has none. `type_name` is its readable name, or "Room" when there is no type.
- `walls` are the walls of the area the room was made from.
- `closed` is false if a wall of the room has been removed or its ring no longer closes. `area` is then 0.
- `area` is the floor inside the walls in square metres, rounded to 4 decimals. It is the centre line area minus the band each wall covers, so a 4 m by 4 m square of 0.2 m walls gives 14.44. It is not the area between the centre lines.
- `label` is the spot where the app writes the name and area.

### room find

Lists the closed areas on a floor, whether or not a room exists for them. It changes nothing, and it works while the app is editing the project.

| Option | Meaning |
|---|---|
| `--floor <id>` | Default: the first floor. |

```
plynth room find
```

```json
{"ok":true,"command":"room find","result":{"floor":"f1","areas":[{"walls":["w1","w7","w6","w4"],"centre_line_area":26.5,"inside":{"x":3.75,"y":4.5},"room":null}]}}
```

`centre_line_area` is measured between the wall centre lines, because no room exists yet to measure the floor of. `inside` is a position that is surely inside the area, ready to pass to `room add`. `room` is the id of the room that holds the area, or `null`.

Walls that only dangle into an area are not part of it. A wall that crosses another without a shared point does not split an area, so draw walls with `wall draw` and `--snap` to get shared points.

### room add

Makes a room from the closed area around a position.

| Option | Meaning |
|---|---|
| `--x <metres>` `--y <metres>` | Required. A position inside the area. |
| `--type <id>` | Optional. A room type id, see `room types`. |
| `--floor <id>` | Default: the first floor. |

```
plynth room add --x 2.5 --y 3 --type living
```

The answer is a room entry. A position outside every closed area gives `not_found`. A position inside an existing room gives `already_exists`. An unknown type gives `invalid_value` and lists the known ids.

### room set

Changes the type of a room.

| Option | Meaning |
|---|---|
| `<id>` | Required. The room. |
| `--type <id\|none>` | Required. A type id, or `none` to clear the type. |

```
plynth room set r1 --type none
```

The answer is a room entry with `type` null and `type_name` "Room".

### room remove

Removes a room. Its walls stay.

```
plynth room remove r1
```

```json
{"ok":true,"command":"room remove","result":{"removed":{"rooms":["r1"],"walls":[],"points":[]}}}
```

### room list

Lists the rooms of a floor, with `--floor <id>` (default: the first floor).

```
plynth room list
```

```json
{"ok":true,"command":"room list","result":{"floor":"f1","rooms":[]}}
```

The example is a plan with no rooms yet. Each room in the list is a room entry.

### room types

Lists the built-in room types. It needs no project, so it does not take `--project`.

```
plynth room types
```

```json
{"ok":true,"command":"room types","result":{"room_types":[{"id":"living","name":"Living room"},{"id":"bedroom","name":"Bedroom"},{"id":"kitchen","name":"Kitchen"},{"id":"bathroom","name":"Bathroom"},{"id":"hall","name":"Hall"},{"id":"dining","name":"Dining room"},{"id":"office","name":"Office"},{"id":"storage","name":"Storage"},{"id":"other","name":"Other"}]}}
```

A plan from another machine can hold a type id that is not in this list. It still loads, and the room shows its id as the name. Only new values typed into `room add` and `room set` are checked.

### detect

Looks at the plan image of a floor and proposes walls, rooms, and doors, windows and gaps. It needs a plan image and a scale. Without `--apply` it only reads: nothing in the project changes, and it works while the app is editing the project. With `--apply` it adds the new walls, openings and rooms to the plan and saves.

The built-in detector (`orthogonal`, "Straight walls") finds horizontal and vertical walls. Walls a few degrees off, such as a slightly crooked scan, are found and drawn straight. Diagonal and curved walls are not found. Draw those with `wall draw`. It works best on a clean plan with dark, solid walls. Thin lines (text, dimension lines, door arcs, window lines, furniture) are ignored.

The `detect` word is a command, so a project folder named `detect` has to be given as `--project .\detect`.

| Option | Meaning |
|---|---|
| `--floor <id>` | Default: the first floor. |
| `--detector <id>` | Which detector to use. Default: `orthogonal`. An unknown id is `invalid_value`, and the message lists the ids. |
| `--min-thickness <metres>` | Dark strokes thinner than this are not walls. Default: 0.08. |
| `--max-thickness <metres>` | Dark areas thicker than this are not walls, for example a filled block. Default: 0.5. Must be above `--min-thickness`. |
| `--sensitivity <0..1>` | Higher counts lighter grey as ink. Default: 0.5. |
| `--min-length <metres>` | Wall pieces shorter than this are dropped. Default: 0.3. |
| `--max-opening-width <metres>` | A gap up to this wide in a wall is closed with wall and listed as an opening. Default: 2.5. 0 closes no gaps. |
| `--no-rooms` | Propose no rooms, and add none with `--apply`. |
| `--no-openings` | Find no openings, and add none with `--apply`. The walls are then closed across the gaps. |
| `--apply` | Add the new walls, openings and rooms to the plan, as one undoable step, and save. |

```
plynth detect --project D:\Plans\MyApartment
```

This is a real answer for `samples\plans\apartment-01.png` at 100 pixels per metre. The lists are cut short: the real answer has all 12 points, 16 walls, 9 openings and 5 rooms.

```json
{"ok":true,"command":"detect","result":{"floor":"f1","detector":"orthogonal","detectors":["orthogonal"],"pixels_per_metre":100.0,"image":{"width":2400,"height":1600},"params":{"min_thickness":0.08,"max_thickness":0.5,"sensitivity":0.5,"min_length":0.3,"max_opening_width":2.5,"rooms":true,"openings":true},"elapsed_ms":20.9,"counts":{"points":12,"walls":16,"new_walls":16,"existing_walls":0,"overlapping_walls":0,"rooms":5,"new_rooms":5,"openings":9,"new_openings":9,"existing_openings":0},"points":[{"index":0,"x":5.15,"y":3.15},{"index":1,"x":10.06,"y":3.15}],"walls":[{"index":0,"from":0,"to":1,"thickness":0.3,"length":4.91,"state":"new"}],"openings":[{"index":0,"wall":0,"type":"window","offset":2.35,"width":2.0,"hinge":"start","swing":"right","state":"new","x":7.5,"y":3.15}],"rooms":[{"index":0,"walls":[0,10,11,3,8],"inside":{"x":7.605,"y":5.5751},"centre_line_area":23.8147,"state":"new"}]}}
```

The answer has these fields.

- `floor`, `detector`, `detectors` (every detector that is installed), `pixels_per_metre`, `image` (width and height in pixels) and `params` (the settings that were used, including `rooms` and `openings`).
- `elapsed_ms` is the time spent finding walls, in milliseconds. Loading the image is not counted.
- `counts` says how many proposals there are. `walls` is the total. `new_walls` are the ones `--apply` would add. `existing_walls` are already in the plan. `overlapping_walls` run partly along a wall that is already there. `new_rooms` are rooms not yet in the plan. `openings` is the total number of openings found, `new_openings` the ones `--apply` would add, and `existing_openings` the ones the plan already has.
- `points` are the proposed corners and junctions, in metres, numbered by `index`.
- `walls` join two points by their `index` (`from` and `to`). `thickness` and `length` are in metres. `state` is `new`, `existing` or `overlaps`.
- `openings` sit on a wall (`wall` is a wall `index`). `type` is `door`, `window` or `plain`. `offset` is the distance from the wall's `from` point to the centre of the opening, `width` is its width, and `x` and `y` are its centre in metres. `hinge` and `swing` say how a door is hung, relative to the proposed wall: the jamb the leaf hangs on (`start` or `end`) and the side it opens toward (`left` or `right`, see "Left and right"). Windows and plain openings carry the default values. `state` is `new` or `existing`.
- `rooms` are the closed areas of the proposed walls. `walls` lists wall `index` numbers. `inside` is a position surely inside the area. `centre_line_area` is measured between the wall centre lines, in square metres, rounded to 4 decimals. `state` is `new` or `existing`.
- `applied` appears only with `--apply`. See below.

**States and running it twice.** Detection compares what it found with the walls and rooms the floor already has. A proposed wall that lies on a wall that is already there is `existing`. One that lies partly along an existing wall is `overlaps`. A proposed room is `existing` when a room already holds its `inside` position. A proposed opening is `existing` when an opening of the plan already covers its centre. Only `new` proposals are added. So running `detect --apply` again does not double anything, and walls you drew by hand are not drawn twice. A proposal that is `overlaps` is left out and counted, never trimmed. Draw the missing piece yourself.

**`--apply`.** The new walls, then the new openings, then the new rooms are added as one undoable step. Ends of new walls snap to points and walls that are already in the plan, so detected walls join hand drawn ones, and a wall that a new one ends on is split there. The answer then also has:

```json
"applied":{"walls":["w1","w2"],"points":["p1","p2"],"split_walls":[],"openings":["o1","o2"],"rooms":["r1"],"skipped_walls":0,"skipped_openings":0,"skipped_rooms":0}
```

The ids are the ones that were created. `split_walls` lists existing walls that were cut at a T junction. `openings` lists the new openings. `skipped_walls`, `skipped_openings` and `skipped_rooms` count proposals the plan refused. The `points`, `walls` and `rooms` lists above it show the states from before applying. When no wall, opening or room is new, `--apply` changes and saves nothing, and answers ok with empty `applied` lists.

**Openings are added.** Each new opening goes on the plan wall that holds its centre, with the hinge and swing the detector found, turned round when that wall runs the other way. Its height and sill come from the project's default sizes (see `project set`): a door gets the door height and no sill, a window the window height and sill, and a plain opening the door height. The shape is `rectangle`. An opening that hangs over the free part of its wall by at most 5 cm is moved, and narrowed if needed, so it fits. One that still does not fit is skipped and counted in `skipped_openings`. The wall is closed across the gap while detecting, so rooms stay separate, and the opening then cuts it again. After `--apply`, run `opening list` to see them. `problems` should be 0.

The script order for a new project:

```
plynth project new --image plan.png --project D:\Plans\MyApartment
plynth scale set --pixels-per-metre 100 --project D:\Plans\MyApartment
plynth detect --project D:\Plans\MyApartment
plynth detect --apply --project D:\Plans\MyApartment
plynth opening list --project D:\Plans\MyApartment
plynth room set r1 --type living --project D:\Plans\MyApartment
```

On `apartment-01.png` the first `detect` finds 9 openings (`new_openings` 9). After `--apply`, `opening list` shows 9 openings and `problems` 0, and a second `detect` shows `existing_openings` 9 and `new_openings` 0.

Errors:

| Code | When |
|---|---|
| `not_found` | The floor does not exist, or has no plan image, or the image file is missing. |
| `invalid_value` | The plan has no scale ("Set the scale first. Run 'scale from-points' or 'scale set'."), the detector id is unknown, or a setting is out of range: a number that is not finite, a thickness or length of 0 or less, a negative opening width, `--min-thickness` not below `--max-thickness`, or `--sensitivity` outside 0 to 1. |
| `image_error` | The image cannot be decoded, or is too large to process. |
| `locked` | With `--apply` only: the project is open in the app and no edit session is active. Plain `detect` is never refused. |

### session begin

Starts an edit session. See the next section.

| Option | Meaning |
|---|---|
| `--tool <name>` | A name shown in the app while the session is active. 1 to 64 characters. Default: `plynth`. |

```
plynth session begin --tool my-script
```

```json
{"ok":true,"command":"session begin","result":{"state":"active","tool":"my-script","pid":4242,"started":"2026-11-21T10:00:00Z","last_activity":"2026-11-21T10:00:00Z","expires_at":"2026-11-21T10:05:00Z","already_active":false,"app_open":true,"app_modified":false}}
```

Running it again while a session is active keeps `started`, refreshes `last_activity` and gives `already_active: true`. Times are UTC. `app_open` says whether the app has the project open. `app_modified` says whether the app has unsaved changes.

### session end

Ends the session and lets the app edit again.

```
plynth session end
```

```json
{"ok":true,"command":"session end","result":{"ended":true}}
```

`ended` is false when there was no active session. The command works in any state.

### session status

Reports the session without changing it. It does not count as activity, so a script can poll it.

```
plynth session status
```

```json
{"ok":true,"command":"session status","result":{"state":"none","tool":null,"pid":null,"started":null,"last_activity":null,"expires_at":null,"app_open":false,"app_modified":false}}
```

`state` is `none`, `active` or `expired`. The other fields are filled in when a session exists.

## File format version 2

`plan.json` and `project.json` are now at format version 2. What changed:

- Every opening in `plan.json` has `hinge` (`start` or `end`) and `swing` (`left` or `right`).
- `project.json` has `defaults`, the default sizes (see `project set`).

**Older projects upgrade by themselves.** A version 1 file is read and upgraded in memory. Openings get `"hinge": "start"` and `"swing": "right"`, and the project gets the built-in default sizes. Nothing is written when you only read. The next command that edits and saves writes both files as version 2:

```
plynth project info --project D:\Plans\OldFlat
plynth wall set w1 --thickness 0.25 --project D:\Plans\OldFlat
```

After the second command, `plan.json` and `project.json` both say `"version": 2`, and every opening has `hinge` and `swing`. `project info` reports `"version":2` under `plan` straight away, because that is the version it would save. The app does the same when it opens and saves an old project.

**Older programs refuse the new files.** A version of Plynth from before this change reads only version 1. Given a version 2 file it answers `unsupported_version`. Keep a copy of a project folder if you still need to open it with the old program.

**Files from a newer Plynth** than the one you run are refused with `unsupported_version`, and nothing is changed. The version is in the file's `version` field. The shape of each version is in `schema/plan.schema.json` and `schema/project.schema.json` (version 2) and `schema/plan.v1.schema.json` and `schema/project.v1.schema.json` (version 1, kept for reference).

## The script order

A script builds a plan in this order, because each step needs the one before it:

1. Project and scale: `project new`, `scale set`.
2. Default sizes, if you want others than the built-in ones: `project set`.
3. Walls: `wall draw` (or `point add` and `wall add`), then `wall set --bulge` for curved walls and `point set --corner-radius` for rounded corners. Heights of walls and floors can be set now or later: `floor set`, `wall set --height`.
4. Rooms: `room find`, `room add`.
5. Openings: `opening add`, then `opening set` for shape, hinge and swing.
6. Check: `opening list` and read `problems`, `room list` and read the areas.

Bend and round before you place openings. Curves change a wall's length and corners change its free part, so an opening placed first may stop fitting. Set the ceiling height before placing tall openings, and lower a wall only after you have checked what stands on it.

A full run, with the answers shown above:

```
plynth project new --image samples\plans\apartment-01.png --project D:\scratch\m5b
plynth scale set --pixels-per-metre 100 --project D:\scratch\m5b
plynth wall draw --from-x 1 --from-y 3 --to-x 5 --to-y 3 --project D:\scratch\m5b
plynth wall draw --from p2 --to-x 5 --to-y 7 --project D:\scratch\m5b
plynth wall draw --from p3 --to-x 1 --to-y 7 --project D:\scratch\m5b
plynth wall draw --from p4 --to p1 --project D:\scratch\m5b
plynth room add --x 3 --y 5 --type living --project D:\scratch\m5b
plynth wall set w1 --bulge -2 --project D:\scratch\m5b
plynth point set p3 --corner-radius 1 --project D:\scratch\m5b
plynth room list --project D:\scratch\m5b
plynth opening add --wall w2 --offset 2 --type door --swing right --project D:\scratch\m5b
plynth opening add --wall w3 --offset 2 --type window --shape round_arch --project D:\scratch\m5b
plynth floor set --ceiling-height 2.8 --project D:\scratch\m5b
plynth opening list --project D:\scratch\m5b
```

The room is a 4 m by 4 m square of 0.2 m walls (14.44 square metres) before `wall set w1 --bulge -2` bends its top wall into a half circle bulging out of the room, and `point set p3 --corner-radius 1` rounds one corner. `room list` then reports an area of 20.2956, because the half circle adds area and the rounded corner takes a little off. `w2` runs south, so its right side is west, which is inside the room, and the door swings into the room.

## Working while the app is open

Plynth's app and the command line can both work on the same project folder. To keep one from overwriting the other, the command line uses an edit session. The steps for a script or a coding assistant:

1. **Begin.** Run `plynth session begin --tool <name>`. If the app has the project open, it turns read only: a banner appears saying "Being edited from the command line by <name>", the canvas is dimmed, and Undo, Redo, Save, Save as and Set plan image are disabled.
2. **Edit.** Run commands such as `point add`, `wall draw` and `room add`. Each one saves to the project folder. The app notices the change within a second and shows it live. Zoom and pan stay where they were.
3. **End.** Run `plynth session end`. The app reloads once and becomes editable again.

A worked example:

```
plynth session begin --tool my-script
plynth point add --x 0 --y 0
plynth point add --x 5 --y 0
plynth wall add --from p1 --to p2 --thickness 0.3
plynth session end
```

### What gets refused

If the app has the project open and no session is active, an editing command is refused:

```json
{"ok":false,"command":"point add","error":{"code":"locked","message":"The project is open in the Plynth app. Run 'plynth session begin' first so the app does not overwrite your changes."}}
```

If the app has unsaved changes, `session begin` itself is refused, because starting would throw those changes away:

```json
{"ok":false,"command":"session begin","error":{"code":"unsaved_changes","message":"The project has unsaved changes in the Plynth app. Save or discard them in the app first."}}
```

Save or discard in the app, then run `session begin` again.

If the app does not have the project open, editing commands just run. A session is still allowed and harmless, so a script can always begin one.

Read only commands such as `project info`, `point list`, `wall list`, `opening list`, `opening shapes`, `floor list`, `room list`, `room find` and `detect` without `--apply` are never refused.

### Expiry

A session lasts 5 minutes after the last command. Every command except `session status` and `session end` moves that time forward. If a script goes quiet for longer, the session ends by itself and the app becomes editable. The next edit command is then refused with `locked` and the message "The edit session ended after 5 minutes without commands. Run 'plynth session begin' again." Begin again to continue.

### Unlock now

The banner in the app has an "Unlock now" button. After a confirmation it ends the session at once. The script's next edit is refused with `locked` until it begins a new session. This is how a person takes the project back.

### Autosave

The app keeps a recovery copy of unsaved work in the project's `autosave` folder. The command line never reads, writes or removes it. `project info` only reports whether it exists.

### The two small files

The session uses two small files in the project folder: `session.lock` (the command line session) and `app-state.json` (the app has this project open). They are not part of the project. Both are safe to delete when the app is closed. A crashed app does not block the command line, because the app file counts only while the app's process is still running.
