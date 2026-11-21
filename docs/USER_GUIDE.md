# User guide

This covers what Plynth does today: opening a project, showing its plan image, setting the scale, drawing walls (straight and curved, with rounded corners), placing doors, windows and other openings, setting heights, finding walls and openings in the image automatically, making rooms, undo and redo, saving, the setup check, the library and assets catalog, working next to the command line, and the built-in terminal. More screens arrive in later releases.

For keys see [Keyboard shortcuts](SHORTCUTS.md). For scripts see the [Command line guide](CLI_GUIDE.md). For Blender, the library folder and the first run see [Setting up Plynth](SETUP.md).

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

Once the scale is set, points and walls are drawn over the image: walls as blue bands of their real thickness, points as small blue dots, openings as gaps in the bands, rooms as a faint blue fill with a name and an area.

If the image file is missing, the sheet shows "The plan image ... could not be opened." You can still edit and save. See "When a plan image is missing" below.

## The tools

A strip of buttons sits at the left edge of the plan. The active tool is the one in blue. Each button has a tooltip with its name and key.

| Tool | Key | What it is for |
|---|---|---|
| Select | V | Pick points, walls, openings and rooms, drag points and openings, bend walls |
| Scale | S | Tell Plynth how long something on the image really is |
| Wall | W | Draw walls |
| Opening | O | Place doors, windows and other openings on walls |
| Room | R | Turn a closed area into a room |
| Detect | D | Find walls, openings and rooms in the plan image for you |

The status bar at the bottom shows the position of the cursor in metres and a short hint for the active tool. If a tool cannot do what you asked, the reason shows there in amber for a few seconds instead of in a dialog.

The Scale tool needs a plan image. The Wall, Opening, Room and Detect tools also need a scale, because without one a click cannot be turned into metres. Until the scale is set, their buttons are greyed and their tooltips say "Set the scale first (S)". While the command line is editing the project, the Scale, Wall, Opening, Room and Detect tools are disabled and you can only select. You can still click to look at things.

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

- Click a point, a wall, an opening or a room to select it. It turns amber, and the properties panel on the right shows it. Click on nothing, or press Esc, to clear the selection.
- Drag a point to move it. The walls at the point follow. The point snaps to the grid, and Shift keeps its move straight. It never snaps onto other points or walls. A whole drag is one undo step.
- Drag an opening along its wall to move it. See "Openings".
- Drag the blue ring in the middle of a selected wall to bend it. See "Curved walls".

**The properties panel.** With nothing selected, it shows the scale, the grid size and the ceiling height of the floor (change them and press Enter), and the number of points, walls and rooms. With a selection it shows:

| Selected | What you see and can change |
|---|---|
| A point | Its id, X and Y in metres (change a number and press Enter), the walls that end there, and its corner radius |
| A wall | Its id, the two points, its length along the wall, its thickness, its bend with a Straighten button, its own height, and the number of openings on it |
| An opening | Its id and wall, its type, shape, hinge and swing, its offset and sill, its width and height, and a Delete button |
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

## Openings

An opening is a cut through a wall: a door, a window, or a plain opening with nothing in it. It sits on one wall and cuts the wall's band, so the plan image shows through the gap.

### Placing openings

1. Press O, or click the Opening tool. It needs a scale.
2. The panel on the right shows what the tool will place. Pick the **Type** (Door, Window or Plain opening) and the **Shape** (see below). For a door, pick the **Hinge** and **Swing** as well. Width, height and sill are set here too.
3. Move the mouse over a wall. An amber outline of the opening follows it along the wall. It moves in steps of 5 cm. Hold Ctrl to place it freely.
4. Click. The opening is added and selected, so its page opens in the panel.

If the opening would not fit where the outline is, the outline is dashed and the status bar says why, for example "The opening overlaps o1 on wall w2." A click then does nothing. Move along the wall until the outline is solid.

The status bar hint reads "Click a wall to add an opening. Ctrl places freely. The wall view sets the height."

You can also add an opening from the wall view. Press O, then click on the wall face. See "The wall view".

**Type.** A door starts on the floor. A window starts higher up, at its sill height. A plain opening is a cut with no leaf and no glass, such as an archway. It starts on the floor.

