# Command line guide

Plynth can be driven from a terminal, a script or a coding assistant. Every command prints one JSON answer, so a program can read the result without parsing text meant for people.

This page covers the commands that exist today: version, help, creating and reading a project, setting the scale and grid, points, walls, rooms, finding walls in the plan image with `detect`, and the edit session that keeps the app and the command line from overwriting each other. More commands arrive in later releases.

## Running a command

Two programs sit side by side in the Plynth folder.

| Program | What it is for |
|---|---|
| `plynth.com` | The one to use from a terminal. It waits for the command to finish, so the answer appears before the prompt comes back and the exit code is set. |
| `plynth.exe` | The app. It also runs commands, but a terminal does not wait for it, so the prompt can come back before the answer is printed. Use it from scripts that capture its output. |

Typing `plynth` in a terminal finds `plynth.com` first. The terminal inside Plynth is set up so `plynth` works there without any path.

The first word decides what happens:

- A command word (`version`, `help`, `project`, `scale`, `point`, `wall`, `room`, `detect`, `session`) runs the command and prints the answer. No window opens.
- A project folder or an app option (`--image`, `--screenshot`, `--size`, `--terminal`), or no argument at all, opens the window. `plynth.com` starts the window and returns at once.
- Anything else prints an `unknown_command` answer and exits with code 2. For example `plynth bogus`.

To open a folder that is literally named after a command word, such as `project`, `room` or `detect`, write `.\project`, `.\room` or `.\detect`. Without the `.\` Plynth runs the command instead.

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
{"ok":true,"command":"point add","result":{"id":"p3","floor":"f1","x":1.5,"y":2.0,"corner_radius":0.0,"walls":[]}}
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
| `invalid_value` | A number or name is out of range, for example a scale of 0. |
| `not_found` | A floor, point, wall, room or file does not exist, or there is no closed area at the position given to `room add`. |
| `already_exists` | The thing to create is already there, for example `project new` in a folder that holds a project, a wall between two points that already have one, or a room where one already is. |
| `in_use` | Removing something would leave another item pointing at nothing. Add `--cascade` to remove what depends on it as well (see `point remove`). |
| `not_a_project` | The folder has no `project.json`. |
| `cannot_read` | A file or folder could not be opened. |
| `cannot_write` | A file or folder could not be written. |
| `bad_file` | A project file is not valid or breaks the format rules. `error.where` says where. |
| `unsupported_version` | The file was written by a newer version of Plynth. |
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
{"ok":true,"command":"version","result":{"version":"0.0.1","plan_format":1,"project_format":1}}
```

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
{"ok":true,"command":"project new","result":{"folder":"D:/Plans/MyApartment","name":"MyApartment","export_mode":"single","grid_size":0.5,"has_autosave":false,"plan":{"version":1,"pixels_per_metre":null,"floors":[{"id":"f1","name":"Floor 1","image":"plans/plan.png","image_found":true,"ceiling_height":3.0,"points":0,"walls":0,"rooms":0,"openings":0}]}}}
```

Without `--image` the floor has no image and `image` is `null`.

### project info

Shows the settings and contents of a project. It changes nothing. The result is the same as for `project new`.

- `pixels_per_metre` is `null` until the scale is set.
- `image_found` says whether the image file the plan points at exists.
- `has_autosave` says whether the app left a recovery copy in the project. The command line never reads, writes or removes it.

```
plynth project info
```

### project set

Changes a project setting. Today the only one is the grid size.

| Option | Meaning |
|---|---|
| `--grid <metres>` | Required. Distance between grid lines. Above zero. |

```
plynth project set --grid 0.25
```

The answer is the same as for `project info`, with the new `grid_size`. A grid of 0 or less is refused with `invalid_value`. The grid starts at the top left corner of the image. The app uses it for snapping, and so does `wall draw --grid`.

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
| `--corner-radius <metres>` | Corner rounding. Default: 0. |

```
plynth point add --x 1.5 --y 2
```

```json
{"ok":true,"command":"point add","result":{"id":"p1","floor":"f1","x":1.5,"y":2.0,"corner_radius":0.0,"walls":[]}}
```

Ids are a letter plus a number: `p1`, `p2` and so on. An id never changes once it is given.

Every answer that shows a point has the same shape (the "point entry"). `walls` lists the ids of the walls that end at the point. It is empty for a point with no wall.

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
{"ok":true,"command":"point move","result":{"id":"p6","floor":"f1","x":9.0,"y":8.0,"corner_radius":0.0,"walls":["w3","w6","w7"]}}
```

