# User guide

This covers what Plynth does today: opening a project, showing its plan image, setting the scale, drawing walls, finding walls in the image automatically, making rooms, undo and redo, saving, working next to the command line, and the built-in terminal. More screens arrive in later releases.

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

Once the scale is set, points and walls are drawn over the image: walls as blue bands of their real thickness, points as small blue dots, rooms as a faint blue fill with a name and an area.

If the image file is missing, the sheet shows "The plan image ... could not be opened."

## The tools

A strip of buttons sits at the left edge of the plan. The active tool is the one in blue. Each button has a tooltip with its name and key.

| Tool | Key | What it is for |
|---|---|---|
| Select | V | Pick points, walls and rooms, and drag points |
| Scale | S | Tell Plynth how long something on the image really is |
| Wall | W | Draw walls |
| Room | R | Turn a closed area into a room |
| Detect | D | Find walls and rooms in the plan image for you |

The status bar at the bottom shows the position of the cursor in metres and a short hint for the active tool. If a tool cannot do what you asked, the reason shows there in amber for a few seconds instead of in a dialog.

The Scale tool needs a plan image. The Wall, Room and Detect tools also need a scale, because without one a click cannot be turned into metres. Until the scale is set, their buttons are greyed and their tooltips say "Set the scale first (S)". While the command line is editing the project, the Scale, Wall, Room and Detect tools are disabled and you can only select. You can still click to look at things.

## Setting the scale

Plynth needs to know how big the drawing is. You tell it by measuring something on the plan whose real size you know, such as a wall length written on the plan or a door.

1. Press S, or click the Scale tool.
2. Click one end of the known length. Move the mouse: an amber line follows it and shows its length in image pixels. Hold Shift to keep the line horizontal or vertical.
3. Click the other end.
4. In the "Set scale" box, type the real distance between the two points in metres, and choose Set scale.

Esc cancels while you measure. Two clicks closer than a pixel are refused. This tool does not snap to anything, because it measures the image itself.

If a scale is already set, the box starts with the distance Plynth measures now. When the plan already has points, a box "Keep marks on the image" appears and is ticked. Ticked means the points and walls stay on the same spot of the picture, so their metre values change to match the new scale. Wall thickness, heights and widths are real sizes and stay as they are. Unticked means every metre value stays and the marks move on the picture. Setting the very first scale moves nothing. You can undo a scale change, and the marks go back exactly.

The scale shows in the properties panel as "100.0 px per m".

## Drawing walls

Press W, or click the Wall tool. While it is active, the grid shows as thin lines when the lines are at least 8 pixels apart on screen.

1. Click where the wall starts.
2. Click where it ends. The wall is drawn.
3. Keep clicking. Each click draws the next wall from where the last one ended. This is called a chain.
4. To close a room, click the first point of the chain again. The wall is drawn and the chain ends by itself.
5. To stop without closing, press Esc or click the right mouse button. The next click starts a new chain.

An amber band follows the mouse, with the thickness of the wall that would be drawn. The amber mark at its end shows what the click would snap to.

**Snapping.** A click does not land exactly where the mouse is. Plynth looks, in this order:

1. An existing point within about 10 screen pixels. The wall then uses that point. A ring marks it.
2. The body of an existing wall within the same distance. The wall you draw ends on it, and the old wall is cut in two there, so they meet at a shared point. This is a T junction. A small cross marks it.
3. The grid, if "Snap to grid" is on. A small plus marks the grid point.
4. Nothing: the exact spot you clicked.

**Keys while drawing.** Hold Shift while you click to keep the new wall exactly horizontal or vertical from the last point. A point close by still wins over Shift, so you can always close a loop. Hold Ctrl while you click to place the point freely, with no snapping at all. Press G, or use View > Snap to grid, to turn grid snapping on or off. Snapping to points and walls stays on.

**Thickness.** While the Wall tool is active, the properties panel has a "New walls" box. It sets the thickness for the walls you draw next. It starts at 0.2 m and is kept until you close Plynth.

Every wall is one undo step, even when it also made new points or cut another wall. Ctrl+Z removes the wall and puts the cut wall back together. If you undo, redo or reload while a chain is open, the chain ends.

## Selecting and editing

Press V for the Select tool. It is the tool you start with.

- Click a point, a wall or a room to select it. It turns amber, and the properties panel on the right shows it. Click on nothing, or press Esc, to clear the selection.
- Drag a point to move it. The walls at the point follow. The point snaps to the grid, and Shift keeps its move straight. It never snaps onto other points or walls. A whole drag is one undo step.

**The properties panel.** With nothing selected, it shows the scale, the grid size (change it and press Enter), and the number of points, walls and rooms. With a selection it shows:

| Selected | What you see and can change |
|---|---|
| A point | Its id, X and Y in metres (change a number and press Enter), and the walls that end there |
| A wall | Its id, the two points, its length, and its thickness (change it and press Enter) |
| A room | Its id, its type, its area, and the number of walls |

A number is only changed when you type in its box. Clicking into a box and leaving it never rounds the real value. Letters you type in a box go into the box and do not switch tools.

## Rooms

A room is a closed area made of walls. It has a type and an area, and it is what Plynth later turns into floors.