**Default sizes.** Width, height and sill in the tool page are the defaults for the type you picked. Change one and it is kept for every opening you place later, and it is saved with the project. The built-in sizes are a door of 0.9 by 2.1 m, a window of 1.2 by 1.2 m starting 0.9 m up, and a plain opening the size of a door. The ceiling height for new floors is 3.0 m. A set of sizes that does not work together, such as a window that would end above the ceiling, is refused. Type, shape, hinge and swing belong to the tool and are not saved. Changing a default does not change openings that are already placed.

**Hinge and swing.** These matter for doors. A wall has a direction, from its first point to its second point. Stand on the first point and look at the second: the side on your right is the wall's right side. The Hinge says which jamb the door hangs on, the one near the wall's first point ("Start of wall") or the one near its second point ("End of wall"). The Swing says which side of the wall the door opens toward, Left or Right. On the plan a door is drawn as a leaf, a line from the hinge jamb square to the wall, and a quarter arc from the end of the leaf to the other jamb. If the door opens the wrong way, change Swing or Hinge in the panel. You do not have to delete the door.

### Shapes

The shape is the outline of the cut. The built-in shapes are:

| Shape | What it looks like | Size it needs |
|---|---|---|
| Rectangle | The whole box | Any |
| Round arch | Straight sides, then a half circle on top | Height at least half the width |
| Flat arch | A shallow curve on top that rises a sixth of the width | Height at least a sixth of the width |
| Pointed arch | Two arcs meeting in a point on top | Height at least 0.866 times the width |
| Circle | An ellipse that touches the four sides of the box, a circle when width equals height | Any |

A shape that cannot take the opening's size is refused, and the status bar or the panel tells you the rule. You can add your own shapes. See [Writing generators](GENERATORS.md).

### Moving and editing an opening

- **Select** it with the Select tool (V) by clicking its gap. It turns amber and its page opens.
- **Drag it along its wall** in the plan. It moves in steps of 5 cm (hold Ctrl for free movement) and stays inside the free part of the wall. Dragging does not move it onto another wall. A whole drag is one undo step.
- **The panel** has the id and wall, type, shape, hinge and swing boxes, the offset (distance along the wall from its first point to the middle of the opening), the sill, the width and the height. Change a number and press Enter. A change that does not fit is refused and the old value comes back.
- **The wall view** sets height and size by dragging. See below.
- **Delete** with Delete, Backspace, Edit > Delete or the Delete button. One undo brings it back.

When the Shape, Type, Hinge or Swing box has focus, typing a letter jumps to the matching entry, as in any list. It does not switch tools.

### When an opening fits

An opening has to fit its wall. These are the rules, and Plynth checks them when you place, move or resize an opening, or change its type or shape:

- It must be inside the **free part** of the wall. The free part leaves out the rounded corner at each end, and half the thickness of the thickest other wall that meets the wall there. A wall end that is free, or that simply continues in a straight line, needs no clearance.
- Its top, the sill plus its height, must be at most the wall's height.
- It must not overlap another opening on the same wall. A window above a door is fine, and so are two openings that only touch.
- Its shape must be able to take its size.

If an edit you make to a wall breaks a rule afterwards, such as moving a point so the wall gets shorter, bending the wall or rounding a corner, the edit is allowed and the opening is kept. It is then drawn with a **dashed amber outline** on the plan and in the wall view, and its panel shows the reason in amber. Drag it back inside, resize it or delete it. You are never blocked from reshaping walls.

## The wall view

The wall view shows one wall face, front on and at true scale, so you can set heights.

- It appears under the plan when a wall or an opening is selected. Press **E**, or use View > Wall view, to hide or show it.
- It shows the face of the wall as seen from the side you clicked. The header says which wall, how long, how high, and "seen from the right" or "seen from the left". Press **F**, click **Flip**, or use View > Flip wall view to see the other side. Seen from the right side, the wall's first point is on the left of the picture.
- One metre looks the same size across and up, so a 0.9 by 2.1 m door has the right proportions.
- A ruler on the left shows heights every 0.5 m, with a label every metre. The floor is the line at the bottom. When the wall's height is not the ceiling height, a dashed line shows the ceiling.
- The parts of the wall near its ends that openings cannot use are shaded.
- Openings are cut out of the face and drawn in their real shape. The selected one is amber. An opening that does not fit is dashed amber.
- For the selected opening the view writes its width by height, its sill, and its distance from the left end of the free part, in numbers.
- A bent wall is shown unrolled along its curve. The note "Curved wall, shown unrolled" says so.
- Zoom with the wheel, pan with the middle button, and press Home to fit the wall again.

