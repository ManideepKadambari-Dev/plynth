# User guide

This is the first version. It covers what Plynth does today: opening a project, showing its plan image, undo and redo, saving, working next to the command line, and the built-in terminal. More screens arrive in later releases.

For keys see [Keyboard shortcuts](SHORTCUTS.md). For scripts see the [Command line guide](CLI_GUIDE.md).

## Projects

A project is one folder:

```
MyApartment/
  project.json      settings, export mode, grid size
  plan.json         all marks
  plans/            floor1.png, floor2.png
  build/            building.blend, preview.glb
  export/           FBX files, Textures/, reports
  renders/          images for checking results
  autosave/         a recovery copy of unsaved work
```

Only `plan.json`, `project.json` and `plans/` are the real project. `build/`, `export/`, `renders/` and `autosave/` can be deleted at any time; Plynth recreates them. Today only the first three and `autosave/` are used.

Two small files, `session.lock` and `app-state.json`, may also appear. They are explained below and are safe to delete while Plynth is closed.

### Create and open

- **File > New project** (Ctrl+N) starts an empty, untitled project.
- **File > New project from image** (Ctrl+Shift+N) starts an untitled project with a plan image already in place.
- **File > Open project** (Ctrl+O) asks for a project folder.

You can also open a folder from a terminal: `plynth MyApartment`.

An untitled project has no folder yet. Save it to give it one.

## The plan image

**File > Set plan image** (Ctrl+Shift+O), or drop a PNG, JPEG or BMP file on the window. The image is set on the current floor. You can undo it.

Plynth fits the image to the window. Zoom with the mouse wheel, pan with the middle button or with Space and the left button. Ctrl+0 fits again and Ctrl+1 shows 100 percent. The full list is in the shortcuts page.

If the scale of the plan is set, points and walls are drawn over the image: walls as blue bands of their real thickness, points as small blue dots. Today these can be added from the command line. Drawing them in the app comes in a later release.

If the image file is missing, the sheet shows "The plan image ... could not be opened."

## Undo and redo

Every change to the plan can be undone. Use **Edit > Undo** (Ctrl+Z) and **Edit > Redo** (Ctrl+Y or Ctrl+Shift+Z). The menu names the change, for example "Undo Set plan image". If you undo back to the point where you last saved, the unsaved mark goes away.

## Saving

The title bar shows the project name, with `*` after it when there are unsaved changes.

- **Save** (Ctrl+S) writes `project.json` and `plan.json`. Images from outside the project are copied into `plans/`.
- **Save as** (Ctrl+Shift+S) writes to another folder, copying the images. The old folder is not changed. Plynth refuses a folder that already holds a different project.
- Closing, opening or starting a new project with unsaved changes asks "Save changes to <name>?" with Save, Don't save and Cancel.

A save writes each file to a temporary name first and then renames it, so a crash during a save does not leave a half written file.

### Autosave and recovery

Every 30 seconds, if there are unsaved changes, Plynth writes a copy into the project's `autosave/` folder. Saving or choosing Don't save removes it. Untitled projects have no folder, so they are not autosaved.

If Plynth was closed unexpectedly, the next time you open that project it asks: "This project has unsaved changes from <date and time>. Recover them?" If the saved plan is newer than the copy, the question tells you. Recover loads the copy as unsaved changes, so you still decide whether to save. Discard removes it.

## When the command line is editing

A script or a coding assistant can edit the project while Plynth has it open. To keep your work and its work apart, it first begins an edit session. While one is active:

- A banner under the title bar says "Being edited from the command line by <name>. Changes appear here as they are saved."
- The plan is dimmed. Undo, Redo, Save, Save as and Set plan image are disabled. New project and Open project still work.
- Changes appear on screen within a second, with the zoom and position unchanged.
- When the session ends, or after 5 minutes without a command, the plan reloads once and becomes editable again.

A project you open while a session is active starts read only. The recovery question waits until it unlocks.

**Unlock now.** The button on the banner ends the session at once. Plynth asks first: "Unlocking ends the command line session. Its next edit will be refused until it begins a new one." Use it when a script has stalled or you want the project back.

If the app has unsaved changes, a script cannot begin a session. It has to wait until you save or discard.

**Changed outside Plynth.** In the rare case that the files change while you have unsaved edits and no session is active, Plynth asks "The plan was changed outside Plynth. Load the new version? Your unsaved changes will be lost." Choose Load to take the new version, or Keep mine to keep working. Keep mine means your next save overwrites the outside change.

The commands and the exact rules are in the [Command line guide](CLI_GUIDE.md).

## The terminal

Plynth has a terminal built in, so you can run commands without leaving the window.

- Open it with the round `>_` button at the bottom right of the plan, or with **View > Terminal** (Ctrl+`). The same key hides it. Hiding keeps the shell running with its history.
- It starts your shell in the project folder. For an untitled project it starts in your Documents folder. It uses PowerShell 7 if installed, else Windows PowerShell, else the Command Prompt.
- `plynth` works there without a path, so you can run `plynth project info` or `plynth session begin` straight away.
- Drag the top left corner of the panel to resize it.
- Closing or switching the project ends the shell. If a program is still running, Plynth asks first: "A program is still running in the terminal. Closing the project will stop it."
- If the shell exits by itself, the panel says "The shell ended. Press Enter to start a new one."

While the terminal has focus, nearly every key goes to the shell, including Ctrl+S, Ctrl+Z and Ctrl+O. They do not save, undo or open anything in Plynth until you click outside the terminal. Ctrl+C copies when text is selected and otherwise stops the running program. Ctrl+V pastes, and Shift+PageUp and Shift+PageDown scroll back. The full list is in the [shortcuts](SHORTCUTS.md).

The terminal shows program output in its own set of colors, so colored text from tools looks right.