The walls that end at the point follow it. An unknown id gives `not_found` and changes nothing.

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
{"ok":true,"command":"point remove","result":{"removed":{"rooms":["r1"],"walls":["w3","w6","w7"],"points":["p3","p6"]}}}
```

Without `--cascade`, a point that a wall uses is refused with `in_use`. With it, everything goes in one step: the walls at the point, the rooms that use those walls, and the end points of removed walls that are left with no wall. A point you placed on its own is only removed when you name it. The `removed` lists show what went. `wall remove` gives the same shape.

### point list

Lists the points of a floor.

| Option | Meaning |
|---|---|
| `--floor <id>` | Default: the first floor. |

```
plynth point list
```

```json
{"ok":true,"command":"point list","result":{"floor":"f1","points":[{"id":"p1","floor":"f1","x":1.0,"y":1.0,"corner_radius":0.0,"walls":["w1","w4"]}]}}
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
{"ok":true,"command":"wall add","result":{"id":"w1","floor":"f1","from":"p1","to":"p2","thickness":0.3,"height":null,"bulge":0.0,"length":6.0,"rooms":[]}}
```

A wall that names a point that does not exist is refused with `not_found`, and the project files are not touched. A second wall between the same two points is refused with `already_exists`.

Every answer that shows a wall has the same shape (the "wall entry"). `bulge` is 0 for a straight wall. `length` is in metres. `rooms` lists the ids of the rooms that use the wall.

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
{"ok":true,"command":"wall draw","result":{"id":"w7","floor":"f1","from":"p5","to":"p6","thickness":0.3,"height":null,"bulge":0.0,"length":4.0,"rooms":[],"new_points":["p5","p6"],"split_walls":["w1","w3"]}}
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
{"ok":true,"command":"wall split","result":{"point":{"id":"p7","floor":"f1","x":5.0,"y":1.0,"corner_radius":0.0,"walls":["w5","w8"]},"walls":[{"id":"w5","floor":"f1","from":"p5","to":"p7","thickness":0.3,"height":null,"bulge":0.0,"length":1.0,"rooms":[]},{"id":"w8","floor":"f1","from":"p7","to":"p2","thickness":0.3,"height":null,"bulge":0.0,"length":2.0,"rooms":[]}]}}
```

The old wall keeps its id and runs to the new point. A new wall runs on to the old end. Rooms that used the wall get the new wall too, so they stay closed. A curved wall cannot be split yet.

### wall set

Changes a wall.

| Option | Meaning |
|---|---|
| `<id>` | Required. The wall. |
| `--thickness <metres>` | Required. Above zero. |

```
plynth wall set w1 --thickness 0.25
```