**Editing in the wall view.**

- **Move:** drag an opening. Left and right change its place along the wall, up and down change its sill. Hold Shift to move in one direction only, whichever you drag further. Hold Ctrl to move freely. Without Ctrl, moves snap to 5 cm.
- **Resize:** select an opening and drag one of its eight handles, the four corners and the four edge middles. The opposite edges stay where they are. An opening cannot get smaller than 0.1 m, cannot go past the free part of the wall, and cannot go above the wall's top.
- **Add:** with the Opening tool active, click on the wall face. The opening is added there at the default sill.
- Every drag is one undo step. While a command line session is editing the project, the wall view is read only.

The plan sets where an opening is along the floor, the wall view sets how high and how big. Both show the same plan, so a change in one appears at once in the other.

## Heights

- **Ceiling height** belongs to a floor. With nothing selected, the properties panel has a "Ceiling height" box. Walls use it unless they have their own height.
- **Wall height.** Select a wall and tick "Own height" in the panel, then type a height. A half wall that is 1.1 m high is made this way. Untick it to follow the ceiling again.
- A height that would leave an opening above the top of a wall is refused, and the message names the opening. Lower or remove the opening first.

## Curved walls

Any wall can be bent into a true circular arc.

1. Select the wall with the Select tool. A blue ring appears in the middle of its curve.
2. Drag the ring. The wall bends and the room fills follow it. Drag it to the other side of the wall and the bend goes the other way. Near the straight line it snaps back to straight. Otherwise it rounds to 1 cm.
3. Release. The whole drag is one undo step.

You can also type a number in the **Bend** box in the panel. It is the distance in metres from the middle of the straight line to the middle of the arc. Positive bends the wall to its right side, negative to its left. **Straighten** makes the wall straight again with one click.

Limits: the largest bend is a half circle, a bend of half the straight distance between the wall's points. A wall also cannot be bent so tightly that the radius of its curve is smaller than half its thickness. Such a bend is refused with a message. The same limits are checked when you move a point or change the thickness of a bent wall.

On a bent wall, lengths and the places of openings are measured along the curve. The length shown in the panel is the length along the arc. You can place doors and windows on a bent wall like on any other. You can cut a bent wall in two with a T junction, and both halves stay on the same arc. Rooms can be closed by curved walls, and their area follows the curve.

## Rounded corners

A corner can be rounded with an arc. Select a point and type a **Corner radius** in the panel. 0 means a sharp corner.

A corner is rounded only where exactly two straight walls meet at an angle between 1 and 179 degrees. At a point with one wall, with three or more walls, or with a curved wall, the radius is kept but nothing is rounded. The panel says "Not rounded: needs exactly two straight walls". When the walls are too short for the radius, the arc is made smaller and the panel says for example "Rounded at 0.50 m". The arc takes away at most half of each wall.

The wall bands meet in the middle of the arc, so the corner has no notch. Room fills and areas follow the rounded corner. Openings keep clear of the rounded part of a wall.

## Detecting walls

Instead of clicking every wall and opening, you can let Plynth look at the plan image and propose the walls, the openings and the rooms. You check the result, tune it with sliders, and accept it. You can always draw walls by hand instead, or to finish what detection missed.

**What you need.** A plan image and a scale. Set the scale first (S).

**Which plans work best.** Clean plans where the walls are dark and solid, on light paper. Right now Plynth finds horizontal and vertical walls only. A scan that is a few degrees crooked is fine. Diagonal walls and curved walls are not found, so draw those by hand. Plans where walls are drawn as two thin outlines with nothing between them, or very noisy or textured scans, give poor results. Thin marks such as text, dimension lines, door arcs, window lines and furniture outlines are ignored on purpose.

**Using it.**