1. Draw walls so they close an area. The walls have to share points where they meet. Snapping does that for you.
2. Press R, or click the Room tool.
3. Click inside the closed area. A menu offers the room types: Living room, Bedroom, Kitchen, Bathroom, Hall, Dining room, Office, Storage and Other, and "No type yet".
4. Pick one. The room appears with a faint blue fill, its name and its area.

Click inside an existing room with the Room tool to change its type. You can also change the type in the properties panel when the room is selected, with the Select tool.

If you click where no closed area exists, the status bar says "No closed area here. Close the walls around it first."

**The area.** The number shown is the floor inside the walls, in square metres. It is not the area between the wall centre lines. The thickness of every wall is taken off. A 4 m by 4 m square of 0.2 m walls shows 14.44 m². A wall that sticks into a room from one side, without closing off anything, is not counted as part of the room. A room inside another room, such as a box inside a hall, is cut out of the hall's area.

**Rooms keep their walls.** A room remembers the walls of the area it was made from. If you draw a new wall across a room, the room stays as it was. Delete the room and add the two new ones. Cutting one of its walls with a T junction is fine, because the room takes the new part too.

The same room finding is available to scripts with `room find` and `room add`. See the [Command line guide](CLI_GUIDE.md).

## Detecting walls

Instead of clicking every wall, you can let Plynth look at the plan image and propose the walls and rooms. You check the result, tune it with sliders, and accept it. You can always draw walls by hand instead, or to finish what detection missed.

**What you need.** A plan image and a scale. Set the scale first (S).

**Which plans work best.** Clean plans where the walls are dark and solid, on light paper. Right now Plynth finds horizontal and vertical walls only. A scan that is a few degrees crooked is fine. Diagonal walls and curved walls are not found, so draw those by hand. Plans where walls are drawn as two thin outlines with nothing between them, or very noisy or textured scans, give poor results. Thin marks such as text, dimension lines, door arcs, window lines and furniture outlines are ignored on purpose.

**Using it.**

1. Press D, or click the Detect tool. The panel on the right shows the detection settings, and Plynth starts looking at once.
2. After a moment the proposals appear over the plan as dashed blue outlines: walls with a faint blue fill, hollow blue rings at corners and junctions, and a dashed blue outline for each room it found. Doors, windows and gaps show as two small blue ticks across the wall, with a quarter arc for a door and two thin lines for a window.
3. Under the settings, a line says what was found, for example "16 walls, 5 rooms, 9 openings in 85 ms". It also tells you when some walls are already drawn, or when some overlap walls you drew and are left out.
4. Move the sliders until the blue walls match the real ones. The preview follows about a fifth of a second after you stop. The app stays responsive while it works.
5. Press Enter, or click Accept. The walls and rooms are added to the plan in the normal style. One Ctrl+Z takes all of them away again, and the proposals come back so you can try other settings.

Leaving the tool throws the proposals away. Nothing is added to the plan until you accept. Your settings are kept until you close Plynth.

**The settings.**

| Setting | What it does |
|---|---|
| Thinnest wall | Dark lines thinner than this are not walls. Raise it to drop thin inner walls, or lower it if real thin walls are missing. |
| Thickest wall | Dark areas thicker than this are not walls. This keeps filled blocks, such as a legend box, out. It stays above the thinnest wall. |
| Sensitivity | How dark a pixel has to be to count as ink. Raise it for faint plans so lighter grey counts. Lower it when grey paper or shading is being taken for walls. |
| Shortest wall | Wall pieces shorter than this are dropped. Raise it to get rid of small specks. |
| Widest opening | A gap up to this wide in a wall, such as a door or a window, is closed with wall so rooms stay separate, and shown as an opening. Set it to 0 to close no gaps. |
| Propose rooms | Also propose a room for every closed area. |
| Show openings | Show doors, windows and gaps. |

**Reset settings** puts every setting back to its default.

**Running it again never doubles walls.** Detection compares what it finds with the walls you already have. Walls that are already in the plan, whether you drew them or accepted them earlier, are not shown and are never added a second time. The status line counts them. If a proposed wall runs partly along a wall you drew, it is left out and counted, not trimmed. Draw the missing piece by hand. New walls snap onto the points and walls you already have, so detected walls join hand drawn ones.

**Openings are for checking only.** The doors, windows and gaps are shown so you can see that the walls make sense, but Accept does not add them yet. They are added in a later version. The walls are closed across them, so every room stays a room.

If detection cannot run, the panel says why in amber, for example when the plan has no scale or the image could not be read. While a command line session is editing the project, the Detect tool is off.

## Deleting

Select something and press Delete or Backspace, use Edit > Delete, or use the Delete button under the selection in the properties panel. One undo brings everything back.

Delete takes with it whatever cannot stay without the thing you deleted:

- Deleting a point deletes the walls that end at it, and the rooms that use those walls.
- Deleting a wall deletes the rooms that use it.
- Deleting a room deletes only the room. Its walls stay.
- A point left with no wall because of a delete goes too, so no stray dots stay behind. A point you placed on its own is only removed when you select it.


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
- The Scale, Wall and Room tools, Delete, and every box in the properties panel are disabled. You can still click to select and look.
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