The answer is a wall entry.

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
{"ok":true,"command":"wall remove","result":{"removed":{"rooms":["r1"],"walls":["w2"],"points":[]}}}
```

Without `--cascade`, a wall that a room uses is refused with `in_use`. With it, the rooms that use the wall go as well, and so do end points that are left with no wall.

### wall list

Lists the walls of a floor, with `--floor <id>` (default: the first floor).

```
plynth wall list
```

```json
{"ok":true,"command":"wall list","result":{"floor":"f1","walls":[{"id":"w1","floor":"f1","from":"p1","to":"p5","thickness":0.25,"height":null,"bulge":0.0,"length":3.0,"rooms":[]}]}}
```

### Rooms

A room is a closed area made of walls, with an optional type such as Living room. The room commands work on the walls you already drew.

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

Looks at the plan image of a floor and proposes walls, rooms, and doors, windows and gaps. It needs a plan image and a scale. Without `--apply` it only reads: nothing in the project changes, and it works while the app is editing the project. With `--apply` it adds the new walls and rooms to the plan and saves.

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
| `--no-openings` | Propose no openings. |
| `--apply` | Add the new walls and rooms to the plan, as one undoable step, and save. |

```
plynth detect --project D:\Plans\MyApartment
```

This is a real answer for `samples\plans\apartment-01.png` at 100 pixels per metre. The lists are cut short: the real answer has all 12 points, 16 walls, 9 openings and 5 rooms.

```json
{"ok":true,"command":"detect","result":{"floor":"f1","detector":"orthogonal","detectors":["orthogonal"],"pixels_per_metre":100.0,"image":{"width":2400,"height":1600},"params":{"min_thickness":0.08,"max_thickness":0.5,"sensitivity":0.5,"min_length":0.3,"max_opening_width":2.5,"rooms":true,"openings":true},"elapsed_ms":20.9,"counts":{"points":12,"walls":16,"new_walls":16,"existing_walls":0,"overlapping_walls":0,"rooms":5,"new_rooms":5,"openings":9},"points":[{"index":0,"x":5.15,"y":3.15},{"index":1,"x":10.06,"y":3.15}],"walls":[{"index":0,"from":0,"to":1,"thickness":0.3,"length":4.91,"state":"new"}],"openings":[{"index":0,"wall":0,"type":"window","offset":2.35,"width":2.0,"x":7.5,"y":3.15}],"rooms":[{"index":0,"walls":[0,10,11,3,8],"inside":{"x":7.605,"y":5.5751},"centre_line_area":23.8147,"state":"new"}]}}
```

The answer has these fields.

- `floor`, `detector`, `detectors` (every detector that is installed), `pixels_per_metre`, `image` (width and height in pixels) and `params` (the settings that were used, including `rooms` and `openings`).
- `elapsed_ms` is the time spent finding walls, in milliseconds. Loading the image is not counted.
- `counts` says how many proposals there are. `walls` is the total. `new_walls` are the ones `--apply` would add. `existing_walls` are already in the plan. `overlapping_walls` run partly along a wall that is already there. `new_rooms` are rooms not yet in the plan.
- `points` are the proposed corners and junctions, in metres, numbered by `index`.
- `walls` join two points by their `index` (`from` and `to`). `thickness` and `length` are in metres. `state` is `new`, `existing` or `overlaps`.
- `openings` sit on a wall (`wall` is a wall `index`). `type` is `door`, `window` or `plain`. `offset` is the distance from the wall's `from` point to the centre of the opening, `width` is its width, and `x` and `y` are its centre in metres.
- `rooms` are the closed areas of the proposed walls. `walls` lists wall `index` numbers. `inside` is a position surely inside the area. `centre_line_area` is measured between the wall centre lines, in square metres, rounded to 4 decimals. `state` is `new` or `existing`.
- `applied` appears only with `--apply`. See below.

**States and running it twice.** Detection compares what it found with the walls and rooms the floor already has. A proposed wall that lies on a wall that is already there is `existing`. One that lies partly along an existing wall is `overlaps`. A proposed room is `existing` when a room already holds its `inside` position. Only `new` proposals are added. So running `detect --apply` again does not double anything, and walls you drew by hand are not drawn twice. A proposal that is `overlaps` is left out and counted, never trimmed. Draw the missing piece yourself.

**`--apply`.** The new walls and rooms are added as one undoable step. Ends of new walls snap to points and walls that are already in the plan, so detected walls join hand drawn ones, and a wall that a new one ends on is split there. The answer then also has:

```json
"applied":{"walls":["w1","w2"],"points":["p1","p2"],"split_walls":[],"rooms":["r1"],"skipped_walls":0,"skipped_rooms":0}
```

The ids are the ones that were created. `split_walls` lists existing walls that were cut at a T junction. `skipped_walls` and `skipped_rooms` count proposals the plan refused. The `points`, `walls` and `rooms` lists above it show the states from before applying. When nothing is new, `--apply` changes and saves nothing, and answers ok with empty `applied` lists.

**Openings are listed, not added.** The doors, windows and gaps are in the answer so you can check them, but `--apply` does not add them to the plan. A later version will. The gap in the wall is closed with wall, so rooms stay separate.

The script order for a new project:

```
plynth project new --image plan.png --project D:\Plans\MyApartment
plynth scale set --pixels-per-metre 100 --project D:\Plans\MyApartment
plynth detect --project D:\Plans\MyApartment
plynth detect --apply --project D:\Plans\MyApartment
plynth room set r1 --type living --project D:\Plans\MyApartment
```

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

Read only commands such as `project info`, `point list`, `wall list`, `room list`, `room find` and `detect` without `--apply` are never refused.

### Expiry

A session lasts 5 minutes after the last command. Every command except `session status` and `session end` moves that time forward. If a script goes quiet for longer, the session ends by itself and the app becomes editable. The next edit command is then refused with `locked` and the message "The edit session ended after 5 minutes without commands. Run 'plynth session begin' again." Begin again to continue.

### Unlock now

The banner in the app has an "Unlock now" button. After a confirmation it ends the session at once. The script's next edit is refused with `locked` until it begins a new session. This is how a person takes the project back.

### Autosave

The app keeps a recovery copy of unsaved work in the project's `autosave` folder. The command line never reads, writes or removes it. `project info` only reports whether it exists.

### The two small files

The session uses two small files in the project folder: `session.lock` (the command line session) and `app-state.json` (the app has this project open). They are not part of the project. Both are safe to delete when the app is closed. A crashed app does not block the command line, because the app file counts only while the app's process is still running.