1. Press D, or click the Detect tool. The panel on the right shows the detection settings, and Plynth starts looking at once.
2. After a moment the proposals appear over the plan as dashed blue outlines: walls with a faint blue fill, hollow blue rings at corners and junctions, and a dashed blue outline for each room it found. Doors, windows and gaps show as two small blue ticks across the wall, with a leaf and a quarter arc for a door and two thin lines for a window. The door arc starts at the hinge side Plynth found and bends toward the side the door opens, so it matches the arc in the plan image. Openings that are already in the plan are not drawn again.
3. Under the settings, a line says what was found, for example "16 walls, 5 rooms, 9 openings in 85 ms". It also tells you when some walls are already drawn, or when some overlap walls you drew and are left out.
4. Move the sliders until the blue walls match the real ones. The preview follows about a fifth of a second after you stop. The app stays responsive while it works.
5. Press Enter, or click Accept. The walls, the openings and the rooms are added to the plan in the normal style. The status bar says, for example, "Added 16 walls, 9 openings and 5 rooms". One Ctrl+Z takes all of them away again, and the proposals come back so you can try other settings.

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
| Find openings | Find doors, windows and gaps, and add them to the plan when you accept. Untick it to find walls only. |

**Reset settings** puts every setting back to its default.

**Running it again never doubles walls.** Detection compares what it finds with the walls you already have. Walls that are already in the plan, whether you drew them or accepted them earlier, are not shown and are never added a second time. The status line counts them. If a proposed wall runs partly along a wall you drew, it is left out and counted, not trimmed. Draw the missing piece by hand. New walls snap onto the points and walls you already have, so detected walls join hand drawn ones.

**Openings are added.** Accept adds every new door, window and gap as an opening on the wall that holds it. A door gets the hinge and swing Plynth found in the image. Its height and sill come from the default sizes (see "Placing openings"), so a door is the default door height and a window the default window size. An opening that hangs a few centimetres over the free part of its wall is moved, and narrowed if needed, so it fits. One that still does not fit is left out. Each added opening is a normal opening, so you can move, resize, change or delete it. While detecting, the walls are closed across the gaps so every room stays a room, and the openings then cut them again. One Ctrl+Z takes the walls, the openings and the rooms away together. Running detection again does not add an opening twice.

If detection cannot run, the panel says why in amber, for example when the plan has no scale or the image could not be read. While a command line session is editing the project, the Detect tool is off.

## Deleting

Select something and press Delete or Backspace, use Edit > Delete, or use the Delete button under the selection in the properties panel. One undo brings everything back.

Delete takes with it whatever cannot stay without the thing you deleted:

- Deleting a point deletes the walls that end at it, the openings on those walls, and the rooms that use those walls.
- Deleting a wall deletes the openings on it and the rooms that use it.
- Deleting an opening deletes only the opening.
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

### Older projects

Projects made with earlier versions of Plynth open as they are and upgrade by themselves. Nothing is asked and nothing is lost: doors and windows you already had get a hinge on the start of the wall and a swing to the right, and the project gets the default sizes. The files are written in the new format the next time you save. A project saved by this version cannot be opened by an older version of Plynth, so keep a copy if you still need that.

### Autosave and recovery

Every 30 seconds, if there are unsaved changes, Plynth writes a copy into the project's `autosave/` folder. Saving or choosing Don't save removes it. Untitled projects have no folder, so they are not autosaved.

If Plynth was closed unexpectedly, the next time you open that project it asks: "This project has unsaved changes from <date and time>. Recover them?" If the saved plan is newer than the copy, the question tells you. Recover loads the copy as unsaved changes, so you still decide whether to save. Discard removes it.

## Setup check and the library

Before you import models, Plynth needs Blender, a library folder and a projects folder. **Help > Setup check** shows what is ready and what is not, and it opens by itself on the first run. Plynth never installs anything: if Blender is missing, you install it from blender.org yourself. Every item is explained in [Setting up Plynth](SETUP.md).

The **library** is one folder on your computer that holds your assets catalog. It is shared by all projects. Choose or create it in the setup check. **Catalog > Open library folder** opens it in Explorer.

## The assets catalog

The catalog is the list of 3D models you can use in your plans, such as chairs, tables and lamps. You bring your own model files. Plynth cleans each one up so they all behave the same way, draws a thumbnail, and keeps them in the library.

Open it with **Catalog > Assets catalog** (Ctrl+Shift+A), or click the **Assets** tab at the top of the right panel. The other tab, **Properties**, is the panel you already know. With no library set, the Assets tab says "No library folder is set." and has an **Open setup check** button.

### Finding assets

At the top of the Assets tab:

