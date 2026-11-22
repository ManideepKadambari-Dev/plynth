# Command line guide

Plynth can be driven from a terminal, a script or a coding assistant. Every command prints one JSON answer, so a program can read the result without parsing text meant for people.

This page covers the commands that exist today: version, help, creating and reading a project, setting the scale, grid and default sizes, floors, stairs, roofs, points, walls (straight and curved), rounded corners, doors, windows and other openings, rooms, placing furniture with `place` and `item`, finding walls and openings in the plan image with `detect`, building the project in Blender with `build`, the edit session that keeps the app and the command line from overwriting each other, and the machine commands: `doctor`, `settings`, `library` and `assets`. More commands arrive in later releases.

The plan and project files are now at format version 5. Older projects open and upgrade by themselves. See "File format version 5" below.

## Running a command

Two programs sit side by side in the Plynth folder.

| Program | What it is for |
|---|---|
| `plynth.com` | The one to use from a terminal. It waits for the command to finish, so the answer appears before the prompt comes back and the exit code is set. |
| `plynth.exe` | The app. It also runs commands, but a terminal does not wait for it, so the prompt can come back before the answer is printed. Use it from scripts that capture its output. |

Typing `plynth` in a terminal finds `plynth.com` first. The terminal inside Plynth is set up so `plynth` works there without any path.

The first word decides what happens:

- A command word (`version`, `help`, `project`, `scale`, `point`, `wall`, `opening`, `floor`, `room`, `place`, `item`, `detect`, `build`, `session`, `doctor`, `settings`, `library`, `assets`) runs the command and prints the answer. No window opens.
- A project folder or an app option (`--image`, `--screenshot`, `--size`, `--terminal`), or no argument at all, opens the window. `plynth.com` starts the window and returns at once.
- Anything else prints an `unknown_command` answer and exits with code 2. For example `plynth bogus`.

To open a folder that is literally named after a command word, such as `project`, `room`, `opening`, `floor`, `place`, `item`, `detect` or `build`, write `.\project`, `.\room`, `.\opening`, `.\floor`, `.\place`, `.\item`, `.\detect` or `.\build`. Without the `.\` Plynth runs the command instead. `doctor`, `settings`, `library` and `assets` are new command words, so a project folder with one of those names that worked before now needs the `.\` too. The same goes for `--project`: write `--project .\opening`.

### Which project a command works on

Like git, a command works on the project in the current folder. Add `--project <folder>` to use another one. It works on every command except `version`, `help`, `doctor`, `settings`, `library`, `assets` and `project recent`, which are about the machine and never open a project.

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

An answer can also have a top level `warnings` list next to `result`. See "Warnings" below. Paths in answers are absolute and use forward slashes. Lengths are in metres. The shape is described by `schema/cli-answer.schema.json`.

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
| `in_use` | Removing something would leave another item pointing at nothing, for example a wall that a room uses. Add `--cascade` to remove what depends on it as well (see `point remove`). Also used by `build` when another build of the same project is already running, and by `export` when a build or an export is running. |
| `not_a_project` | The folder has no `project.json`. |
| `cannot_read` | A file or folder could not be opened. |
| `cannot_write` | A file or folder could not be written. |
| `bad_file` | A project file is not valid or breaks the format rules. `error.where` says where. |
| `unsupported_version` | The file was written by a newer version of Plynth than this one. Older files are upgraded, not refused. |
| `image_error` | The plan image was refused: missing, empty, damaged, too large or not PNG, JPEG or BMP. `detect` also uses it when the image cannot be decoded or processed. |
| `locked` | The project is open in the app and no edit session is active, or the session ended. See "Working while the app is open". |
| `unsaved_changes` | The app has unsaved changes, so an edit session cannot begin. |
| `no_library` | No library folder is set, or the folder given is not a library. The message names `plynth library init`. |
| `blender_not_found` | No usable Blender: none is installed, the one asked for is missing, or it is older than 4.2. Install Blender from blender.org yourself and run `plynth doctor`. |
| `blender_failed` | Blender ran but the job failed, crashed or gave no result. The message says why, with the end of Blender's log when there is one. |
| `quality_failed` | `export` found a failed quality check and `--force` was not given. Exit 1. The report is still written. |
| `cancelled` | The job was stopped before it finished. Nothing is kept. |
| `database_error` | The catalog database could not be opened or written. `assets rescan` rebuilds it from the folders. |
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

- `defaults` are the default sizes of new openings, of the ceiling of new floors, and the height of the centre of a new wall item such as a TV (`wall_item_centre`), in metres.
- `plan.version` is the format version of the plan file as it will be saved, always 3 here.
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
| `--wall-item-centre <metres>` | Height of the middle of a wall item, such as a TV, when it is mounted without a height of its own. Default: 1.2. Above zero and below the ceiling height. |

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

The wall item centre is checked against the ceiling height in the same way:

```
plynth project set --wall-item-centre 1.4
plynth project set --wall-item-centre 3.5
```

```json
{"ok":true,"command":"project set","result":{"folder":"D:/Plans/MyApartment","name":"MyApartment","export_mode":"single","grid_size":0.5,"defaults":{"ceiling_height":3.0,"door_width":0.9,"door_height":2.1,"window_width":1.2,"window_height":1.2,"window_sill":0.9,"wall_item_centre":1.4},"has_autosave":false,"plan":{"version":3,"pixels_per_metre":100.0,"floors":[{"id":"f1","name":"Floor 1","image":"plans/apartment-01.png","image_found":true,"ceiling_height":3.0,"points":12,"walls":15,"rooms":4,"openings":8}]}}}
{"ok":false,"command":"project set","error":{"code":"invalid_value","message":"The wall item centre 3.50 m must be below the ceiling height 3.00 m."}}
```

### project recent

Lists the projects the app opened or saved lately. These are the same projects the home screen shows. The list lives in the machine settings, so this command needs no project and takes no `--project`.

| Option | Meaning |
|---|---|
| `--remove <folder>` | Take this project folder off the list. The folder and its files are not touched. |

```
plynth project recent
```

```json
{"ok":true,"command":"project recent","result":{"projects":[{"path":"D:/Plans/MyApartment","name":"MyApartment","last_opened":"2026-11-20T18:30:00Z","exists":true}],"count":1}}
```

- Most recent first, at most 30.
- `exists` is false when the folder is gone. The entry stays until you remove it. Nothing is pruned by itself.
- Only the app adds to the list, when it opens or saves a project. Opening or saving through the command line does not add anything.

```
plynth project recent --remove D:/Plans/OldFlat
```

```json
{"ok":true,"command":"project recent","result":{"removed":"D:/Plans/OldFlat","projects":[{"path":"D:/Plans/MyApartment","name":"MyApartment","last_opened":"2026-11-20T18:30:00Z","exists":true}],"count":1}}
```

A folder that is not on the list is refused with `not_found`.

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
{"ok":true,"command":"point remove","result":{"removed":{"items":[],"detached":[],"openings":["o1"],"rooms":["r1"],"walls":["w3","w6","w7"],"points":["p3","p6"]}}}
```

Without `--cascade`, a point that a wall uses is refused with `in_use`. With it, everything goes in one step: the openings on those walls, the walls at the point, the rooms that use those walls, and the end points of removed walls that are left with no wall. A point you placed on its own is only removed when you name it. The `removed` lists show what went. Every remove answer (`point remove`, `wall remove`, `opening remove`, `room remove` and `item remove`) has the same six lists, in this order: `items`, `detached`, `openings`, `rooms`, `walls`, `points`.

