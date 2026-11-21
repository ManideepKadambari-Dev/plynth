# Command line guide

Plynth can be driven from a terminal, a script or a coding assistant. Every command prints one JSON answer, so a program can read the result without parsing text meant for people.

This page covers the commands that exist today: version, help, creating a project, reading it, setting the scale, adding points and walls, and the edit session that keeps the app and the command line from overwriting each other. More commands arrive in later releases.

## Running a command

Two programs sit side by side in the Plynth folder.

| Program | What it is for |
|---|---|
| `plynth.com` | The one to use from a terminal. It waits for the command to finish, so the answer appears before the prompt comes back and the exit code is set. |
| `plynth.exe` | The app. It also runs commands, but a terminal does not wait for it, so the prompt can come back before the answer is printed. Use it from scripts that capture its output. |

Typing `plynth` in a terminal finds `plynth.com` first. The terminal inside Plynth is set up so `plynth` works there without any path.

The first word decides what happens:

- A command word (`version`, `help`, `project`, `scale`, `point`, `wall`, `session`) runs the command and prints the answer. No window opens.
- A project folder or an app option (`--image`, `--screenshot`, `--size`, `--terminal`), or no argument at all, opens the window. `plynth.com` starts the window and returns at once.
- Anything else prints an `unknown_command` answer and exits with code 2. For example `plynth bogus`.

To open a folder that is literally named `project`, write `.\project`.

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
{"ok":true,"command":"point add","result":{"id":"p3","floor":"f1","x":1.5,"y":2.0,"corner_radius":0.0}}
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
| `not_found` | A floor, point, wall or file does not exist. |
| `already_exists` | The thing to create is already there, for example `project new` in a folder that holds a project. |
| `in_use` | Removing something would leave another item pointing at nothing. |
| `not_a_project` | The folder has no `project.json`. |
| `cannot_read` | A file or folder could not be opened. |
| `cannot_write` | A file or folder could not be written. |
| `bad_file` | A project file is not valid or breaks the format rules. `error.where` says where. |
| `unsupported_version` | The file was written by a newer version of Plynth. |
| `image_error` | The plan image was refused: missing, empty, damaged, too large or not PNG, JPEG or BMP. |
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

### scale set

Sets how many image pixels make one metre.

| Option | Meaning |
|---|---|
| `--pixels-per-metre <number>` | Required. Above zero. |

```
plynth scale set --pixels-per-metre 100
```

```json
{"ok":true,"command":"scale set","result":{"pixels_per_metre":100.0}}
```

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
{"ok":true,"command":"point add","result":{"id":"p1","floor":"f1","x":1.5,"y":2.0,"corner_radius":0.0}}
```

Ids are a letter plus a number: `p1`, `p2` and so on. An id never changes once it is given.

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
{"ok":true,"command":"wall add","result":{"id":"w1","floor":"f1","from":"p1","to":"p2","thickness":0.3,"height":null}}
```

A wall that names a point that does not exist is refused with `not_found`, and the project files are not touched.

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
2. **Edit.** Run commands such as `point add` and `wall add`. Each one saves to the project folder. The app notices the change within a second and shows it live. Zoom and pan stay where they were.
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

Read only commands such as `project info` are never refused.

### Expiry

A session lasts 5 minutes after the last command. Every command except `session status` and `session end` moves that time forward. If a script goes quiet for longer, the session ends by itself and the app becomes editable. The next edit command is then refused with `locked` and the message "The edit session ended after 5 minutes without commands. Run 'plynth session begin' again." Begin again to continue.

### Unlock now

The banner in the app has an "Unlock now" button. After a confirmation it ends the session at once. The script's next edit is refused with `locked` until it begins a new session. This is how a person takes the project back.

### Autosave

The app keeps a recovery copy of unsaved work in the project's `autosave` folder. The command line never reads, writes or removes it. `project info` only reports whether it exists.

### The two small files

The session uses two small files in the project folder: `session.lock` (the command line session) and `app-state.json` (the app has this project open). They are not part of the project. Both are safe to delete when the app is closed. A crashed app does not block the command line, because the app file counts only while the app's process is still running.