- **Search assets.** Type part of a name, tag or category. The grid filters as you type, after a short pause. Typing `cha` finds "Chair" and "Armchair". Several words must all match. Accents are ignored.
- **Room.** "All rooms", or one kind of room. Only assets in categories that belong in that room are shown. A living room shows sofas and lamps, a bathroom shows baths and mirrors.
- **Category.** "All categories", "Uncategorized" (assets you have not sorted yet), or one category. After you pick a room, the list holds only that room's categories.

Under the controls, a count says how many assets match. Each tile shows the thumbnail, the name, and the size in metres as width x depth x height, for example `0.45 x 0.50 x 0.90 m`.

A tile can say something other than a picture:

- **Rendering** (grey): the thumbnail is being drawn.
- **Render failed** (amber): Blender could not draw the thumbnail. The model is fine and can still be used. Select it and press **Retry render**.
- **Broken** (amber): the asset's folder cannot be read. Select it to see why.

Plynth never shows a stand-in picture. A thumbnail is always a real picture of the model.

### Importing

You can bring in models three ways.

1. **The Import button** (or **Catalog > Import asset...**, Ctrl+I). A file dialog opens. You can pick several files at once.
2. **Drag and drop.** Drop files, zips or folders on the Assets tab. This is also the way to import a folder.
3. **The inbox folder.** Copy files into `Library\assets\inbox\`. Plynth notices within a few seconds and imports them, while it is running. If Plynth is closed, `plynth assets inbox` imports what is waiting.

Plynth reads `.blend`, `.fbx`, `.obj`, `.gltf` and `.glb` files, zips that hold one model, and folders that hold one model. A zip with more than one model is refused, and the message lists them.

Each import joins a list under the grid. A row shows the name, what Blender is doing, a progress bar and a **Cancel** button. Imports run one after another. When one finishes, the new asset is selected, and the status bar says "Imported Chair. Pick its categories." Tick its categories so it shows up in the right rooms.

If an import fails, its row stays in the list with the reason. **Show log** opens the folder with Blender's log when there is one, and **Dismiss** removes the row. Nothing is left in the library after a failed import.

**What happens to the original.** Files and folders you pick are never touched. A zip that was imported goes to the Windows Recycle Bin, so you can get it back, unless you turned on "keep imported zips" (`plynth settings set --keep-zips true`). Files in the inbox leave it once they are handled: imported ones move to `inbox\done`, failed ones move to `inbox\failed` with a text file that says why.

### What the cleanup does to a model

Models from different places differ in size, orientation and where their middle is. So Blender cleans each model the same way before it is saved in the library:

- **Metres.** Sizes are in metres. If a file used other units, Plynth warns you (see below) and you can import again with a scale.
- **The pivot is at the bottom centre.** The point a model is placed by is the middle of its footprint, on the floor. A chair then stands on the floor where you put it.
- **The front faces -Y.** That is Blender's front view. Plynth cannot see which side of a model is its front, so it assumes the file's front. If a model faces the wrong way in its thumbnail, import it again with `plynth assets import <file> --rotate 90` (or 180 or 270).
- **Transforms are applied.** Every object has its scale and rotation baked in, so nothing is hidden.
- **One collection, tidy names.** All objects sit in one collection named after the asset id, named in order, such as `chair_01`, `chair_02`.
- **Cameras, lights and empties are removed.** Objects that hang from them stay where they were.
- **Textures are copied.** Every picture a model uses is copied into the asset's own `textures` folder and linked by a relative path, so the asset folder is complete on its own. Pictures packed inside a `.blend` are unpacked there. A texture that cannot be found is a warning.

Your original file is not changed. The cleaned model is saved as `asset.blend` in `Library\assets\items\<id>\`, with `thumb.png` and an `info.json` that holds the name, categories, tags and license.

### Warnings

Problems with a model never stop an import. They show in amber under the asset's details and in the `warnings` list of the command line:

- **Very high triangle count.** More than 500 000 triangles. Large models slow everything down. You can change the limit with `plynth settings set --triangle-warning`.
- **Suspicious size.** The largest side is above 30 m or below 0.02 m, which usually means the file used other units. Import again with `--scale` (for example `--scale 0.01` for a model drawn in centimetres).
- **Missing texture.** A picture the model asked for was not found, so that part has a plain color.
- **Removed objects.** Cameras, lights or empties were left out.
- **Eevee failed.** The thumbnail was drawn with the slower Cycles engine, because your graphics driver could not run the fast one.

### Thumbnails

A thumbnail is a 512 pixel render made by Blender: the model seen from the front right and a little above, on a dark background, with soft lights. It is drawn once, when the model is imported.

If it fails, the tile says "Render failed". Select the asset and press **Retry render** in its details. This also measures the model again. **Retry render** is also what to use after you change `asset.blend` in Blender yourself.

### Details, and editing an asset

Click a tile to see the asset's details under the grid: a larger thumbnail, the name, the id and size in metres, the triangle count, where it came from, and the warnings. Every edit is saved at once.

- **Name.** Type a new name and press Enter.
- **License.** Pick one from the list: Unknown, CC0, CC BY 4.0, Royalty free, Personal use only or Own work. Plynth does not check it, so set it from the terms you got the model under.
- **Categories.** Tick each category the asset belongs to. This decides which rooms show it.
- **Tags.** Words that help search, separated by commas. Lower case letters, digits, `_` and `-`.

Buttons at the bottom of the details:

- **Retry render**: measure the asset again and draw a new thumbnail.
- **Open folder**: open the asset's folder in Explorer.
- **Remove**: delete the asset and its folder from the library. Plynth asks first. This cannot be undone.

Catalog edits are not plan changes, so Undo does not undo them.

### Rescan

**Catalog > Rescan library**, or the **Rescan** button, reads the library folder again and updates the index. Use it after you add, remove or change asset folders by hand. Plynth also watches the library while it runs and rescans by itself a couple of seconds after the folders change.

A rescan:

- adds folders that are new and drops rows whose folder is gone;
- makes an `info.json` for a folder that has only an `asset.blend`, as an uncategorized asset;
- measures and redraws again any asset whose `asset.blend` changed;
- lists folders it cannot read as Broken, and still loads the rest.

The files in `items` are the real catalog. If the index is lost, a rescan builds it again.

### Room types and categories

**Catalog > Room types and categories...** opens a window where you shape the lists.

- **Room types** (left): Add, Rename, Remove, and Up and Down to change the order the room picker shows. Select one and tick the categories that belong in it, on the right of the window.
- **Categories**: Add, Rename and Remove.

Changes are saved as you make them. A new room type shows up in the room picker of the Properties panel at once, so you can give it to a room in your plan.

Removing a category takes it off every asset that has it. Removing a room type leaves plans that use it alone: they keep its id and show the id as the label. There are nine room types and 24 categories to start with, and you can change all of them. Room types and categories live in the library's index, so they are shared by all projects.

## When a plan image is missing

If the file of a plan image has been moved or deleted, Plynth still opens the project and still lets you edit and save it. The image path stays in the plan. After a save, the status bar shows in amber: "The plan image plans/floor1.png is missing. Its path was kept." Put the file back in the project's `plans` folder, or set another image, and the warning goes away. The plan view shows "The plan image ... could not be opened." until then.

## When the command line is editing

A script or a coding assistant can edit the project while Plynth has it open. To keep your work and its work apart, it first begins an edit session. While one is active:

- A banner under the title bar says "Being edited from the command line by <name>. Changes appear here as they are saved."
- The plan is dimmed. Undo, Redo, Save, Save as and Set plan image are disabled. New project and Open project still work.
- The Scale, Wall, Opening and Room tools, Delete, dragging in the plan and the wall view, and every box in the properties panel are disabled. You can still click to select and look.
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
- It starts your shell in the project folder. With no project open it starts in your projects folder from the setup check, or in your Documents folder when none is set. It uses PowerShell 7 if installed, else Windows PowerShell, else the Command Prompt.
- `plynth` works there without a path, so you can run `plynth project info` or `plynth session begin` straight away.
- Drag the top left corner of the panel to resize it.
- Closing or switching the project ends the shell. If a program is still running, Plynth asks first: "A program is still running in the terminal. Closing the project will stop it."
- If the shell exits by itself, the panel says "The shell ended. Press Enter to start a new one."

While the terminal has focus, nearly every key goes to the shell, including Ctrl+S, Ctrl+Z and Ctrl+O. They do not save, undo or open anything in Plynth until you click outside the terminal. Ctrl+C copies when text is selected and otherwise stops the running program. Ctrl+V pastes, and Shift+PageUp and Shift+PageDown scroll back. The full list is in the [shortcuts](SHORTCUTS.md).

The terminal shows program output in its own set of colors, so colored text from tools looks right.