- `items` are furniture items that were removed.
- `detached` are wall items that were taken off a removed wall. They are not removed. They stay where they were, as floor items, with their height.

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
plynth wall remove w6 --cascade
```

```json
{"ok":true,"command":"wall remove","result":{"removed":{"items":[],"detached":["i4"],"openings":["o3"],"rooms":["r5"],"walls":["w6"],"points":[]}}}
```

Without `--cascade`, a wall that a room, an opening or a wall item uses is refused with `in_use`. With it, the openings on the wall go as well, then the rooms that use the wall, and so do end points that are left with no wall. Items hanging on the wall are not removed. They are listed in `detached` and stay in place.

### wall list

Lists the walls of a floor, with `--floor <id>` (default: the first floor).

```
plynth wall list
```

```json
{"ok":true,"command":"wall list","result":{"floor":"f1","walls":[{"id":"w1","floor":"f1","from":"p1","to":"p5","thickness":0.25,"height":null,"bulge":0.0,"length":3.0,"radius":null,"height_used":3.0,"rooms":[],"openings":[]}]}}
```

### floor list

Lists the floors of the project, lowest first. It needs no `--floor` option.

```
plynth floor list
```

```json
{"ok":true,"command":"floor list","result":{"floors":[{"id":"f1","name":"Floor 1","image":"plans/ground.png","ceiling_height":3.0,"slab_thickness":0.2,"image_offset":{"x":0.0,"y":0.0},"elevation":0.0,"rise":3.2,"stairs":["s1"],"roof_sections":1},{"id":"f2","name":"Upper floor","image":"plans/upper.png","ceiling_height":3.0,"slab_thickness":0.2,"image_offset":{"x":0.0,"y":0.0},"elevation":3.2,"rise":3.2,"stairs":[],"roof_sections":1}]}}
```

| Field | Meaning |
|---|---|
| `image` | The plan image, or `null` for a floor with none. |
| `ceiling_height`, `slab_thickness` | In metres. The slab is the thick floor above the ceiling of this floor. |
| `image_offset` | Where the plan image sits on the plan, in metres. See `floor set`. |
| `elevation` | The level of the floor: the level below, plus that floor's ceiling height, plus its slab. The lowest floor is at 0. |
| `rise` | Ceiling height plus slab. The distance from this floor's level to the next floor's level. |
| `stairs` | The ids of the stairs that start on this floor. |
| `roof_sections` | How many roof sections the floor has now. 0 when it is fully covered by the floor above. |

Every other command that takes `--floor` uses the ids from this list. The floor entry is the answer of `floor add` and `floor set` too.

### floor add

Adds an empty floor. By default it goes on top of the others. It gets the next free id, such as `f2`.

| Option | Meaning |
|---|---|
| `--name <text>` | Default: "Floor N", N being the number of floors after adding. |
| `--ceiling-height <metres>` | Default: the project's new floor height. |
| `--slab-thickness <metres>` | From 0.05 to 1. Default 0.2. |
| `--image <file>` | A plan image for the floor (PNG, JPEG or BMP). A file from outside the project is copied into `plans/` when the project is saved. |
| `--below <floor id>` | Put the new floor right below that floor instead of on top. |

```
plynth floor add --name "Upper floor" --image samples\plans\house-01-upper.png
```

```json
{"ok":true,"command":"floor add","result":{"id":"f2","name":"Upper floor","image":"plans/house-01-upper.png","ceiling_height":3.0,"slab_thickness":0.2,"image_offset":{"x":0.0,"y":0.0},"elevation":3.2,"rise":3.2,"stairs":[],"roof_sections":0}}
```

Refusals, with nothing changed: `not_found` for an unknown `--below` floor, `invalid_value` for a ceiling height of 0 or less or a slab outside 0.05 to 1, `image_error` for an image that is missing or cannot be read.

### floor set

Changes the name, the heights, the plan image or the image offset of a floor, all in one undo step. Give at least one option.

| Option | Meaning |
|---|---|
| `--floor <id>` | Default: the first floor. |
| `--name <text>` | A new name. |
| `--ceiling-height <metres>` | Above zero. Walls without their own height follow it. |
| `--slab-thickness <metres>` | From 0.05 to 1. |
| `--image <file or none>` | Set the plan image, or clear it with `none`. |
| `--image-offset <x,y>` | Move the plan image to this offset, in metres. The floor's marks (points, walls, rooms, openings, items and stairs) move with it. |
| `--keep-marks` | With `--image-offset`, leave the marks where they are and move only the image. |

```
plynth floor set --floor f2 --image-offset 0.15,-0.05
plynth floor set --floor f2 --slab-thickness 0.3
```

The scale is one for the whole plan, so `--image-offset` is how you line the image of an upper floor up with the floor below. A point at (2, 2) on a floor whose offset goes from (0, 0) to (1, 0) ends up at (3, 2). Detection on a floor with an offset puts its proposals in the right place, by adding the offset.

A ceiling height that would leave an opening above the top of a wall is refused with `invalid_value`. The message names the first opening that would not fit, and nothing is changed:

```json
{"ok":false,"command":"floor set","error":{"code":"invalid_value","message":"The opening o1 would reach above the wall w2, which would be 2.00 m high."}}
```

A wall that has its own height is not affected by the ceiling height. To change the height for new floors, use `project set --new-floor-height`. Other refusals: `not_found` for an unknown floor, `bad_arguments` (exit code 2) for no option at all or an offset that is not two numbers.

### floor remove

Removes a floor with everything on it: its walls, rooms, openings, items, stairs and roof settings. Because that cannot be recovered by this command, `--yes` is required.

```
plynth floor remove f2 --yes
```

```json
{"ok":true,"command":"floor remove","result":{"removed":"f2","stairs_without_floor":["s1"]}}
```

`stairs_without_floor` lists the stairs on the floor below that now have no floor above them. They stay in the plan, and `stair list` and `build` report them as `stair_no_floor_above`. Remove them with `stair remove`, or add a floor on top again. The only floor of a project is refused with `invalid_value`. Without `--yes` the answer is `bad_arguments`.

### stair add

Adds a stair to a floor. It climbs to the next floor up in the plan, and its steps are worked out from the floor heights.

| Option | Meaning |
|---|---|
| `--floor <id>` | The floor the stair starts on. Default: the first floor. |
| `--x <metres>`, `--y <metres>` | Required. The middle of the front edge of the bottom step. |
| `--rot <degrees>` | The direction of the climb, clockwise from down the plan. 0 climbs toward the bottom of the plan, 90 toward the left. Any number is accepted and turned into 0 up to 360. Default 0. |
| `--type <id>` | `straight`, `l_shape`, `u_shape` or `spiral`. See `stair types`. Default `straight`. |
| `--width <metres>` | From 0.6 to 3. Default 1. |
| `--turn left\|right` | Which way an L, U or spiral stair turns, as you climb. Default `right`. Types without a turn ignore it. |
| `--material <id>` | The material of the steps. Default: none, so the project default for stairs applies. |

```
plynth stair add --floor f1 --x 1 --y 1
```

```json
{"ok":true,"command":"stair add","result":{"id":"s1","floor":"f1","to_floor":"f2","type":"straight","x":1.0,"y":1.0,"rotation":0.0,"width":1.0,"turn":"right","material":null,"rise":3.2,"riser_count":17,"riser":0.18824,"going":0.27,"length":4.32,"footprint":[[0.5,1.0],[1.5,1.0],[1.5,5.32],[0.5,5.32]],"arrival":[1.0,5.32],"problems":[]}}
```

| Field | Meaning |
|---|---|
| `to_floor` | The floor the stair reaches. `null` when there is none. |
| `rise` | The height climbed, from one level to the next. |
| `riser_count`, `riser` | The number of steps up and the height of each. A riser is at most 0.19 m and all are equal. |
| `going` | The depth of a tread, 0.27 m. |
| `length` | The walk line from the bottom step to the upper floor. |
| `footprint` | The outline of the whole stair on the plan, in metres. This is the hole cut in the slab, the ceilings below and the floors above. |
| `arrival` | Where the stair reaches the upper floor. |
| `problems` | What the build would report for this stair. Each has `code`, `severity` and `message`. See below. |

A stair that has a problem with severity `warning` is still added. Refusals, with nothing changed: `invalid_value` for a stair on the top floor ("Add a floor above before placing a stair."), for a width outside 0.6 to 3 and for an unknown type (the message lists the known ones). A rotation outside 0 to 360 is not refused. It is turned into that range, so `--rot 450` is stored as 90. `not_found` is returned for an unknown floor or an unknown material, and `bad_arguments` for a missing `--x` or `--y`.

**Stair problem codes.** They appear in `problems` and in the build report.

| Code | Severity | Meaning |
|---|---|---|
| `stair_overlaps_wall` | warning | The footprint crosses a wall on the stair's floor or on the floor above. The message names the wall. |
| `stair_outside_floor` | warning | Part of the footprint is outside the walls and rooms of the floor. |
| `stair_arrival_outside_room` | warning | The arrival point is not in any room of the floor above. |
| `stair_no_floor_above` | error | There is no floor above. The stair is left out of the build. |
| `stair_type_unknown` | error | The type is not registered. The stair is left out of the build. |

### stair set

Changes a stair. Give the id and at least one option. Everything not given stays as it is.

| Option | Meaning |
|---|---|
| `<id>` | The stair. |
| `--x`, `--y`, `--rot`, `--type`, `--width`, `--turn` | As for `stair add`. |
| `--material <id or none>` | Set the material of the steps, or clear it with `none`. |

```
plynth stair set s1 --type spiral --turn left
```

The answer is the stair entry, as for `stair add`. A stair is not moved between floors: to do that, remove it and add it again. Refusals: `not_found` for an unknown stair or material, `invalid_value` for a bad value, `bad_arguments` when no option is given.

### stair remove

Removes a stair. The hole it cut is filled again at the next build.

```
plynth stair remove s1
```

```json
{"ok":true,"command":"stair remove","result":{"removed":"s1"}}
```

### stair list

Lists the stairs of one floor (`--floor <id>`) or of every floor, each as the stair entry above, with its problems.

```
plynth stair list --floor f1
```

```json
{"ok":true,"command":"stair list","result":{"stairs":[{"id":"s1","floor":"f1","to_floor":"f2","type":"straight","x":1.0,"y":1.0,"rotation":0.0,"width":1.0,"turn":"right","material":null,"rise":3.2,"riser_count":17,"riser":0.18824,"going":0.27,"length":4.32,"footprint":[[0.5,1.0],[1.5,1.0],[1.5,5.32],[0.5,5.32]],"arrival":[1.0,5.32],"problems":[]}]}}
```

An unknown `--floor` is `not_found`.

### stair types

Lists the stair types. It needs no project.

```
plynth stair types
```

```json
{"ok":true,"command":"stair types","result":{"types":[{"id":"straight","name":"Straight","uses_turn":false},{"id":"l_shape","name":"L shape","uses_turn":true},{"id":"u_shape","name":"U shape","uses_turn":true},{"id":"spiral","name":"Spiral","uses_turn":true}]}}
```

A type that is added to Plynth later shows up here with no other change. See "Adding a stair type" in [Writing generators](GENERATORS.md).

### roof list

Lists the roof default and every roof section. Roof sections are not drawn. They are worked out from the floors: each part of a floor with no floor above it is a section. A part narrower than 0.2 m has no roof. The build gives it a plain flat top in the outside wall material instead. `--floor <id>` lists one floor only.

```
plynth roof list
```

```json
{"ok":true,"command":"roof list","result":{"default":{"type":"flat","pitch":30.0,"overhang":0.3,"parapet":0.6,"direction":null},"sections":[{"floor":"f1","index":0,"area":24.0,"label":[3.0,2.0],"section":null,"settings":{"type":"flat","pitch":30.0,"overhang":0.3,"parapet":0.6,"direction":null},"type_built":"flat","material":{"material":"concrete","origin":"default","from":null},"outline":[[0.0,0.0],[6.0,0.0],[6.0,4.0],[0.0,4.0]]}],"unused":[]}}
```

| Field | Meaning |
|---|---|
| `default` | The roof settings every section without its own uses. `direction` is `null` for automatic. |
| `sections[].floor`, `index` | The floor, and the place of the section among that floor's sections. |
| `area` | Square metres. |
| `label` | A point well inside the section. `roof set --at` takes a point like it. |
| `section` | The id of the roof entry (such as `rf1`) that governs this section, or `null` when the default does. |
| `settings` | The settings that apply: `type`, `pitch`, `overhang`, `parapet`, `direction`. |
| `type_built` | The type the build makes. It differs from `settings.type` when a hip roof falls back to gable. |
| `material` | The covering material that applies, with its `origin` (`roof` for an entry's own material, otherwise `default`) and `from`. |
| `outline` | The section's outline on the plan. A section can also have holes, which are not listed. |
| `unused` | The ids of roof entries that govern no section. See below. |

### roof set

Changes the roof default, one roof entry, or the section at a point. Give at least one setting option.

| Form | What it changes |
|---|---|
| `roof set default ...` | The roof default. |
| `roof set rf1 ...` | The roof entry `rf1`. |
| `roof set --floor f1 --at 3,2 ...` | The section of floor `f1` that contains the point. An entry is added when the section has none, starting from the settings that apply to it now. Without `--floor`, the first floor. |

| Option | Meaning |
|---|---|
| `--type <id>` | A roof type. See `roof types`. |
| `--pitch <degrees>` | The slope. A pitched type needs 5 to 75. |
| `--overhang <metres>` | How far the roof reaches past the outer walls, 0 to 2. |
| `--parapet <metres>` | The height of the low wall round a flat roof, 0 to 3. 0 for none. |
| `--direction <degrees or auto>` | From 0 up to 360, clockwise on the plan from +x. The ridge direction of a gable or sawtooth roof, the way a shed roof falls. `auto` stores nothing, so the type picks it from the shape of the section. |
| `--material <id or none>` | The covering material of an entry. Not for `default`. |

```
plynth roof set default --type gable --pitch 35 --direction 90
plynth roof set --floor f1 --at 3,2 --type hip
plynth roof set rf1 --pitch 40 --material grey_tile
```

```json
{"ok":true,"command":"roof set","result":{"type":"gable","pitch":35.0,"overhang":0.3,"parapet":0.6,"direction":90.0}}
```

The answer of `roof set default` is the settings. The answer of the other two is the section entry as in `roof list`. When an entry's point is no longer inside any section, the answer has only `floor`, `index` (`null`), `section`, `settings` and `material`.

Refusals, with nothing changed:

| Code | When |
|---|---|
| `invalid_value` | An unknown type, a number out of range, a pitched type with a pitch below 5, or `--at` with a point that is in no roof section. |
| `not_found` | An unknown roof entry, floor or material. |
| `bad_arguments` | No setting option; no target; both an id and `--at`; a `--direction` that is not a number or `auto`; `--at` that is not `x,y`; `--material`, `--floor` or `--at` with `default`. Exit code 2. |

### roof clear

Removes a roof entry, so its section follows the default again. The unused entries listed by `roof list` are removed the same way.

```
plynth roof clear rf1
```

```json
{"ok":true,"command":"roof clear","result":{"removed":"rf1"}}
```

An unknown id is `not_found`.

### roof types

Lists the roof types and the settings each one uses. It needs no project.

```
plynth roof types
```

```json
{"ok":true,"command":"roof types","result":{"types":[{"id":"flat","name":"Flat","uses":["overhang","parapet"]},{"id":"gable","name":"Gable","uses":["pitch","direction","overhang"]},{"id":"hip","name":"Hip","uses":["pitch","overhang"]},{"id":"shed","name":"Shed","uses":["pitch","direction","overhang"]},{"id":"sawtooth","name":"Sawtooth","uses":["pitch","direction","overhang"]}]}}
```

A setting a type does not use is kept but has no effect on it. A flat roof with a parapet above 0 has no overhang. A hip roof needs a convex section. On any other section the build makes a gable roof and reports `roof_type_fallback`.

**Roof problem codes.**

| Code | Severity | Meaning |
|---|---|---|
| `roof_type_fallback` | warning | The type does not fit the section (a hip roof on a section with an inward corner), so the build used the fallback type. The message says why. |
| `roof_type_unknown` | warning | The type is not registered, so the section is built flat. |
| `roof_section_unused` | warning | The entry's point lies in no section of its floor, or in a section another entry already governs. It has no effect. The `object` is the entry. |

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
{"ok":true,"command":"opening remove","result":{"removed":{"items":[],"detached":[],"openings":["o4"],"rooms":[],"walls":[],"points":[]}}}
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
{"ok":true,"command":"room remove","result":{"removed":{"items":[],"detached":[],"openings":[],"rooms":["r1"],"walls":[],"points":[]}}}
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

Lists the room types. With a library set, this is the library's list, including room types you added (see `assets room add`). Without one, it is the built-in list. It needs no project, so it does not take `--project`.

```
plynth room types
```

```json
{"ok":true,"command":"room types","result":{"room_types":[{"id":"living","name":"Living room"},{"id":"bedroom","name":"Bedroom"},{"id":"kitchen","name":"Kitchen"},{"id":"bathroom","name":"Bathroom"},{"id":"hall","name":"Hall"},{"id":"dining","name":"Dining room"},{"id":"office","name":"Office"},{"id":"storage","name":"Storage"},{"id":"other","name":"Other"}]}}
```

A plan from another machine can hold a type id that is not in this list. It still loads, and the room shows its id as the name. Only new values typed into `room add` and `room set` are checked.

### Furniture items

An item is an asset from the library placed on a floor. `place` makes one, and the `item` commands list and change them. Everything is in metres and degrees, with positions on the plan (x to the right, y down), not relative to a room.

An item has:

- `x` and `y`: the middle of its footprint, the point where the asset's bottom centre stands.
- `z`: the height of its bottom above the floor. 0 for something on the floor.
- `rotation`: degrees from 0 up to 360, clockwise on the plan. At 0 the front of the asset faces +y, down the plan. 90 turns the front toward the left of the plan.
- `size`: width (the asset's own x), depth (its own y) and height. It is copied from the library when you `place` or `item swap`. The footprint, the fit and the overlap checks then work with no library, so a project opened on another machine, or a script run without `--library`, still gets the same answers.
- `room`: the room the item's position is in, worked out on every change. It is empty when the position is in no room.
- `wall`: the wall a wall item hangs on, otherwise empty. A wall item is always put flat on the face of its wall, facing away from it.

Only `place` and `item swap` need a library, because they read the asset's size. Every other item command works without one. An item whose asset is gone from the library is kept exactly as it is, and you can still move, turn, swap and remove it.

Place and swap refuse an asset that is not in the library (`not_found`), is broken, or is not measured (`invalid_value`). Check `measured` in the asset entry.

#### Warnings and refusals

A command refuses a change that cannot be right: a number that is not finite, a size that is not above zero, `z` below zero, an unknown floor, room or wall, a wall item whose top is above its wall or that is wider than its wall, and turning a wall item. The plan is then not changed.

Problems with fit and overlap never refuse. The command works and the answer has them under `warnings` and in the item's `problems`. A rug under a table, or a sofa against a radiator, is normal in a real room, so these are for you to look at.

| Code | Meaning |
|---|---|
| `no_room` | The centre of the item is in no room. |
| `outside_room` | Part of the footprint is outside its room. |
| `overlaps_item` | Its box overlaps another item's box. Footprints must share more than 1 cm by 1 cm, and the heights must overlap too, so a TV above a cabinet is fine. `other` is the item. |
| `overlaps_wall` | The footprint overlaps a wall. A wall item's own wall is not counted. `other` is the wall. |
| `overlaps_opening` | A wall item overlaps a door or window on its wall. `other` is the opening. |
| `off_wall` | A wall item is more than 1 cm from the face of its wall, for example after you moved a wall point. Move it along the wall, or attach it again, to put it flat again. |
| `too_tall` | A floor item reaches above the ceiling, or a wall item above its wall. |
| `missing_asset` | The asset is not in the library. |
| `size_changed` | The library's size for the asset is different from the size stored in the item. `item swap <id> <same asset>` updates it. |

Add `--strict` to `place`, `item move`, `item rotate`, `item swap` and `item attach` when a script wants to be stopped by any problem. The command is then refused with `invalid_value`, the message names the first problem and counts them, and nothing is changed or saved.

#### The item entry

Every command that shows an item uses this entry:

| Field | Meaning |
|---|---|
| `id`, `floor` | The item (`i1`...) and its floor. |
| `asset`, `asset_name` | The asset id, and its name in the library. `null` when the asset is missing or no library could be opened. |
| `asset_state` | `ok`, `missing`, `size_changed`, or `unknown` when there is no library to ask. |
| `room` | The room id, or `null`. |
| `x`, `y`, `z`, `rotation` | As above. |
| `wall`, `wall_side`, `wall_offset` | For a wall item: the wall, the face it hangs on (`left` or `right`, see "Left and right") and the distance along the wall to its centre. `null` for a floor item. |
| `centre_height` | The height of the middle of the item above the floor: `z` plus half its height. |
| `size` | `width`, `depth`, `height`. |
| `footprint` | The four corners on the plan, going back right, back left, front left, front right, as seen from behind the item looking toward its front. |
| `problems` | A list of `code`, `message` and `other` (the other item, wall or opening, or `null`). Same codes as the table above. |

### place

Puts an asset on the floor or on a wall.

| Option | Meaning |
|---|---|
| `<asset>` | Required. The asset id in the library. |
| `--x <metres>` `--y <metres>` | The centre on the plan. Give both. |
| `--room <id or type>` | A room id such as `r5`, or a room type such as `living` when exactly one room on the floor has it. Alone it places the item at the room's label point. With `--x` and `--y` it checks that the position is inside that room. |
| `--wall <id>` | Hang the item on this wall. |
| `--rot <degrees>` | Rotation, clockwise. Default 0. Not with `--wall`. |
| `--z <metres>` or `--centre <metres>` | Height of the bottom, or of the centre, above the floor. Not both. Default: 0 on the floor, and the project's wall item centre for a wall item. |
| `--offset <metres>` | With `--wall`: the distance along the wall to the centre. Default: the middle of the wall. |
| `--side left\|right` | With `--wall`: the face to hang on. Default: the side facing `--room` when given, else the side `--x` and `--y` are on, else right. |
| `--snap <metres>` | Floor items only. Snap the centre to the grid, and slide the footprint flush against walls within this distance, as the app does. Default: no snapping. |
| `--floor <id>` | Default: the first floor. |
| `--strict` | Refuse when the item would have any problem. |
| `--library <folder>` | Default: the library in the settings. |

One place is needed: `--x` with `--y`, `--room` or `--wall`.

```
plynth place sofa --room living --rot 180
```

```json
{"ok":true,"command":"place","result":{"item":{"id":"i1","floor":"f1","asset":"sofa","asset_name":"Sofa","asset_state":"ok","room":"r5","x":14.41,"y":9.88,"z":0.0,"rotation":180.0,"wall":null,"wall_side":null,"wall_offset":null,"centre_height":0.425,"size":{"width":2.0,"depth":0.9,"height":0.85},"footprint":[{"x":15.41,"y":10.33},{"x":13.41,"y":10.33},{"x":13.41,"y":9.43},{"x":15.41,"y":9.43}],"problems":[]}}}
```

The values are shortened here. `living` was the type of room `r5`, and with no position the sofa went to the room's label point.

A second item that overlaps the first still works. The answer carries the warning twice, in the item and at the top level:

```
plynth place table --room living --snap 0.3
```

```json
{"ok":true,"command":"place","result":{"item":{"id":"i2","floor":"f1","asset":"table","asset_name":"Table","asset_state":"ok","room":"r5","x":14.5,"y":10.0,"z":0.0,"rotation":0.0,"wall":null,"wall_side":null,"wall_offset":null,"centre_height":0.375,"size":{"width":1.2,"depth":0.8,"height":0.75},"footprint":[{"x":13.9,"y":9.6},{"x":15.1,"y":9.6},{"x":15.1,"y":10.4},{"x":13.9,"y":10.4}],"problems":[{"code":"overlaps_item","message":"Table i2 overlaps Sofa i1.","other":"i1"}]}},"warnings":[{"code":"overlaps_item","message":"Table i2 overlaps Sofa i1."}]}
```

With `--strict` the same command is refused and the plan is not changed:

```
plynth place table --room living --x 14.5 --y 10 --strict
```

```json
{"ok":false,"command":"place","error":{"code":"invalid_value","message":"Table i5 overlaps Sofa i1. 2 problems in all."}}
```

A TV on a wall. Its centre is at the project's wall item centre, 1.2 m unless you changed it. It faces into the room:

```
plynth place tv --wall w6 --room living
```

```json
{"ok":true,"command":"place","result":{"item":{"id":"i4","floor":"f1","asset":"tv","asset_name":"Tv","asset_state":"ok","room":"r5","x":14.455,"y":12.67,"z":0.845,"rotation":180.0,"wall":"w6","wall_side":"left","wall_offset":4.395,"centre_height":1.2,"size":{"width":1.23,"depth":0.06,"height":0.71},"footprint":[{"x":15.07,"y":12.7},{"x":13.84,"y":12.7},{"x":13.84,"y":12.64},{"x":15.07,"y":12.64}],"problems":[]}}}
```

Refusals:

```json
{"ok":false,"command":"place","error":{"code":"not_found","message":"There is no asset sofa_01 in the library."}}
{"ok":false,"command":"place","error":{"code":"invalid_value","message":"The place (14, 10) is not inside room r1."}}
{"ok":false,"command":"place","error":{"code":"bad_arguments","message":"--rot cannot be used with --wall. A wall item faces away from its wall."}}
{"ok":false,"command":"place","error":{"code":"invalid_value","message":"The item would reach 3.25 m, but the wall is 3.00 m high."}}
```

`bad_arguments` (exit 2) also covers `--x` without `--y`, `--z` with `--centre`, `--offset` or `--side` without `--wall`, `--snap` with `--wall`, and no place at all. `--room hall` when two rooms are halls is `invalid_value` and the message lists their ids, so name one by its id.

### item list

Lists the items of a floor with their problems.

| Option | Meaning |
|---|---|
| `--floor <id>` | Default: the first floor. |
| `--room <id>` | Only the items in this room. |
| `--wall <id>` | Only the items mounted on this wall. |
| `--library <folder>` | Used to fill `asset_name`, `asset_state`, `missing_asset` and `size_changed`. Without a library the state is `unknown` and those two problems are not checked. |

```
plynth item list --room r1
```

```json
{"ok":true,"command":"item list","result":{"items":[{"id":"i3","floor":"f1","asset":"bed","asset_name":null,"asset_state":"missing","room":"r1","x":7.8,"y":5.6,"z":0.0,"rotation":0.0,"wall":null,"wall_side":null,"wall_offset":null,"centre_height":0.5,"size":{"width":1.6,"depth":2.1,"height":1.0},"footprint":[{"x":7.0,"y":4.55},{"x":8.6,"y":4.55},{"x":8.6,"y":6.65},{"x":7.0,"y":6.65}],"problems":[{"code":"missing_asset","message":"Bed i3 uses asset bed, which is not in the library.","other":null}]}],"count":1}}
```

The bed's asset folder had been taken out of the library. The item is still listed with its stored size.

### item move

Moves an item. What you leave out stays as it was.

| Option | Meaning |
|---|---|
| `<id>` | Required. The item. |
| `--x <metres>` `--y <metres>` | The new centre on the plan. Give both. A wall item slides to the nearest place along its wall and stays flat on it. |
| `--offset <metres>` | Wall items only: the new distance along the wall to the centre. |
| `--z <metres>` or `--centre <metres>` | The new height of the bottom, or of the centre. Not both. |
| `--snap <metres>` | With `--x` and `--y`, floor items only: snap as `place --snap` does. |
| `--strict`, `--library` | As for `place`. |

Give at least one of `--x` and `--y`, `--offset`, `--z` and `--centre`. `--x` and `--y` cannot be combined with `--offset`.

```
plynth item move i2 --x 11 --y 10 --snap 0.3
```

The table was wanted at x 11, and it stopped at x 10.72 because that puts its side flat against a wall face within 0.3 m. The answer is `{"item": ...}` as for `place`, with `problems` empty now. Raising a TV:

```
plynth item move i4 --offset 2 --centre 1.5
```

This answer's `problems` holds `overlaps_opening`, because the opening o3 is on the same wall at that place, and `warnings` repeats it.

### item rotate

Turns a floor item about its centre.

| Option | Meaning |
|---|---|
| `<id>` | Required. |
| `--to <degrees>` or `--by <degrees>` | Exactly one. The rotation is kept between 0 and 360, so `--by -90` on a rotation of 0 gives 270. |
| `--strict`, `--library` | As for `place`. |

```
plynth item rotate i1 --by 90
```

The sofa was at 180, and the answer has `"rotation":270.0`. A wall item cannot be turned, because it faces away from its wall:

```json
{"ok":false,"command":"item rotate","error":{"code":"invalid_value","message":"The item i4 hangs on a wall and faces away from it. Take it off the wall first."}}
```

### item swap

Puts another asset in an item's place. The position, rotation, id and room stay. A wall item stays flat on its wall and keeps its centre height.

| Option | Meaning |
|---|---|
| `<id>` | Required. The item. |
| `<asset>` | Required. The asset id in the library. |
| `--strict`, `--library` | As for `place`. |

```
plynth item swap i2 sofa
```

Swapping to the same asset the item already has copies the library's size again. That is the fix for `size_changed`. Swapping to an asset that is not in the library is refused with `not_found` and the item stays as it is.

### item attach

Hangs an item flat on a wall. Its height stays when it already has one, and when its bottom is on the floor the height of the project's wall item centre is used.

| Option | Meaning |
|---|---|
| `<id>` | Required. The item. |
| `--wall <id>` | The wall. Left out: the nearest wall within `--snap` of the item. |
| `--snap <metres>` | Without `--wall`: how far a wall may be from the item. Default 0.3. |
| `--strict`, `--library` | As for `place`. |

```
plynth item attach i4 --wall w6
```

The item goes on the face of the wall that its position is on. With no wall near, and no `--wall`, the command is refused with `not_found` and says "There is no wall within 0.3 m of i4. Give --wall."

### item detach

Takes an item off its wall. It keeps its place, rotation and height, and becomes a floor item.

```
plynth item detach i4
```

### item remove

Removes an item. The answer is the extended `removed` shape described under `point remove`.

```
plynth item remove i2
```

```json
{"ok":true,"command":"item remove","result":{"removed":{"items":["i2"],"detached":[],"openings":[],"rooms":[],"walls":[],"points":[]}}}
```

Removing a room does not remove the items in it. They stay and lose their room. Adding a room gives it the items that stand in it and have none.

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

### material set

Sets or clears the material of one wall face, one room surface or one project default. The material is an id from the materials catalog. Rules for editing are the same as for every command that changes the plan: the command takes an edit session, and is `locked` while the app has the project open without one.

```
plynth material set r3.floor oak_planks
plynth material set w5.outside red_brick
plynth material set hall.floor grey_tile
plynth material set default.ceiling none
```

The first argument is the **target**:

| Target | Sets |
|---|---|
| `w3.inside` | The material of the faces of wall `w3` that look into a room. |
| `w3.outside` | The faces of wall `w3` that look into no room. |
| `r1.floor` | The floor of room `r1`. |
| `r1.wall` | The wall faces that look into room `r1`, unless a wall names its own inside material. |
| `r1.ceiling` | The ceiling of room `r1`. |
| `s2` | The material of the steps of stair `s2`. |
| `rf1` | The covering material of roof entry `rf1`. |
| `default.<surface>` | A project default. The surfaces are `wall_inside`, `wall_outside`, `wall_top`, `floor`, `ceiling`, `door`, `frame`, `glass`, `handle`, `roof` (pitched roofs), `flat_roof` and `stair`. |

For a room, the part before the dot may also be a room type when exactly one room has it, such as `hall.floor`. With two halls the command is refused and names both ids.

A stair or a roof entry has one material. A roof entry exists only after `roof set` has given a section its own settings, so `rf1` is `not_found` until then. `none` clears the stair's or the entry's own material, and the project default for that surface applies again.

The second argument is a material id, or `none` to clear the setting. `none` needs no library. Any other id is checked against the materials catalog of the library (`--library` or the one in the settings), and the check is why the library is needed.

```json
{"ok":true,"command":"material set","result":{"target":"r3.floor","material":"oak_planks","previous":null,"resolved":{"material":"oak_planks","origin":"room","from":"r3"}}}
```

`material` is what you set (`null` for none), `previous` is what the target named before, and `resolved` is what the target shows now after the rules are applied. `origin` is `wall`, `room`, `stair`, `roof`, `default` or `none`, and `from` is the wall or room that names it, or `null` for a default or none. Clearing `w3.outside` shows `red_brick` with origin `default`.

Refusals:

| Code | When |
|---|---|
| `invalid_value` | The target is malformed ("default.nonsense"), a room type matches no room or several rooms, or the material is broken. |
| `not_found` | The wall or room does not exist, or the material is not in the library. |
| `no_library` | A material (not `none`) was given and no library is set. |
| `locked` | The project is open in the app with no edit session. |

Nothing changes after a refusal. Material choices are part of the plan, so Undo in the app undoes them.

### material list

Shows the defaults, what every wall face and room surface gets now, and every material the build would use.

```
plynth material list
```

```json
{"ok":true,"command":"material list","result":{"defaults":{"wall_inside":"white_paint","wall_outside":"red_brick","wall_top":"white_paint","floor":"oak_planks","ceiling":null,"door":"painted_wood","frame":"white_paint","glass":"clear_glass","handle":"brushed_steel"},"walls":[{"id":"w3","floor":"f1","inside":[{"material":"grey_tile","origin":"room","from":"r2","room":"r2"}],"outside":{"material":"red_brick","origin":"default","from":null}}],"rooms":[{"id":"r2","floor":"f1","type":"bathroom","materials":{"floor":{"material":"grey_tile","origin":"room","from":"r2"},"wall":{"material":"grey_tile","origin":"room","from":"r2"},"ceiling":{"material":null,"origin":"none","from":null}}}],"used":["brushed_steel","clear_glass","grey_tile","oak_planks","painted_wood","red_brick","white_paint"],"missing":[],"library":"D:/Demo/Library"}}
```

The lists are shortened. `walls` has one entry per wall with an `inside` entry for each room the wall belongs to, and `rooms` one entry per room. `stairs` has one entry per stair, `{"id", "floor", "material"}`, and `roofs` one per roof section, `{"floor", "index", "section", "material"}`, each material resolved as `{material, origin, from}`. `defaults` also has `roof`, `flat_roof` and `stair`. `used` is sorted and has no repeats. `missing` lists the used ids the library does not have. With no library, `library` is `null` and `missing` is empty. Both commands take `--project` and `--library`.

### build

Builds the project in Blender: the walls with their thickness, every opening cut in its exact shape, a door or window fitted in each door and window opening, a floor and a ceiling in every closed room, a slab between the floors, every stair, a roof on every part of a floor with nothing above it, every surface with its material, and every placed item as a copy of its asset. It saves `build\building.blend`, `build\building.fbx` and `build\materials.json`, and writes the parts the 3D view loads into `build\preview`. The app's Build button does the same thing, so both give the same result.

```
plynth build
plynth build --project D:\Plans\MyApartment
plynth build --check
plynth build --quiet --library D:\Demo\Library
```

`build` never changes the plan or the project settings. It only writes into the project's `build` folder, so it runs while the app has the project open, with no edit session. It uses the plan as saved in the project folder.

The `build` word is a command, so a project folder named `build` has to be given as `--project .\build`.

| Option | Meaning |
|---|---|
| `--project <folder>` | The project. Default: the current folder. |
| `--library <folder>` | The library the items come from. Default: the one in the settings. With none, or a folder that is not a library, the build still runs and every item is left out with the `no_library` problem. |
| `--blender <exe>` | Use this Blender instead of the one the setup check chooses. |
| `--quiet` | Write no progress lines to standard error. |
| `--check` | List the problems without running Blender and without writing anything. |

**Progress.** Unless you give `--quiet` or `--check`, `build` writes one JSON line to standard error for each stage and each Blender step, and then one answer on standard output as for every command. A script can read standard error line by line to show a progress bar, and read standard output once at the end.

```
{"progress":{"stage":"preparing","step":"","index":0,"count":0,"message":"Checking the plan"}}
{"progress":{"stage":"blender","step":"prepare_scene","index":1,"count":15,"message":"Preparing the scene"}}
{"progress":{"stage":"blender","step":"walls","index":2,"count":15,"message":"Raising walls"}}
{"progress":{"stage":"blender","step":"openings","index":3,"count":15,"message":"Cutting openings"}}
{"progress":{"stage":"finishing","step":"","index":0,"count":0,"message":"Writing the report"}}
{"progress":{"stage":"done","step":"","index":0,"count":0,"message":"Done"}}
```

The stages are `preparing`, `blender` (one line per step, `index` counting from 1 up to `count`), `finishing` and `done`. The steps are the registered build steps, in order: `prepare_scene`, `walls`, `openings`, `bevel`, `triangulate`, `opening_fills`, `floors`, `slabs`, `stairs`, `roofs`, `assets`, `materials`, `collision`, `save_blend`, `export_fbx`, `preview_parts`. Adding a step adds a line.

**The answer.** An example, shortened. The real lists hold every step and every problem.

```json
{"ok":true,"command":"build","result":{"format":"plynth-build-report","version":1,"ok":true,"error":null,"job_id":"build-3f9a07c1","started_at":"2026-11-22T10:42:07Z","elapsed_ms":8240,"blender_version":"5.2.0","plan_fingerprint":"9c41d0e27ab35f18","outputs":{"blend_file":"D:/Plans/MyApartment/build/building.blend","fbx_file":"D:/Plans/MyApartment/build/building.fbx","log_file":"D:/Plans/MyApartment/build/blender.log"},"objects":42,"triangles":18200,"counts":{"wall_solids":1,"openings_cut":8,"fills":8,"rooms":5,"items":4},"steps":[{"step":"check","ms":14},{"step":"prepare_scene","ms":310},{"step":"walls","ms":120}],"problems":[{"code":"missing_asset","severity":"error","message":"Item i5 uses the asset chair, which is not in the library.","floor":"f1","object":"i5","step":"check"}],"error_count":1,"warning_count":0},"warnings":[{"code":"missing_asset","message":"Item i5 uses the asset chair, which is not in the library."}]}
```

- The `result` is the build report, the same text that is saved as `build\build.json` (`schema/build-report.schema.json`). Paths are absolute with forward slashes.
- The `ok` inside the report means the build ran and finished. It does not mean there were no problems. Problems with severity `error` can still be listed next to `"ok":true`, so a script that needs a clean building must also test `error_count`.
- `counts` says how many wall solids, openings, doors and windows, rooms, items, slabs, stairs, roofs and materials were made. `slabs` is the number of floors that got a slab, `stairs` the number of stairs and `roofs` the number of roof sections. A report from before floors, stairs and roofs still reads, with those three at 0. `materials` is how many catalog materials were built into the file (neutral looks are not counted). `outputs.materials_file` is the path of `materials.json`, and it is `null` when no FBX was written. A report from before materials still reads, with `materials` 0. `collision` is the number of collision meshes (`UCX_...`) made, and a report from before collision reads with `collision` 0. The collision meshes are in `building.blend` and in the export, not in `building.fbx`.

`openings_cut` and `fills` count what was built, so an opening that did not fit is not in them. `objects` and `triangles` are what went into the FBX.
- `steps` lists the time of each step in milliseconds. `check` is the work core does before Blender starts.
- `plan_fingerprint` changes when the plan or the project settings change. The app uses it to say "The plan changed since this build".
- `problems` lists core's problems first, then the ones Blender found, each in its own order. The top level `warnings` list has one entry (`code` and `message`) for every problem, whatever its severity. The `object` of a problem is a plan id: a point, wall, opening, room or item. It is `null` when the problem is not about one object.

**Problem codes.** A problem of severity `error` means the object was left out or is incomplete. A problem of severity `warning` means it was built and should be checked.

| Code | Severity | Meaning |
|---|---|---|
| `opening_does_not_fit` | error | The opening reaches past its wall or the wall's height. It is not cut. The message says how far. |
| `room_not_closed` | error | The room's walls do not close a shape, so it gets no floor and no ceiling. |
| `missing_asset` | error | The item's asset is not in the library. The item is left out. |
| `asset_broken` | error | The library lists the asset, but it is marked broken or its folder or `asset.blend` is gone. The item is left out. |
| `no_library` | error | No usable library. Every item is left out. When the plan has no items and only uses materials, it is a warning and every surface gets its neutral look. |
| `missing_material` | warning | A wall, room or project default names a material that is not in the library. The surface gets its neutral look. The `object` is the wall or room that names it, or empty for a project default. One problem for each material and object. |
| `material_broken` | warning | The material is in the library but marked broken. The surface gets its neutral look. `object` as for `missing_material`. |
| `material_failed` | warning | One image of a material would not load while building. The surface gets its neutral look. |
| `cut_failed` | error | The opening could not be cut into its wall. The wall is left as it was. |
| `fill_failed` | warning | The opening was cut, but the door or window could not be made, for example because the opening is too small for a frame. The hole stays empty. |
| `asset_load_failed` | error | Blender could not load the asset's `asset.blend`. The item is left out. |
| `bevel_failed` | warning | The wall edges could not be rounded, so they stay sharp. |
| `stair_overlaps_wall`, `stair_outside_floor`, `stair_arrival_outside_room`, `stair_no_floor_above`, `stair_type_unknown` | warning, error for the last two | The stair problems listed under `stair add`. The `object` is the stair. |
| `roof_type_fallback`, `roof_type_unknown`, `roof_section_unused` | warning | The roof problems listed under `roof types`. The `object` is the roof entry, when there is one. |
| `slab_failed`, `stair_failed`, `roof_failed` | warning | Blender could not make that slab, stair or roof from its outline. The `object` is the floor, stair or roof entry. The rest of the building is built. |
| `preview_failed` | warning | The preview parts could not be written, so `build\preview\parts.json` is removed and the 3D view loads `building.fbx`. |
| `no_room`, `outside_room`, `overlaps_item`, `overlaps_wall`, `overlaps_opening`, `off_wall`, `too_tall`, `size_changed` | warning | The furniture warnings of the item (see "Furniture items"). They are shown again here because the build marks them on the plan. |

If an asset's folder was removed or renamed by hand, the code depends on what the catalog says. While the catalog still lists the asset, it is `asset_broken`. When the catalog does not know the asset at all, it is `missing_asset`.

**`--check`.** Works out the same problems without starting Blender and without touching `build`. It is fast, so a script can run it before a build.

```
plynth build --check
```

```json
{"ok":true,"command":"build","result":{"problems":[{"code":"opening_does_not_fit","severity":"error","message":"Window o4 does not fit its wall: it reaches 0.12 m past the corner.","floor":"f1","object":"o4","step":"check"}],"error_count":1,"warning_count":0,"plan_fingerprint":"9c41d0e27ab35f18"}}
```

Blender can find more problems while it builds (`cut_failed`, `fill_failed`, `asset_load_failed`, `bevel_failed`, `material_failed`, `slab_failed`, `stair_failed`, `roof_failed`, `preview_failed`), so a clean `--check` does not promise a clean build.

**Refusals.** All have exit code 1 and write nothing, except where said.

| Code | When |
|---|---|
| `not_a_project` | The folder has no `project.json`. |
| `invalid_value` | The plan has no walls: "The plan has no walls to build." |
| `blender_not_found` | No usable Blender. Run `plynth doctor`. |
| `in_use` | Another build of this project is running. |
| `blender_failed` | Blender failed. The message names `build\blender.log`. `build\build.json` is written with `"ok":false` and an `error` with the `code` and the worker's own code, such as `save_failed` or `export_failed`. The files of the last good build stay in place. |

**Files.** A build writes into `<project>\build\`:

| File | What it is |
|---|---|
| `building.blend` | The Blender file. Open it in Blender, edit it, render it. Items are instances of their asset. |
| `building.fbx` | The whole building in one file. Items are real objects that share their mesh. |
| `preview\` | One FBX for each floor and one for each floor's roofs (`f1.fbx`, `f2_roof.fbx`), and `parts.json` that lists them (`schema/build-preview.schema.json`). The 3D view loads these so it can hide roofs and floors. Written after `building.fbx`. Not written, and `parts.json` removed, when it fails. |
| `materials.json` | Written with the FBX, never without it. For each built material, its image maps (absolute paths) and alpha mode, and for each built object its material slots in order (`schema/build-materials.schema.json`). The 3D view reads it to show metal, roughness and see-through parts. |
| `textures\` | Small constant images for surfaces with no material, such as `Wall_Outside_base_color.png`. |
| `build.json` | The report of the last build. |
| `job.json`, `result.json`, `blender.log` | What was sent to Blender, what came back, and what Blender printed. |
| `build.lock` | Exists only while a build runs. |

Blender writes the two model files under a temporary name and renames them at the end, so a build that fails or is stopped leaves the last good ones untouched. `build\` can be deleted at any time.

Two builds of one project at the same time are not allowed: the second gets `in_use`. A build that was killed leaves a `build.lock` behind. The next build takes it over when the process in it has ended or the lock is older than 30 minutes.

`plynth build` has no stop signal of its own. Ctrl+C ends the process at once, and the lock file is left for the next build to take over by the process check or the timeout. The app's Cancel button, which does stop a build cleanly, is the way to stop one from the window.

### export

Exports the last build for Unreal: runs the quality checks, then writes `export\<Name>.fbx`, `export\Textures\` and `export\export_report.json`. The app's Export tab does the same thing, so both give the same result. `export` never builds. It uses `build\building.blend` as it is, hand edits included, and it never changes the plan, the project settings or `building.blend`. It runs while the app has the project open, with no edit session.

```
plynth export
plynth export --check
plynth export --force
plynth export --project D:\Plans\MyApartment --quiet
plynth export report
```

| Option | Meaning |
|---|---|
| `--project <folder>` | The project. Default: the current folder. |
| `--blender <exe>` | Use this Blender instead of the one the setup check chooses. |
| `--check` | Run the quality checks and write no FBX and no textures. The report is still written. |
| `--force` | Write the files even when a check failed. The report records it. |
| `--quiet` | Write no progress lines to standard error. |

`<Name>` is the project name with spaces and odd characters turned into `_`, for example `My_Flat_2`. The FBX holds only relative texture paths, so the `export` folder can be moved. `Textures` holds only the textures the FBX uses, named `T_<Material>_<Channel>`. Materials are named by slot, for example `Living_Floor` and `Exterior_Wall`. Collision shapes are meshes named `UCX_...`. See "Exporting for Unreal", "Collision" and "Material slot names" in the [User guide](USER_GUIDE.md).

**Progress** is written to standard error as for `build`, with the stages `preparing`, `blender`, `finishing` and `done`. The Blender steps are `open_blend`, `prepare`, `name_objects`, `textures`, `checks`, `write_fbx` and `file_checks`.

**The answer** is the export report, the same text that is saved as `export\export_report.json` (`schema/export-report.schema.json`). An example, shortened:

```json
{"ok":true,"command":"export","result":{"format":"plynth-export-report","version":1,"ok":true,"error":null,"exported":true,"checked_only":false,"forced":false,"overridden":[],"job_id":"export-3f9a07c1","elapsed_ms":9100,"exporter":"single","objects":58,"triangles":21400,"collision_objects":31,"outputs":{"files":[{"group":"building","file":"MyApartment.fbx","objects":58,"triangles":21400}],"textures_folder":"D:/Plans/MyApartment/export/Textures","texture_count":14,"report_file":"D:/Plans/MyApartment/export/export_report.json","log_file":"D:/Plans/MyApartment/build/export_work/blender.log"},"quality":{"passed":true,"fail_count":0,"warning_count":0,"checks":[{"id":"naming","name":"Naming","phase":"scene","status":"pass","message":"All names are valid.","findings":[]}]}},"warnings":[]}
```

- `exported` is true when the files were written. `checked_only` is true for `--check`. `forced` is true when `--force` was used, and `overridden` lists the ids of the failed checks it went past.
- `quality.checks` has one entry for each check, in the order they ran. `status` is `pass`, `warning`, `fail` or `skipped`. `findings` holds at most 50 entries, and each one may name a plan id in `plan_id`.
- The eleven checks are `build_current`, `build_complete`, `scale`, `transforms`, `naming`, `uvs`, `texel_density`, `triangles`, `materials`, `collision` and `fbx_scale`. They are explained in "The quality gate" in the [User guide](USER_GUIDE.md).
- With `--check`, a failed check does not make the command fail. The answer is ok, and the top level `warnings` list has one entry with the code `quality_failed` for each failed check, for example "The check 'naming' failed." A script can read it to know what to fix.
- A blocked export replaces nothing. The files of the last good export stay, and the report says `exported` is false.

| Code | When |
|---|---|
| `not_a_project` | The folder has no `project.json`. |
| `not_found` | There is no `build\building.blend`. The message is "Build the project first." |
| `invalid_value` | The project's export mode is grouped. The message is "Grouped export is not available yet." This stays until grouped export is added. |
| `in_use` | A build or another export of the project is running. |
| `blender_not_found` | No usable Blender. |
| `blender_failed` | The worker failed. The message names the log file. |
| `quality_failed` | A check failed and `--force` was not given. Exit 1. The message names the failed checks, for example "The quality gate failed: naming, collision. See D:/Plans/MyApartment/export/export_report.json, or export with --force." The report is written. |
| `cancelled` | The export was stopped. Nothing is kept. |

#### export report

Answers with the last report of the project, read from `export\export_report.json`, in the same shape. It is `not_found` when the project has never been exported or checked. It takes only `--project`.

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

## Machine settings, the library and the assets catalog

These commands are about your computer, not about one project. They never open a project, so they do not take `--project` and the edit lock never refuses them. They work while the app has a project open.

All of them read the machine settings file. `settings show` tells you where it is. By default it is `%APPDATA%\Plynth\settings.json`. Set the environment variable `PLYNTH_CONFIG_DIR` to a folder to use another settings file, which is how scripts and tests keep away from the real one. `PLYNTH_BLENDER` names a `blender.exe` (or the folder that holds it).

**Which Blender is used.** Plynth takes the first of these that is set: the `--blender <exe>` option, the `PLYNTH_BLENDER` variable (when not empty), the Blender saved in the settings, and last the search of the usual install places. A Blender you name with the option or the variable is the only one that is started. If it is missing or cannot be used, the command says so and stops. It does not fall back to another install.

Commands that use the library take `--library <folder>` to use another library than the one in the settings. Commands that run Blender (`assets import`, `assets inbox`, `assets rescan`, `assets thumbnail`, `library rescan`) take `--blender <exe>` to use another Blender than the one the setup check chooses. Ids (`chair`, `living`) use lower case letters, digits and `_`, start with a letter, and are at most 64 characters. Tags use lower case letters, digits, `_` and `-`.

The examples below ran against a fresh library in `D:\Demo`, with the sample chair from `samples\models\chair`.

### doctor

Checks the machine: Blender, the library and projects folders, the graphics driver and the optional add-ons. It only looks. It installs nothing, downloads nothing and changes no setting. The same check is the Setup check screen in the app, and [Setting up Plynth](SETUP.md) explains each item.

```
plynth doctor
plynth doctor --no-addons
plynth doctor --blender "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
```

Options:

- `--no-addons`: skip the add-on probe, so your saved preferences are not read. The add-on items then say `not_checked`, and `probed` is false. This is faster. Blender is still started briefly with `--version` to read its version.
- `--blender <file>`: check this `blender.exe` and no other. It beats `PLYNTH_BLENDER` and the saved setting. Nothing is saved.

Without `--no-addons`, doctor starts Blender once in the background, which takes a few seconds. That run uses Blender's factory settings and a private folder of Plynth's, so it does not load your add-ons or touch your Blender folders. To tell you which add-ons are on and whether BlenderKit has a sign-in, the check reads your saved Blender preferences file (`userpref.blend`) as plain data, and only reads it. When BlenderKit is on and that file holds no key, it may also read BlenderKit's `config\preferences.json`, only to see whether a sign in exists. It never starts an add-on, never changes or copies the file, and never shows the key. It only learns whether a key exists. If the preferences file cannot be understood (for example a Blender version Plynth does not know), the add-on items say `not_checked`. The answer always has `ok` true and exit code 0, even when something is missing. A script reads `ready`, which is true when no required item is missing or too old.

```json
{"ok":true,"command":"doctor","result":{"ready":true,"first_run":false,"updated":true,"probed":false,"items":[{"id":"blender","name":"Blender","required":true,"state":"found","version":"5.2.0","path":"C:/Program Files/Blender Foundation/Blender 5.2/blender.exe","detail":"Blender 5.2.0 LTS (hash fbe6228777e7 built 2026-07-14 01:35:40) found.","action":"","link":"","affects":[]},{"id":"library_folder","name":"Library folder","required":true,"state":"found","version":null,"path":"D:/Demo/Library","detail":"The library is ready.","action":"","link":"","affects":[]},{"id":"graphics_driver","name":"Graphics driver","required":true,"state":"not_checked","version":null,"path":null,"detail":"Checked by the app when it starts the 3D preview.","action":"","link":"","affects":[]},{"id":"addon_blenderkit","name":"BlenderKit add-on","required":false,"state":"not_checked","version":null,"path":null,"detail":"Add-ons were not checked this time.","action":"","link":"https://www.blenderkit.com/","affects":["importing assets from BlenderKit"]}],"blender_installs":[{"path":"C:/Program Files/Blender Foundation/Blender 5.2/blender.exe","version":"5.2.0","version_text":"Blender 5.2.0 LTS (hash fbe6228777e7 built 2026-07-14 01:35:40)","support":"supported","sources":["program_folder","registry"],"selected":true,"problem":null}]}}
```

The example is shortened. The real answer lists every item, in this order: `blender`, `blender_version`, `library_folder`, `projects_folder`, `graphics_driver`, then the add-ons `addon_blenderkit`, `addon_archimesh`, `addon_home_builder`, `addon_bool_tool`, `addon_asset_optimizer`, `addon_bagapie`.

Each item has:

| Field | Meaning |
|---|---|
| `id`, `name` | Which item it is. |
| `required` | True when the item blocks importing now and Build and Export later. The add-ons are never required. |
| `state` | `found`, `missing`, `too_old`, `needs_attention` or `not_checked`. |
| `version`, `path` | What was found. `null` when unknown. |
| `detail` | One sentence on what was found. |
| `action` | One sentence on what to do. Empty when nothing is needed. |
| `link` | An official page, or an empty string. |
| `affects` | For add-ons, what stays switched off without it. |

`first_run` is true when no settings file exists yet. `updated` is true when the last passing setup check was made by another Plynth version. `blender_installs` lists every Blender found, newest first, with `support` (`supported`, `too_old` or `untested`) and where it was found. The shape is in `schema/setup-report.schema.json`.

### settings show

Shows the machine settings and where the file is.

```
plynth settings show
```

```json
{"ok":true,"command":"settings show","result":{"file":"D:/Demo/config/settings.json","exists":true,"settings":{"library":"D:/Demo/Library","projects":"D:/Demo/Projects","blender":null,"keep_imported_zips":false,"triangle_warning":500000,"setup_checked_version":null,"recent_projects":[]}}}
```

`recent_projects` is the list `project recent` prints, with its own `path`, `name` and `last_opened`. `exists` is false until something has been saved, and the settings then show their defaults. `library`, `projects` and `blender` are `null` when not set. `blender` null means Plynth uses the newest supported Blender it finds.

### settings set

Changes one or more settings. Each option is checked before anything is saved. If one is refused, nothing is saved.

```
plynth settings set --projects D:\Demo\Projects
plynth settings set --keep-zips true --triangle-warning 800000
plynth settings set --blender none
```

Options (give at least one):

- `--library <folder|none>`: the library folder. It must already be a library, so run `library init` first. `none` clears it.
- `--projects <folder|none>`: where new projects go and where the built-in terminal starts. The folder must exist.
- `--blender <file|none>`: the `blender.exe` to use. It must run and be version 4.2 or newer. `none` goes back to the newest one found.
- `--keep-zips <true|false>`: keep a zip after its asset was imported instead of moving it to the Recycle Bin.
- `--triangle-warning <n>`: imports with more triangles than this get the `high_polycount` warning.

The answer is the same as `settings show`. A library folder that is not a library gives `no_library`. A projects folder that does not exist gives `not_found`. A Blender that is missing or too old gives `blender_not_found`.

There is no thumbnail engine setting any more. Thumbnails are always drawn with Eevee, and `--thumbnail-engine` is refused as an unknown option (`bad_arguments`). A settings file from an older Plynth that still has `thumbnail_engine` is read, and saved again as version 3 without it.

```json
{"ok":false,"command":"settings set","error":{"code":"blender_not_found","message":"There is no program at C:/nope/blender.exe."}}
```

### library init

Makes a folder into a library: it creates the folders and the two catalog databases, and fills the lists of room types, categories and licenses. It never removes or changes anything already there, so it is safe to run on an existing library.

```
plynth library init D:\Demo\Library --use
```

Option: `--use` also saves the folder as the library in the settings.

```json
{"ok":true,"command":"library init","result":{"root":"D:/Demo/Library","free_bytes":440542654464,"assets":{"db":"D:/Demo/Library/assets/assets.db","items":0,"uncategorized":0,"inbox_waiting":0},"materials":{"db":"D:/Demo/Library/materials/materials.db","items":0},"shapes":[]}}
```

### library add-sample-shapes

Copies the sample opening shapes, such as `keyhole.shape.json`, into the library's `shapes` folder. `library init` already does this for a new library, so this command is for a library made before. A file that is already there is never replaced.

```
plynth library add-sample-shapes
plynth library add-sample-shapes --from D:\MyShapes
```

Options: `--library <folder>`, and `--from <folder>` for another folder of `.shape.json` files. The default is the `samples\shapes` folder next to the program.

```json
{"ok":true,"command":"library add-sample-shapes","result":{"folder":"D:/Demo/Library/shapes","copied":["keyhole.shape.json"],"skipped":[]}}
```

`copied` lists the files added and `skipped` the ones already there. A `--from` folder that does not exist is `not_found`. A shape added this way is listed by `opening shapes` the next time a command runs.

### library info

Describes the library: its folder, free space, how many assets it holds, and the custom opening shapes found in `shapes/`.

```
plynth library info
```

The answer has the same shape as `library init`. `uncategorized` counts assets with no category. `inbox_waiting` counts files waiting in the inbox. `shapes` lists the ids of custom opening shapes loaded from `Library\shapes\`.

Option: `--library <folder>`.

### library rescan

Rebuilds the asset index from the folders in `assets\items\`, then measures again every asset whose `asset.blend` changed. It is the same as `assets rescan` with the answer wrapped in `assets`. Add `--no-blender` to list the changed assets without starting Blender.

```
plynth library rescan --no-blender
```

```json
{"ok":true,"command":"library rescan","result":{"assets":{"added":4,"updated":0,"removed":0,"unchanged":0,"needs_refresh":[],"refreshed":[],"refresh_failed":[],"broken":[],"inbox_waiting":0}}}
```

That answer is from a library whose `assets.db` was deleted by hand: all four assets came back from their folders. The room types you added and the categories you made yourself are kept only in `assets.db`, so they went back to the built-in lists (plus any category an `info.json` names).

### assets import

Imports a model file, a zip or a folder into the library. Blender runs in the background, cleans the model, saves it as `asset.blend`, measures it and renders a thumbnail. This takes a few seconds.

```
plynth assets import D:\Demo\in\chair.obj --category chair --tag oak
plynth assets import D:\Demo\in\chair.zip --name "Zip chair" --keep-original
plynth assets import D:\Demo\in\chair.obj --scale 0.01
```

Options:

- `<path>`: the file, zip or folder. Files ending `.blend`, `.fbx`, `.glb`, `.gltf`, `.obj` and `.zip` are read. A folder must hold one model. `assets importers` lists the endings.
- `--id <id>`: the asset id. Default: made from the file name (`Red Chair (2).obj` becomes `red_chair_2`). A taken default gets `_2`, `_3` and so on. An id you give that is taken is `already_exists`.
- `--name <text>`: the name. Default: made from the file name.
- `--category <id>`: a category. Repeat for several. An unknown one is `not_found`, and nothing is imported.
- `--tag <tag>`: a tag. Repeat for several.
- `--license <id>`: a license from `assets licenses`. Default `unknown`.
- `--scale <number>`: multiply the size of the model. Use it when the file has other units, for example `--scale 0.01` for a model drawn in centimetres.
- `--rotate <0|90|180|270>`: turn the model about the vertical axis. Use it when the front of the model does not face the way Plynth expects (see the user guide).
- `--keep-original`: keep a zip after the import. Without it, an imported zip goes to the Windows Recycle Bin. Files and folders you point at are never touched, only zips.

```json
{"ok":true,"command":"assets import","result":{"asset":{"id":"chair","name":"Chair","categories":["chair"],"tags":["oak"],"license":"unknown","source":{"kind":"local_file","reference":"chair.obj","imported_at":"2026-11-21T16:40:56Z"},"size":{"width":0.45,"depth":0.5,"height":0.9},"triangles":72,"state":"ready","problem":null,"thumbnail":{"state":"ok","file":"D:/Demo/Library/assets/items/chair/thumb.png","error":null},"folder":"D:/Demo/Library/assets/items/chair"},"importer":"local_file","thumbnail_failed":false,"original_removed":false}}
```

The `asset` is an asset entry (below). `importer` is the importer that was used. `thumbnail_failed` is true when the render failed: the asset is still imported, with `thumbnail.state` `failed` and the reason in `thumbnail.error`. `original_removed` is true when a zip was moved to the Recycle Bin.

Problems with the model are warnings in the top level `warnings` list. The import still works. This one is from `--scale 0.01`:

```json
"warnings":[{"code":"scale_suspicious","message":"The largest side is 0.0090 m, which is very small. The file may use other units. Import with a scale (for example --scale 100)."}]
```

Cleanup warning codes: `high_polycount` (more triangles than the limit in the settings), `scale_suspicious` (largest side above 30 m or below 0.02 m), `missing_texture` (a texture file could not be found), `removed_objects` (cameras, lights or empties were removed), `unsupported_object`, `fbx_failed` (`asset.fbx`, which the app uses for the live 3D preview of the asset, could not be written; the asset itself is fine).

Refusals: an ending no importer reads is `invalid_value` and names the endings. A rotation that is not 0, 90, 180 or 270 is `invalid_value`. A file that Blender cannot turn into a model is `blender_failed` with the reason. No Blender is `blender_not_found`. No library is `no_library`. Nothing is left in the library after a failed import.

```json
{"ok":false,"command":"assets import","error":{"code":"invalid_value","message":"The rotation must be 0, 90, 180 or 270 degrees."}}
```

### assets inbox

Imports everything waiting in the library's `assets\inbox\` folder. The app does this by itself while it runs. Use the command when the app is closed.

```
plynth assets inbox
```

```json
{"ok":true,"command":"assets inbox","result":{"imported":["office_chair"],"failed":[{"entry":"broken.obj","code":"blender_failed","message":"The file holds no model with geometry.\nThe work folder was kept for the log: D:/Demo/Library/assets/.staging/broken-2ce374cb"}],"remaining":0}}
```

The answer is ok even when some entries failed. An imported zip goes to the Recycle Bin, or to `inbox\done\` when `keep_imported_zips` is on (as it was for this run). Other imported files and folders move to `inbox\done\`. A failed entry moves to `inbox\failed\` together with a `<name>.error.txt` that holds the reason. An entry never stays in the inbox, so it is never imported twice. `remaining` counts entries still waiting.

### assets search

Searches the assets by words and filters. Every word must match the start of a name, id, tag or category, so `cha` finds "Chair" and "Armchair". Accents are ignored.

```
plynth assets search cha
plynth assets search --room gaming
plynth assets search --tag oak --tag wood --license cc0
plynth assets search --uncategorized --limit 20
```

Options:

- `<text>`: words to find. Optional.
- `--room <id>`: only assets in a category of this room type.
- `--category <id>`: only assets in this category.
- `--tag <tag>`: only assets with this tag. Repeat to require several.
- `--license <id>`: only assets with this license.
- `--uncategorized`: only assets with no category.
- `--limit <n>`: at most this many.

```json
{"ok":true,"command":"assets search","result":{"assets":[{"id":"chair","name":"Chair","categories":["chair"],"tags":["oak"],"license":"unknown","source":{"kind":"local_file","reference":"chair.obj","imported_at":"2026-11-21T16:40:56Z"},"size":{"width":0.45,"depth":0.5,"height":0.9},"triangles":72,"state":"ready","problem":null,"thumbnail":{"state":"ok","file":"D:/Demo/Library/assets/items/chair/thumb.png","error":null},"folder":"D:/Demo/Library/assets/items/chair"}],"count":1}}
```

An unknown `--room` is `not_found`.

```json
{"ok":false,"command":"assets search","error":{"code":"not_found","message":"There is no room type nope."}}
```

#### The asset entry

Every asset in an answer has these fields:

| Field | Meaning |
|---|---|
| `id`, `name` | The id used by commands, and the name shown to people. |
| `categories` | Category ids. Empty means uncategorized. |
| `tags` | Sorted tags. |
| `license` | A license id. |
| `source` | `kind` (`local_file`), `reference` (the original file name) and `imported_at` (UTC). |
| `size` | `width` (X), `depth` (Y) and `height` (Z) in metres. |
| `measured` | True when the size is a real measurement. False for an asset that has not been measured yet, whose size is 0. Such an asset cannot be placed. `assets thumbnail` measures it again. |
| `triangles` | Triangle count. |
| `state` | `ready`, `needs_refresh` (the `.blend` changed and has not been measured again) or `broken` (see `problem`). |
| `problem` | Why the asset is broken, otherwise `null`. |
| `thumbnail` | `state` (`ok`, `failed` or `missing`), `file` (the picture, when `ok`) and `error`. |
| `folder` | The asset's folder. |

### assets list

The same as `assets search` without the words. It takes the same filters.

```
plynth assets list --category chair
```

### assets info

Shows one asset with more detail: the entry above plus the number of objects, the materials, the textures, the warnings from the last cleanup and the Blender version that cleaned it.

```
plynth assets info chair_2
```

```json
{"ok":true,"command":"assets info","result":{"id":"chair_2","name":"Tiny chair","categories":[],"tags":["small"],"license":"own_work","source":{"kind":"local_file","reference":"chair.obj","imported_at":"2026-11-21T16:40:58Z"},"size":{"width":0.0045,"depth":0.005,"height":0.009},"triangles":72,"state":"ready","problem":null,"thumbnail":{"state":"ok","file":"D:/Demo/Library/assets/items/chair_2/thumb.png","error":null},"folder":"D:/Demo/Library/assets/items/chair_2","objects":6,"materials":["Fabric","Wood"],"textures":["textures/chair_wood.png"],"warnings":[{"code":"scale_suspicious","message":"The largest side is 0.0090 m, which is very small. The file may use other units. Import with a scale (for example --scale 100)."}],"blender_version":"5.2.0"}}
```

An unknown id is `not_found`.

### assets set

Changes the name, categories, tags or license of an asset. Give at least one option. The change is written to the asset's `info.json` first and then to the index.

```
plynth assets set chair_2 --name "Tiny chair" --license own_work --add-tag small
plynth assets set chair --categories chair,desk --tags ""
```

Options:

- `--name <text>`: a new name.
- `--categories <a,b>`: replace the categories. An empty string clears them.
- `--add-category <id>`, `--remove-category <id>`: change one category. Repeatable.
- `--tags <a,b>`: replace the tags. An empty string clears them.
- `--add-tag <tag>`, `--remove-tag <tag>`: change one tag. Repeatable.
- `--license <id>`: a license id.

The answer is the asset entry. An unknown category or license is `not_found` and nothing changes.

### assets remove

Deletes an asset and its whole folder. It cannot be undone, so `--yes` is required.

```
plynth assets remove chair_2 --yes
```

```json
{"ok":true,"command":"assets remove","result":{"removed":"chair_2"}}
```

Without `--yes` the answer is `bad_arguments` and nothing is deleted:

```json
{"ok":false,"command":"assets remove","error":{"code":"bad_arguments","message":"Removing an asset deletes its folder and cannot be undone. Add --yes to confirm."}}
```

### assets rescan

Rebuilds the asset index from the folders in `items\`, then measures again each asset whose `asset.blend` changed and renders its thumbnail. A folder with only an `asset.blend` gets a new `info.json` and is uncategorized. A folder with a damaged `info.json` is listed in `broken` and the others still load. Folders whose name starts with a dot are skipped.

```
plynth assets rescan
plynth assets rescan --no-blender
```

With `--no-blender` nothing is measured: changed assets are only listed in `needs_refresh`.

```json
{"ok":true,"command":"assets rescan","result":{"added":0,"updated":1,"removed":0,"unchanged":3,"needs_refresh":["chair"],"refreshed":[],"refresh_failed":[],"broken":[],"inbox_waiting":0}}
```

`added`, `updated`, `removed` and `unchanged` count asset rows. `needs_refresh` lists the assets whose `asset.blend` changed. `refreshed` lists the ones measured again. `refresh_failed` lists the ones that could not be, each with `id`, `code` and `message`. `broken` lists the folders that could not be read, each with `folder`, `code` and `message`. `inbox_waiting` counts entries in the inbox.

### assets thumbnail

Measures an asset again and renders a new thumbnail. Use it when a render failed or after you changed `asset.blend` in Blender.

```
plynth assets thumbnail chair
```

The answer is the asset entry after the refresh.

### assets rooms

Lists the room types of the library, with the categories that belong in each.

```
plynth assets rooms
```

```json
{"ok":true,"command":"assets rooms","result":{"room_types":[{"id":"living","name":"Living room","built_in":true,"categories":["sofa","armchair","table","shelf","tv","lamp","carpet","curtain","plant","decor"]},{"id":"other","name":"Other","built_in":true,"categories":[]}]}}
```

The example is shortened. There are nine built-in room types: `living`, `bedroom`, `kitchen`, `bathroom`, `hall`, `dining`, `office`, `storage` and `other`. This list is also the one `room types`, `room add --type` and `room set --type` use once a library is set.

### assets room add

Adds a room type. `--name` is required.

```
plynth assets room add gaming --name "Gaming room"
```

```json
{"ok":true,"command":"assets room add","result":{"id":"gaming","name":"Gaming room","built_in":false,"categories":[]}}
```

### assets room set

Renames a room type, replaces its categories, or moves it in the list. Give at least one option.

```
plynth assets room set gaming --categories desk,chair,tv,lamp
```

Options: `--name <text>`, `--categories <a,b>` (replaces the list), `--position <n>` (from 0).

```json
{"ok":true,"command":"assets room set","result":{"id":"gaming","name":"Gaming room","built_in":false,"categories":["chair","desk","tv","lamp"]}}
```

An unknown category is `not_found`.

### assets room remove

Removes a room type from the list. Plans that already use it keep its id, show the id as the label, and `room set` refuses it from then on.

```
plynth assets room remove gaming
```

```json
{"ok":true,"command":"assets room remove","result":{"removed":"gaming"}}
```

### assets categories

Lists every category with how many assets hold it. There are 24 built-in categories, from `sofa` to `window`. `door` and `window` belong to no room type, because they are for opening frames in a later release.

```
plynth assets categories
```

```json
{"ok":true,"command":"assets categories","result":{"categories":[{"id":"sofa","name":"Sofa","built_in":true,"assets":0},{"id":"chair","name":"Chair","built_in":true,"assets":1}]}}
```

The example is shortened.

### assets category add

Adds a category. `--name` is required.

```
plynth assets category add rug --name "Rug"
```

```json
{"ok":true,"command":"assets category add","result":{"id":"rug","name":"Rug","built_in":false,"assets":0}}
```

### assets category set

Renames a category. The id does not change, so assets keep it.

```
plynth assets category set rug --name "Floor rug"
```

```json
{"ok":true,"command":"assets category set","result":{"id":"rug","name":"Floor rug","built_in":false,"assets":0}}
```

### assets category remove

Removes a category from the list, from every room type, and from every asset that has it. It lists the assets it changed.

```
plynth assets category remove rug
```

```json
{"ok":true,"command":"assets category remove","result":{"removed":"rug","assets_changed":[]}}
```

### assets licenses

Lists the licenses an asset can have. `redistributable` says whether a model with that license may be shared with a project.

```
plynth assets licenses
```

```json
{"ok":true,"command":"assets licenses","result":{"licenses":[{"id":"unknown","name":"Unknown","url":"","redistributable":false},{"id":"cc0","name":"CC0 (public domain)","url":"https://creativecommons.org/publicdomain/zero/1.0/","redistributable":true},{"id":"cc_by_4","name":"CC BY 4.0","url":"https://creativecommons.org/licenses/by/4.0/","redistributable":true},{"id":"royalty_free","name":"Royalty free","url":"","redistributable":false},{"id":"personal_use","name":"Personal use only","url":"","redistributable":false},{"id":"own_work","name":"Own work","url":"","redistributable":true}]}}
```

### assets importers

Lists the ways an asset can be imported. Today there is one, for your own files.

```
plynth assets importers
```

```json
{"ok":true,"command":"assets importers","result":{"importers":[{"id":"local_file","name":"Own files","description":"Model files (.blend, .fbx, .obj, .gltf, .glb), zip files and folders that hold a model.","suffixes":[".blend",".fbx",".glb",".gltf",".obj",".zip"]}]}}
```

## The materials catalog

The library's second catalog. Its commands mirror the `assets` ones, with `--group` where assets have `--category`. Every command takes `--library <folder>` (default: the one in the settings) and is `no_library` when there is none. An id is lower case letters, digits and `_`, starting with a letter.

### materials import

Imports texture folders, zips, image files or `.blend` files as materials. Blender runs in the background, makes every image map, checks them and renders a thumbnail. One material takes a few seconds.

```
plynth materials import D:\Textures\oak --group floor --tile 2x2
plynth materials import D:\Textures\brick.zip --id red_brick --keep
plynth materials import samples\materials\*
plynth materials import samples\materials\* --skip-existing
plynth materials import D:\Textures\tile --map base_color=tile_a.png --value metallic=0
plynth materials import D:\Models\chair\asset.blend --material Wood --id chair_wood --bake-size 2048
```

Options:

- `<path>`: a folder, zip, image file or `.blend`. Repeat for several. A path whose last part has `*` or `?` is expanded by Plynth, because PowerShell does not expand wildcards for programs. The match is sorted by name and holds folders and files some importer can read, so a README beside the materials is left out. A pattern that matches nothing is `not_found`.
- `--id <id>`: the material id, for one path only (two paths with it is `bad_arguments`). Default: the `id` of the `material.json`, otherwise made from the name. A taken default gets `_2`, `_3` and so on. An id you give that is taken is `already_exists`.
- `--name <text>`: the name. Default: `material.json`, or made from the file name.
- `--group <id>`: a group. Repeat for several. An unknown one is `not_found`.
- `--tag <tag>`: a tag. Repeat for several.
- `--license <id>`: a license from `materials licenses`.
- `--tile <WxH>`: the real size of one tile in metres, `1.2x0.6`, or one number for a square. From 0.01 to 100. Anything else, such as `0x1`, is `invalid_value`.
- `--material <name>`: which material to take from a `.blend`. Needed when it holds several.
- `--map <channel=file>`: use this image for a channel. Repeat. The channels are `base_color`, `normal`, `roughness`, `metallic`, `opacity` and `occlusion`. It beats the file names and `material.json`.
- `--value <channel=number>`: use a number from 0 to 1 for roughness, metallic, opacity or occlusion. Repeat. It beats `material.json`.
- `--normal <auto|opengl|directx>`: the convention of the normal map. `auto` reads `gl` or `dx` from the file name and takes OpenGL when neither is there. DirectX maps get their green channel flipped. Maps are always stored as OpenGL.
- `--bake-size <pixels>`: the size of images drawn from a `.blend` material that is not plain image textures. Default 1024. A power of two from 256 to 4096.
- `--keep`: keep a zip after the import. Without it the imported zip goes to the Windows Recycle Bin. Files and folders you point at are never touched.
- `--skip-existing`: when the material's id is taken, skip it instead of making a new id.
- `--blender <exe>`: use this Blender.
- `--quiet`: no progress lines.

Paths are imported in order. The first failure stops the run, and its message ends with how many were imported before it, such as "2 materials were imported before it." Progress is JSON lines on standard error, one per Blender step, with the `source` named, and the answer is one object on standard output.

```json
{"ok":true,"command":"materials import","result":{"imported":[{"id":"oak_planks","importer":"texture_set","skipped":false,"warnings":[],"thumbnail_failed":false,"original_removed":false}]}}
```

`importer` is `texture_set` or `blend_material`. `skipped` is true when `--skip-existing` found the id taken, and nothing was done. `thumbnail_failed` is true when the render failed. The material is still imported, with its thumbnail state `failed`. Problems with the maps are in `warnings` and again in the top level `warnings` list: `ambiguous_map`, `unused_image`, `map_size_mismatch`, `visible_seams`, `low_resolution` and `picked_material`.

Refusals: `no_library`, `not_found` for a path or group that is not there, `invalid_value` for a bad `--tile`, `--map`, `--value` or `--normal` and for an ending no importer reads, `already_exists`, `blender_not_found`, and `blender_failed` with the reason when Blender cannot make the material, such as "no base color" (a set with no base color image or color), "several materials" (a `.blend` with more than one, listing them, when `--material` is not given) or a failed bake. Nothing is left in the library after a failed import.

### materials inbox

Imports everything waiting in `Library\materials\inbox\`. The app does this by itself while it runs.

```
plynth materials inbox
```

```json
{"ok":true,"command":"materials inbox","result":{"imported":["red_brick_2"],"failed":[],"remaining":0}}
```

The answer is ok even when some entries failed, and each failure is `{"entry","code","message"}`. Imported entries move to `inbox\done\`, failed ones to `inbox\failed\` with a `<name>.error.txt`. Options: `--blender`, `--quiet`.

### materials search

Searches the materials. Every word must match the start of a name, id, tag or group.

```
plynth materials search brick
plynth materials search --group floor --tag wood
plynth materials search --uncategorized --limit 20
```

Options: `<text>`, `--group <id>`, `--tag <tag>` (repeat to require several), `--license <id>`, `--uncategorized`, `--limit <n>`. An unknown group or license is `not_found`.

```json
{"ok":true,"command":"materials search","result":{"materials":[{"id":"red_brick","name":"Red brick","groups":["wall","exterior"],"tags":["brick"],"license":"own_work","source":{"kind":"texture_set","reference":"red_brick","imported_at":"2026-11-22T09:12:31Z"},"tile_size":{"width":1.2,"height":0.6},"state":"ready","problem":null,"thumbnail":{"state":"ok","file":"D:/Demo/Library/materials/items/red_brick/thumb.png","error":null},"folder":"D:/Demo/Library/materials/items/red_brick"}],"count":1}}
```

#### The material entry

| Field | Meaning |
|---|---|
| `id`, `name` | The id used by commands and plans, and the name shown to people. |
| `groups` | Group ids. Empty means uncategorized. |
| `tags` | Sorted tags. |
| `license` | A license id. |
| `source` | `kind` (`texture_set` or `blend_material`), `reference` (the original folder or file name) and `imported_at` (UTC). |
| `tile_size` | `width` and `height` of one tile in metres. |
| `state` | `ready`, `needs_refresh` (a map or the tile size changed and it has not been cleaned again) or `broken` (see `problem`). |
| `problem` | Why the material is broken, otherwise `null`. |
| `thumbnail` | `state` (`ok`, `failed` or `missing`), `file` and `error`. |
| `folder` | The material's folder. |

### materials list

The same as `materials search` without the words. It takes the same filters.

### materials info

Shows one material in full: the entry plus the maps, the texture size and the warnings of the last cleaning.

```
plynth materials info oak_planks
```

```json
{"ok":true,"command":"materials info","result":{"id":"oak_planks","name":"Oak planks","groups":["floor"],"tags":["wood","oak"],"license":"own_work","source":{"kind":"texture_set","reference":"oak_planks","imported_at":"2026-11-22T09:12:40Z"},"tile_size":{"width":2.0,"height":2.0},"state":"ready","problem":null,"thumbnail":{"state":"ok","file":"D:/Demo/Library/materials/items/oak_planks/thumb.png","error":null},"folder":"D:/Demo/Library/materials/items/oak_planks","maps":{"base_color":{"file":"D:/Demo/Library/materials/items/oak_planks/textures/base_color.jpg","from":"file","detail":"oak_Color.jpg"},"normal":{"file":"D:/Demo/Library/materials/items/oak_planks/textures/normal.png","from":"file","detail":"NormalDX.png"},"roughness":{"file":"D:/Demo/Library/materials/items/oak_planks/textures/roughness.png","from":"file","detail":"oak_Roughness.png"},"metallic":{"file":"D:/Demo/Library/materials/items/oak_planks/textures/metallic.png","from":"value","detail":"0"},"opacity":{"file":null,"from":"none","detail":null},"occlusion":{"file":null,"from":"none","detail":null}},"texture_size":{"width":1024,"height":1024},"warnings":[]}}
```

Each map has the absolute `file` (or `null`), `from` (`file`, `value`, `baked` or `none`) and `detail` (the source file name, the value, or `null`). An unknown id is `not_found`.

### materials set

Changes the name, groups, tags, license or tile size of a material. Give at least one option.

```
plynth materials set red_brick --tile 1.2x0.6 --group wall --group exterior
plynth materials set oak_planks --name "Oak boards" --tag wood --tag oak
plynth materials set red_brick --no-groups
```

Options: `--name <text>`, `--group <id>` (repeat; replaces all groups), `--no-groups`, `--tag <tag>` (repeat; replaces all tags), `--license <id>`, `--tile <WxH>`. `--group` with `--no-groups` is `bad_arguments`. The answer is the material entry. A new `--tile` makes the state `needs_refresh`, because the thumbnail shows the tile size. Run `materials thumbnail` to draw it again. An unknown group or license is `not_found` and nothing changes.

### materials remove

Deletes a material and its whole folder. It cannot be undone, so `--yes` is required. Plans that use the id keep it, and the next build says `missing_material`.

```
plynth materials remove clear_glass --yes
```

```json
{"ok":true,"command":"materials remove","result":{"removed":"clear_glass"}}
```

Without `--yes` the answer is `bad_arguments` and nothing is deleted.

### materials groups

Lists the groups with how many materials hold each. The built-in groups are `floor`, `wall`, `ceiling`, `exterior`, `trim`, `door`, `glass` and `metal`.

```
plynth materials groups
```

```json
{"ok":true,"command":"materials groups","result":{"groups":[{"id":"floor","name":"Floor","built_in":true,"materials":2},{"id":"wall","name":"Wall","built_in":true,"materials":3}]}}
```

The example is shortened.

### materials group add, group set, group remove

```
plynth materials group add stone "Stone"
plynth materials group set stone --name "Natural stone"
plynth materials group remove stone
```

`group add <id> <name>` makes a group. `group set <id> --name <text>` renames it, and the id stays. `group remove <id>` removes it from the list and from every material that has it. The eight built-in groups (`floor`, `wall`, `ceiling`, `exterior`, `trim`, `door`, `glass` and `metal`) cannot be renamed or removed. Both commands answer `invalid_value` and change nothing. The first two answer with the group entry, as in `materials groups`. The last answers:

```json
{"ok":true,"command":"materials group remove","result":{"removed":"stone","materials_changed":["red_brick"]}}
```

### materials licenses

Lists the licenses a material can have, as `assets licenses` does.

### materials rescan

Rebuilds the materials index from the folders in `Library\materials\items\`. It runs no Blender. Materials that need a new render are listed in `needs_refresh`: a folder with images and no `info.json`, a changed map, or a new tile size. Use `materials thumbnail <id>` on each. A deleted map makes the material Broken, and a removed folder removes its row. The answer has the same fields as `assets rescan`.

```
plynth materials rescan
```

### materials thumbnail

Cleans a material again and renders a new thumbnail. Use it after `materials set --tile`, when a render failed, or after you changed an image in the material's folder. It keeps the name, groups and tags. The answer is the material entry. Options: `--blender`, `--quiet`.

```
plynth materials thumbnail red_brick
```

### materials importers

Lists the ways a material can be imported.

```
plynth materials importers
```

```json
{"ok":true,"command":"materials importers","result":{"importers":[{"id":"blend_material","name":"Material from a .blend file","description":"One material of a Blender file, made into texture maps.","suffixes":[".blend"]},{"id":"texture_set","name":"Texture set","description":"A folder, zip file or picture file of texture maps such as base color, normal and roughness.","suffixes":[".png",".jpg",".jpeg",".tga",".tif",".tiff",".bmp",".exr",".zip"]}]}}
```

## Warnings

An answer can have a top level `warnings` list next to `result`. It appears only when there is something to say, and it never changes `ok`. Each warning has a `code` and a `message`. Check for it in scripts: the command worked, and the warning says what to look at.

Codes in use:

- `image_missing`: a plan image of the project could not be found. Edits are still saved and the image path is kept. Put the file back, or set another image.
- The cleanup codes listed under `assets import`: `high_polycount`, `scale_suspicious`, `missing_texture`, `removed_objects`, `unsupported_object`, `fbx_failed`.
- The material import codes listed under `materials import`: `ambiguous_map`, `unused_image`, `map_size_mismatch`, `visible_seams`, `low_resolution`, `picked_material`.
- The build codes listed under `build`, including `missing_material`, `material_broken`, `material_failed`, the stair and roof codes. `build` puts one warning in the list for every problem it found, whether its severity is `error` or `warning`.
- The furniture codes listed under "Furniture items": `no_room`, `outside_room`, `overlaps_item`, `overlaps_wall`, `overlaps_opening`, `off_wall`, `too_tall`, `missing_asset`, `size_changed`. `place` and the `item` commands that change an item give the problems of that item. With `--strict` they are refused instead.

A project whose plan image was deleted, after `scale set`:

```json
{"ok":true,"command":"scale set","result":{"pixels_per_metre":100.0,"moved_marks":false},"warnings":[{"code":"image_missing","message":"The plan image plans/apartment-01.png is missing. Its path was kept."}]}
```

`project new` with an image that is missing is still refused with `not_found`, because there is nothing yet to keep.

## The setup order

For a script on a new machine:

1. `plynth doctor`. Read `ready` and the items. Install Blender yourself from blender.org if it is missing. Plynth never installs anything.
2. `plynth library init <folder> --use`, then `plynth settings set --projects <folder>`.
3. `plynth doctor` again. `ready` should now be true.
4. `plynth assets import <file>` for each model. Add `--category` and `--tag` so you do not have to edit afterwards.
5. `plynth assets search <words>` to find the ids you need.
6. `plynth materials import samples\materials\*` to add the nine sample materials, which new projects use by default. Add `--skip-existing` to run it again safely. Then `plynth materials search <words>` finds material ids.

## File format version 5

`plan.json` and `project.json` are now at format version 5. What changed:

- Every floor in `plan.json` has `slab_thickness` (metres, 0.05 to 1, default 0.2), `image_offset` (`{"x", "y"}` in metres, default 0, 0) and `stairs` (a list). A stair has `id`, `type`, `x`, `y`, `rotation`, `width`, `turn` (`left` or `right`) and `material` (`null` for the project default).
- `plan.json` has `roof`, the roof default (`type`, `pitch`, `overhang`, `parapet`, `direction`, which is `null` for automatic), and `roof_sections`, a list of entries with an `id` (`rf1`), a `floor`, an anchor point `x`, `y`, their own settings and a `material`. Roof sections themselves are not saved. They are worked out from the floors, and an entry belongs to the section that holds its anchor point.
- `project.json` has three more default materials: `roof`, `flat_roof` and `stair`.
- Floors now stack on slabs, so the level of a floor is the level below plus its ceiling height plus its slab thickness. A version 4 project with several floors has each upper floor 0.2 m higher for every floor below it.

**Older projects upgrade by themselves.** A version 4 plan gets a slab of 0.2 m on every floor, an offset of 0, no stairs, the default roof (flat, pitch 30, overhang 0.3, parapet 0.6) and no entries. A version 4 project gets `roof_tiles`, `concrete` and `oak_planks` as the three new defaults. Nothing is written when you only read. The next command that edits and saves writes both files as version 5, as the app does.

**Older programs refuse the new files** with `unsupported_version`. The shape of each version is in `schema/plan.schema.json` and `schema/project.schema.json` (version 5), and `schema/plan.v4.schema.json` and `schema/project.v4.schema.json` (version 4, kept as it was).

The sections below describe version 4, which version 5 only adds to.

## File format version 4

`plan.json` and `project.json` were at format version 4 before floors, stairs and roofs. What changed:

- Every room in `plan.json` has `wall_material`, the material of the wall faces that look into it (`null` when not set). Walls already had `material_inside` and `material_outside`, and rooms `floor_material` and `ceiling_material`.
- `project.json` has `materials`, the default material of each of the nine surfaces (`wall_inside`, `wall_outside`, `wall_top`, `floor`, `ceiling`, `door`, `frame`, `glass`, `handle`). A value is a material id, or `null` for none.

**Older projects upgrade by themselves**, as described under "File format version 3" below. A version 3 plan gets `"wall_material": null` on every room, and a version 3 project gets the sample materials as its defaults: `white_paint`, `red_brick`, `white_paint`, `oak_planks`, `white_paint`, `painted_wood`, `white_paint`, `clear_glass` and `brushed_steel`, in the order above. Nothing is written when you only read. The next command that edits and saves writes both files as version 4. The app does the same.

**Older programs refuse the new files** with `unsupported_version`, as for version 3. The shape of each version is in `schema/plan.v4.schema.json` and `schema/project.v4.schema.json` (version 4), and `schema/plan.v3.schema.json` and `schema/project.v3.schema.json` (version 3, both kept as they were).

The sections below describe version 3, which version 4 only adds to.

## File format version 3

Version 3 is what `plan.json` and `project.json` were before materials. Today they are saved as version 5, and the numbers in this section are the ones of that earlier release. What version 3 changed:

- Every floor in `plan.json` has `items`, the furniture placed on it. Each item has `id`, `asset`, `room`, `x`, `y`, `z`, `rotation`, `wall` and `size`. See "Furniture items".
- `project.json` has `wall_item_centre` in `defaults`, the height of the middle of a newly mounted wall item (see `project set`).
- Version 2 added `hinge` and `swing` to every opening, and `defaults` to `project.json`.

**Older projects upgrade by themselves.** A version 1 or 2 file is read and upgraded in memory. A version 2 plan gets `"items": []` on every floor and a version 2 project gets `"wall_item_centre": 1.2`. A version 1 file also gets the hinge, swing and default sizes of version 2: openings get `"hinge": "start"` and `"swing": "right"`. Nothing is written when you only read. The next command that edits and saves writes both files as version 3:

```
plynth project info --project D:\Plans\OldFlat
plynth wall set w1 --thickness 0.25 --project D:\Plans\OldFlat
```

After the second command, `plan.json` and `project.json` both say `"version": 3`. `project info` reports `"version":3` under `plan` straight away, because that is the version it would save. The app does the same when it opens and saves an old project.

**Older programs refuse the new files.** A version of Plynth from before this change reads only versions 1 and 2. Given a version 3 file it answers `unsupported_version`. Keep a copy of a project folder if you still need to open it with the old program.

**Files from a newer Plynth** than the one you run are refused with `unsupported_version`, and nothing is changed. The version is in the file's `version` field. The shape of each version is in `schema/plan.schema.json` and `schema/project.schema.json` (version 3), `schema/plan.v2.schema.json` and `schema/project.v2.schema.json` (version 2) and `schema/plan.v1.schema.json` and `schema/project.v1.schema.json` (version 1, kept for reference).

The machine settings file has its own version. Version 2 added `recent_projects`, and a version 1 file is read as having none and written back as version 2.

## The script order

A script builds a plan in this order, because each step needs the one before it:

1. Project and scale: `project new`, `scale set`.
2. Default sizes, if you want others than the built-in ones: `project set`.
3. Walls: `wall draw` (or `point add` and `wall add`), then `wall set --bulge` for curved walls and `point set --corner-radius` for rounded corners. Heights of walls and floors can be set now or later: `floor set`, `wall set --height`.
4. Rooms: `room find`, `room add`.
5. Openings: `opening add`, then `opening set` for shape, hinge and swing.
6. Room types: `room set <id> --type living`, so `place --room living` can find the room.
7. Furniture: `place`, then `item move`, `item rotate`, `item attach`. Mount wall items after the openings, so the overlap with a window is checked.
8. More floors, stairs and roofs: `floor add` with its plan image, then `detect --floor f2 --apply` or the wall commands with `--floor f2`, then `stair add` on the lower floor, then `roof set`. A stair needs the floor above to exist, so add the floors first.
9. Check: `opening list` and read `problems`, `room list` and read the areas, `item list` and read `problems` and `asset_state`.

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

A second floor with a stair and a pitched roof goes like this. Every command takes the same `--project` option as above.

```
plynth floor add --name "Upper floor" --image samples\plans\house-01-upper.png
plynth detect --floor f2 --apply
plynth floor set --floor f2 --image-offset 0.05,0
plynth stair add --floor f1 --x 4.2 --y 2.1 --rot 90 --type u_shape
plynth stair list
plynth roof set --floor f2 --at 3,2 --type gable --pitch 35
plynth roof list
plynth build
```

Read `problems` in `stair list` before building: a stair that crosses a wall or arrives outside a room says so there. `roof list` shows what the build will make, including `type_built` when a hip roof had to fall back to a gable. `build` reports `slabs`, `stairs` and `roofs` in its counts.

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

Read only commands such as `project info`, `point list`, `wall list`, `opening list`, `opening shapes`, `floor list`, `stair list`, `stair types`, `roof list`, `roof types`, `room list`, `room find`, `build` and `detect` without `--apply` are never refused. `floor add`, `floor set`, `floor remove`, `stair add`, `stair set`, `stair remove`, `roof set` and `roof clear` change the plan, so they follow the rules above. `build` only writes into the project's `build` folder and never changes the plan.

### Expiry

A session lasts 5 minutes after the last command. Every command except `session status` and `session end` moves that time forward. If a script goes quiet for longer, the session ends by itself and the app becomes editable. The next edit command is then refused with `locked` and the message "The edit session ended after 5 minutes without commands. Run 'plynth session begin' again." Begin again to continue.

### Unlock now

The banner in the app has an "Unlock now" button. After a confirmation it ends the session at once. The script's next edit is refused with `locked` until it begins a new session. This is how a person takes the project back.

### Autosave

The app keeps a recovery copy of unsaved work in the project's `autosave` folder. The command line never reads, writes or removes it. `project info` only reports whether it exists.

### The two small files

The session uses two small files in the project folder: `session.lock` (the command line session) and `app-state.json` (the app has this project open). They are not part of the project. Both are safe to delete when the app is closed. A crashed app does not block the command line, because the app file counts only while the app's process is still running.
