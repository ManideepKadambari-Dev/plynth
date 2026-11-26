# User guide

This covers what Plynth does today: the home screen, opening a project, showing its plan image, setting the scale, drawing walls (straight and curved, with rounded corners), placing doors, windows and other openings, setting heights, adding floors, stairs and roofs, finding walls and openings in the image automatically, making rooms, placing furniture from the library, undo and redo, saving, building the building in Blender and looking at it in 3D, the setup check, the library, the assets catalog and the materials catalog, putting materials on walls, rooms and the whole project, working next to the command line, the built-in terminal, generators, and the help pages.

New here? Start with [Getting started](GETTING_STARTED.md). For keys see [Keyboard shortcuts](SHORTCUTS.md). For scripts see the [Command line guide](CLI_GUIDE.md). For Blender, the library folder and the first run see [Setting up Plynth](SETUP.md).

## The home screen

The home screen is the first thing you see when Plynth starts without a project. It also shows when you close a project, and when you choose **File > Home** (Ctrl+Shift+H). On the very first run the setup check opens on top of it.

**Recent projects** fill the right side. Each row shows a small picture of the plan, the project name, its folder and when you last opened it.

- Click a row to open that project. If you have unsaved changes in the project that is open now, Plynth asks about them first.
- **Show folder** opens the project folder in Explorer.
- **Remove** takes the project off the list. Nothing on disk is deleted.
- Type in **Search projects** to filter the list by name or folder.
- A project whose folder is gone shows as **Missing**, in amber, with "Folder not found". It cannot be opened. Press **Remove** to clear it, or put the folder back and it opens again.

Projects join the list when you open or save them in the app. The list keeps the last 30. It belongs to this computer and is kept in the settings file, not in the project. `plynth project recent` prints the same list for scripts.

**The left side** has:

- **New project** and **Open project**, the same as the File menu.
- **Help**: the help pages, such as Getting started, User guide, Keyboard shortcuts and the CLI developer guide. Each opens in the Help window. See "Help".
- **Sample plans**: the sample plan images that come with Plynth. Click one to start a new project with it as the plan image of floor 1, a quick way to try the tools. The New project window opens first, as for **New project**.
- **Sample projects**: whole projects to open and look around in. See "Samples".
- **Setup status**: "Setup is ready.", or in amber what needs attention, with a **Fix** button that opens the setup check. See [Setting up Plynth](SETUP.md).
- **Projects folder**: the folder where new projects go. Click it to open it.

When a project is open and you came here with File > Home, a **Back to** button returns to it. The project stays open behind the home screen and nothing is saved or closed.

## Projects

A project is one folder:

```
MyApartment/
  project.json      settings, export mode, grid size
  plan.json         all marks
  export-options.json  your group edits and the modular kit switch (only once you change one)
  plans/            floor1.png, floor2.png
  build/            building.blend, building.fbx, build.json, blender.log
  export/           FBX files, Textures/, reports
  renders/          images for checking results
  autosave/         a recovery copy of unsaved work
```

Only `plan.json`, `project.json` and `plans/` are the real project. `build/`, `export/`, `renders/` and `autosave/` can be deleted at any time; Plynth recreates them. Today `build/` and `autosave/` are used as well. `build/` is made by the Build button.

Two small files, `session.lock` and `app-state.json`, may also appear. They are explained below and are safe to delete while Plynth is closed.

### Create and open

- **File > New project...** (Ctrl+N), or **New project** on the home screen, opens the New project window. Type a **Name** and choose the **Location**, the folder the project folder goes in. It starts at your projects folder, and **Browse...** picks another. The window shows the folder it will make. **Create** makes that folder, saves the new project in it and opens it, so **Save** never asks for a folder later. **Cancel** leaves the open project as it was. Create stays off, with a sentence that says why, while the name cannot be a folder name (it is empty, holds one of `\ / : * ? " < > |`, ends with a dot, or is a name Windows keeps, such as CON) or while the location already has a folder of that name with files in it.
- **File > Open project** (Ctrl+O) asks for a project folder. You can also pick one from the recent list on the home screen.
- **File > Close project** (Ctrl+W) closes the project and goes to the home screen. If there are unsaved changes it asks "Save changes to <name>?" first, with Save, Don't save and Cancel.
- **File > Home** (Ctrl+Shift+H) goes to the home screen and keeps the project open behind it.

You can also open a folder from a terminal: `plynth MyApartment`.

A new project always has its folder. Starting a new project with unsaved changes asks "Save changes to <name>?" first, after you press Create.

## The plan image

Right-click a floor tab under the plan, such as **Floor 1**, and choose **Set plan image...**, or **Replace plan image...** when the floor has one. Then pick a PNG, JPEG or BMP. The image is set on that floor, and that floor is shown. You can undo it. Ctrl+Shift+O and dropping an image file on the window set the image of the floor that is shown. A floor with no image says "Right-click the floor tab to set a plan image" in the middle of the plan.

Plynth fits the image to the window. Zoom with the mouse wheel, pan with the middle button or with Space and the left button. Ctrl+0 fits again and Ctrl+1 shows 100 percent. The full list is in the shortcuts page.

Once the scale is set, points and walls are drawn over the image: walls as blue bands of their real thickness, points as small blue dots, openings as gaps in the bands, rooms as a faint blue fill with a name and an area.

If the image file is missing, the sheet shows "The plan image ... could not be opened." You can still edit and save. See "When a plan image is missing" below.

## The tools

A strip of buttons sits at the left edge of the plan. The active tool is the one in blue. Each button has a tooltip with its name and key.

| Tool | Key | What it is for |
|---|---|---|
| Select | V | Pick points, walls, openings, furniture and rooms, drag points, openings and furniture, bend walls |
| Scale | S | Tell Plynth how long something on the image really is |
| Wall | W | Draw walls |
| Opening | O | Place doors, windows and other openings on walls |
| Room | R | Turn a closed area into a room |
| Stair | T | Place a stair that climbs to the next floor up |
| Furniture | P | Place furniture from the library |
| Detect | D | Find walls, openings and rooms in the plan image for you |

The status bar at the bottom shows the position of the cursor in metres and a short hint for the active tool. If a tool cannot do what you asked, the reason shows there in amber for a few seconds instead of in a dialog.

The Scale tool needs a plan image. The Wall, Opening, Room, Stair, Furniture and Detect tools also need a scale, because without one a click cannot be turned into metres. Until the scale is set, their buttons are greyed and their tooltips say "Set the scale first (S)". The Furniture tool also needs a library folder, and says "No library folder is set." without one. While the command line is editing the project, the Scale, Wall, Opening, Room, Furniture and Detect tools are disabled and you can only select. You can still click to look at things.

## The side panel and the app's windows

**The Properties panel** on the right of the plan shows the properties of what is selected, or of the tool in use, or of the project when nothing is selected. It is the only panel on that side.

- **Hide** at the top of the panel closes it, and the plan takes the space. **View > Properties panel** (Ctrl+Shift+P) or the **Properties** button at the right end of the bar above the plan opens it again.
- Drag the edge between the plan and the panel to make it wider or narrower.
- Plynth remembers whether the panel was open and how wide it was, and opens it the same way next time.

**The app's own windows.** The catalogs and the build and export pages each open in a window of their own, so you can keep them beside the plan or move them to a second screen:

| Window | Opens with | What it holds |
|---|---|---|
| Assets | **Catalog > Assets catalog** (Ctrl+Shift+A) or **Assets** in the bar | The assets catalog, its import queue and the details of an asset |
| Materials | **Catalog > Materials catalog** (Ctrl+Shift+M) or **Materials** in the bar | The materials catalog and the details of a material |
| Sources | **Catalog > Sources and integrations** (Ctrl+Shift+I) or **Sources** in the bar | BlenderKit, Fab and your own files |
| Import materials | **Catalog > Import materials...** or **Import** in the Materials window | The screen that imports textures and .blend materials |
| Build report | **Build > Build report window** (Ctrl+Shift+B), **Report** in the bar, or the amber problems button | The last build: summary, problems, files and steps |
| Export | **Build > Export window** (Ctrl+Shift+X) or **Export** in the bar | The export for Unreal, its groups and the quality checks |

- There is one window of each kind. Opening it again brings it to the front.
- Each window remembers its size, its place and its screen. If that screen is gone, it opens over the main window.
- The windows have the same title bar as the main window: drag it to move, double click to maximize, and drag an edge to resize.
- The shortcuts with Ctrl, Alt or a function key, such as Ctrl+Z, Ctrl+S, Ctrl+B and F1, work in these windows too.
- Closing the main window closes them all.
- Everything in them works as it did when they were tabs of the side panel.

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

- Click a point, a wall, an opening, a piece of furniture or a room to select it. It turns amber, and the properties panel on the right shows it. Click on nothing, or press Esc, to clear the selection.
- Drag a point to move it. The walls at the point follow. The point snaps to the grid, and Shift keeps its move straight. It never snaps onto other points or walls. A whole drag is one undo step.
- Drag an opening along its wall to move it. See "Openings".
- Drag a piece of furniture to move it, or the round handle beyond its front edge to turn it. See "Editing furniture".
- Drag the blue ring in the middle of a selected wall to bend it. See "Curved walls".

**The properties panel.** With nothing selected, it shows the scale, the grid size, the ceiling height of the floor and the wall item centre (change them and press Enter), and the number of points, walls and rooms. With a selection it shows:

| Selected | What you see and can change |
|---|---|
| A point | Its id, X and Y in metres (change a number and press Enter), the walls that end there, and its corner radius |
| A wall | Its id, the two points, its length along the wall, its thickness, its bend with a Straighten button, its own height, and the number of openings on it |
| An opening | Its id and wall, its type, shape, hinge and swing, its offset and sill, its width and height, and a Delete button |
| A room | Its id, its type, its area, and the number of walls |
| A piece of furniture | Its thumbnail, name and size, its position, turn and heights, the room or wall it is on, and buttons to swap or mount it. See "Editing furniture". |

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
- Furniture that hangs on the face is drawn as its front box, with its name. See "Wall-mounted items".
- A bent wall is shown unrolled along its curve. The note "Curved wall, shown unrolled" says so.
- Zoom with the wheel, pan with the middle button, and press Home to fit the wall again.

**Editing in the wall view.**

- **Move:** drag an opening. Left and right change its place along the wall, up and down change its sill. Hold Shift to move in one direction only, whichever you drag further. Hold Ctrl to move freely. Without Ctrl, moves snap to 5 cm.
- **Resize:** select an opening and drag one of its eight handles, the four corners and the four edge middles. The opposite edges stay where they are. An opening cannot get smaller than 0.1 m, cannot go past the free part of the wall, and cannot go above the wall's top.
- **Add:** with the Opening tool active, click on the wall face. The opening is added there at the default sill.
- Every drag is one undo step. While a command line session is editing the project, the wall view is read only.

The plan sets where an opening is along the floor, the wall view sets how high and how big. Both show the same plan, so a change in one appears at once in the other.

## Placing furniture

Furniture comes from the library, so import some models first. See "The assets catalog". You need a library folder, a plan image and a scale.

1. Press **P**, or click the Furniture tool. The hint reads "Click to place furniture. Hover an asset to see it."
2. Click on the plan where you want the item. A small amber cross marks the spot, and the picker opens next to it.
3. In the picker, pick from the lists, left to right. Each list opens to the right of the one before, or to the left when the window edge is near.
4. Click an asset. It is placed at the cross, selected, and the picker closes.

Esc, or a click outside the picker, closes it and places nothing.

**The picker** has three lists and a preview:

- **Room types.** If you clicked inside a room that has a type, the picker starts at that room's categories, because a bedroom needs beds and not baths. A row at the top, **All room types**, goes back to the list of all types. If you clicked outside every room, or in a room with no type, it starts with the list of room types.
- **Categories.** Each shows how many assets it holds. A category with none is greyed.
- **Assets.** Each shows its name and its size in metres, for example `2.00 x 0.90 x 0.85 m`. An asset that is broken, or that has not been measured, is greyed and its tooltip says why. Use **Retry render** in the Assets window to measure an asset that says "not measured".

**The hover preview.** Point at an asset without clicking. A panel beside the list shows its thumbnail, its name, its real size, its categories and its license. At the same time the item's footprint is drawn on the plan at the cross, at its true size. The footprint is drawn in amber when the item would have a problem there. See "Furniture warnings". The preview is the live 3D model once it is drawn, with the thumbnail shown until then. The thumbnail is always a real picture. While one is still being drawn, the panel says "Rendering", and when it failed, "Render failed".

**Dragging from the Assets window.** You can skip the picker. Drag a tile from the Assets window and drop it on the plan. The item is placed where you drop it, snapped like a drag. Hold Ctrl while you drop to place it freely. Drop it on the wall view to hang it on the wall face. An asset that is broken or not measured cannot be dragged. It works with any tool active.

You can place the same asset as many times as you like. Each item has its own id, `i1`, `i2` and so on. One undo removes the last item.

## Editing furniture

On the plan, an item is drawn as its footprint: a rectangle at true size and true turn, filled softly, with a short tick on its **front** edge. The name is written inside when it fits at the zoom you have. Click an item with the Select tool to select it. It turns amber and shows a round handle beyond its front edge.

**Moving.** Drag the item. The whole drag is one undo step. The item is snapped as you drag:

1. Its centre goes to the grid, when Snap to grid (G) is on.
2. Then each side of its footprint that runs along a wall, and is close to the wall face, slides flush against it. Near a corner it touches both walls, so a sofa slides into the corner.

Hold **Ctrl** to move freely with no snapping. The snap distance is about 10 screen pixels, the same as for walls.

**Turning.** Drag the round handle. The item turns about its centre in steps of 15 degrees. Hold **Shift** to turn freely. The keys **]** and **[** turn the selected item 90 degrees clockwise and counterclockwise, and so do the **Turn right** and **Turn left** buttons in the Properties panel and the Edit menu. Turning is clockwise on the plan. At 0 degrees the front faces down the plan. Each drag, and each key press, is one undo step. A wall item cannot be turned, because it faces away from its wall. Take it off the wall first.

**The Properties panel** for an item shows:

| What | Meaning |
|---|---|
| Thumbnail, name | The asset, with its id in mono type below. |
| Size | Width, depth and height from the asset. |
| X and Y | The centre of the item on the plan, in metres. Floor items only. |
| Rotation | In degrees. Floor items only, with the Turn left and Turn right buttons. |
| Bottom height | How far the bottom is above the floor. 0 for something on the floor. |
| Centre height | How far the middle is above the floor. It edits the same value as the bottom height. |
| Room | The room it stands in. Read only. It follows the item when you move it. |
| Wall | For a wall item, its wall, and **Take off wall**. For a floor item, **Mount on wall**. |
| Swap asset... | Pick another asset, see below. |
| Delete | Remove the item. |

Change a number and press Enter. Each box is one undo step per visit.

**Swapping.** Press **Swap asset...** (or Edit > Swap item asset...). The picker opens at the item, starting at its room's categories. Pick another asset. The new one takes the item's place, turn, id and room. Its size is the new asset's size. A wall item stays flat on its wall and keeps the height of its centre. Esc cancels. Swapping works for a missing asset too, and is how you fix one.

**Deleting.** Select the item and press Delete or Backspace, or use Edit > Delete or the Delete button. One undo brings it back. Deleting a room does not delete the furniture in it. The items stay and show no room. Adding a room gives it the items that stand inside it and have none, so rooms found by detection pick up their furniture. Deleting a wall does not delete the wall items on it either. They stay where they were, as floor items. See "Deleting".

## Wall-mounted items

Some things hang on a wall: a TV, a picture, a shelf. A wall item stands flat against the face of its wall, facing away from it. On the plan it is a thin rectangle on the wall face, with a line on its back edge.

**Putting one on a wall.**

- In the **wall view**, press P and click on the wall face. The picker opens. Pick an asset and it is hung at the place you clicked, with its centre at the **wall item centre**.
- Or drag an asset tile from the Assets window onto the wall view.
- Or place it as any item, drag it close to a wall, select it and press **Mount on wall** in the Properties panel (or Edit > Mount item on wall). It goes on the nearest wall within about 30 cm. If no wall is near, the status bar says "No wall is near this item. Move it next to a wall first." The face is the side the item is on.
- **Take off wall** reverses it. The item keeps its place, turn and height, and becomes a floor item.

Nothing is mounted by itself. A TV can stand on a cabinet as a floor item, with its bottom height set to the cabinet's height.

**The wall item centre.** A new wall item is hung with the middle of the item at 1.2 m above the floor, so a TV is at a natural height. The number is in the Properties panel when nothing is selected, as **Wall item centre**. It is saved with the project. Changing it does not move items already placed. A value at or above the ceiling height is refused.

**Plan view sets the place, wall view sets the height.** In the plan, dragging a wall item slides it along its wall and keeps it flat. In the wall view the item is drawn as its front box on the wall face. Drag it up and down and along the wall. It snaps to 5 cm, and Ctrl moves it freely. While you drag, a dashed line shows the wall item centre height. Centre height in the Properties panel follows as you drag.

A wall item has to fit its wall. It is refused when its top would be above the wall or when it is wider than the wall. A wall item is also checked against openings: hanging a TV across a window is a warning.

**When walls move.** Wall items do not follow when you move a wall point. A wall item that is no longer on its wall face shows the warning "off wall". Drag it once, or press Mount on wall again, and it is put flat against the wall. Cutting a wall in two with a T junction moves the items past the cut to the new part.

## Furniture warnings

Plynth tells you about problems with furniture, but does not stop you. Real rooms have a rug under a table and a sofa against a radiator, and a drag that kept refusing would feel stuck. A problem draws the item's outline dashed in amber, and the Properties panel lists it in a sentence.

| Warning | What it means |
|---|---|
| Not in any room | The middle of the item is outside every room. |
| Outside its room | Part of the footprint sticks out of its room. |
| Overlaps another item | Two items occupy the same space. Items stacked at different heights, like a TV above a cabinet, are fine. |
| Overlaps a wall | A floor item is inside a wall. Move it out. |
| Overlaps an opening | A wall item hangs across a door or window. |
| Off its wall | A wall item is no longer flat against its wall. |
| Too tall | The item reaches above the ceiling, or a wall item above its wall. |
| Missing asset | The asset is not in the library. See below. |
| Size in library differs | The asset's size in the library is not the size stored in the item. See below. |

What is **refused**, with a message in the status bar, is a change that cannot be right: an asset that is broken or not measured, a wall item that would reach past its wall, a turn of a wall item. Nothing changes then. A script can choose to be stopped by warnings too, with `--strict`. See the [Command line guide](CLI_GUIDE.md).

## Missing assets

An item remembers its asset's id and its size. If you remove the asset from the library, or open the project on a computer with another library, the item is not lost. It keeps everything and is saved unchanged. It is drawn with a dashed amber outline, at its stored size, with the words "Missing asset" and its id. You can still move it, turn it, swap it and delete it.

To fix it, put the asset back in the library, or select the item and use **Swap asset...** to pick another one. Without a library open, Plynth cannot tell which assets are missing, so it does not show the state.

**Size in library differs.** Items store the size their asset had when you placed them. If you measure the asset again and it comes out different, for example after changing the model in Blender, the item keeps its old size and says "Size in library differs". Press **Update size** in the Properties panel (or Edit > Update item size) to take the new size. Nothing changes silently.

## Heights

- **Ceiling height** belongs to a floor. With nothing selected, the properties panel has a "Ceiling height" box. Walls use it unless they have their own height.
- **Wall height.** Select a wall and tick "Own height" in the panel, then type a height. A half wall that is 1.1 m high is made this way. Untick it to follow the ceiling again.
- A height that would leave an opening above the top of a wall is refused, and the message names the opening. Lower or remove the opening first.

## Floors

A project can have any number of floors. Each floor is a tab in a thin bar along the bottom of the plan, lowest floor on the left. The tab of the floor you are working on is blue. Everything you do, drawing, detecting, placing furniture and the Properties panel, works on that floor.

- **Switch floors:** click a tab, or press Ctrl+Page Up for the floor above and Ctrl+Page Down for the floor below (View > Floor above and Floor below).
- **Add a floor:** the **+** button at the right end of the bar adds an empty floor above the top one. Right click a tab and choose **Add floor below** to put one under it.
- **Rename:** double click a tab, or right click it and choose **Rename**.
- **Remove:** right click a tab and choose **Remove floor**. Plynth asks first, because the floor goes with its walls, rooms, openings, furniture, stairs and roof settings. Ctrl+Z brings it all back. The only floor cannot be removed.

Adding, renaming and removing a floor can all be undone.

### Heights and slabs

With nothing selected, the Properties panel has a **Floor** section: the name, the **Ceiling height**, the **Slab thickness**, the plan image and the image offset.

Floors are stacked on slabs. A slab is the thick floor between one ceiling and the floor above it. It is 0.2 m thick to start with, and you can set 0.05 m to 1 m. The level of a floor is the level of the floor below, plus that floor's ceiling height, plus its slab. With ceilings of 3.0 m and slabs of 0.2 m, the first floor is at 0 m and the second at 3.2 m. The height a stair has to climb is that same 3.2 m.

Walls keep their own heights. From outside, the slab shows as a band of the outside wall material between the storeys.

### The floor below

On every floor except the lowest, the walls of the floor below are drawn as a faint outline under your marks. Use it to see where the walls underneath are. It cannot be selected, so a click goes through it.

### Lining up the plan image

Every floor has its own plan image, but the whole plan has one scale. If the upper floor's drawing sits a little off from the lower one, move its image until the corners line up with the faint outline.

1. Show the upper floor, then turn on **View > Align plan image**.
2. Drag the image, or nudge it with the arrow keys: 1 cm for each press, 10 cm with Shift. The image moves, and the marks you traced on it move with it.
3. Turn the option off again when it fits.

You can also type the **Image offset X** and **Image offset Y**, in metres, in the Floor section. **Move marks with the image** is on to start with and only applies to the numbers you type. Turn it off when the marks are already right and only the picture should move. Dragging and nudging always move the marks too. Dragging, nudging and typing can all be undone.

Detection takes the offset into account, so walls found in an offset image land where the picture shows them.

## Stairs

A stair is marked once, on the lower floor. It climbs to the next floor up in the plan, and Plynth works out the steps from the floor heights.

**To place one**, show the lower floor and press **T**, the Stair tool. Click where the bottom step starts and drag toward the top of the stair. A live preview follows the mouse and turns in steps of 15 degrees. Hold Alt to turn it freely. Let go, and a small menu lists the stair types. Pick one and the stair is added. Esc stops. On the top floor the tool says "Add a floor above first.", because there is nowhere to climb to.

### The types

| Type | What it is |
|---|---|
| Straight | One flight. |
| L shape | A flight, a square landing, then a second flight turned 90 degrees. |
| U shape | A flight, a landing, then a second flight that comes back beside the first, 10 cm apart. |
| Spiral | Wedge shaped steps round a centre column. |

L shape, U shape and spiral have a **Turn** setting, left or right as you climb. Straight has none. The **Width** is from 0.6 m to 3 m and starts at 1 m.

### What the numbers mean

Select a stair and the Properties panel shows its type, width, turn and material, then the worked out values in the mono font.

- **Rise** is the height to climb, from one floor level to the next. It includes the slab, so ceilings of 3.0 m and slabs of 0.2 m give 3.2 m.
- **Risers** is how many steps up. A riser is at most 0.19 m, and all risers are equal. A rise of 3.2 m gives 17 risers of 0.188 m. The last riser steps onto the upper floor, so there is one tread fewer than there are risers: 16.
- **Riser height** is the height of one step.
- **Going** is the depth of one tread, 0.27 m.
- **Length** is the walk line from the bottom step to the upper floor. Sixteen treads of 0.27 m make 4.32 m.

A landing takes the place of one tread. Steps are solid. A spiral's steps are thin slabs on a column.

### The hole and the arrival

The stair's whole outline is cut out of the slab, the ceilings below and the room floors above, so there is clear height all the way up. On the upper floor the same outline is drawn as a dashed blue line with a dot where you arrive. It cannot be selected. Its tooltip says "Stair s2 from Floor 1. Edit it on Floor 1." Switch to the lower floor to change the stair.

### Editing a stair

A selected stair is amber. Drag its move handle to move it and its rotate handle to turn it. Each drag is one undo step. Delete removes the stair. The **Steps** material picks the material of the steps. Without one, the project default for stairs is used, oak planks to start with.

### Stair problems

The Properties panel lists the problems of a stair, and the Build report window shows them too. A stair with a warning is still built.

| Problem | Kind | What it means |
|---|---|---|
| Stair overlaps a wall | warning | The stair crosses a wall on its floor or on the floor above. The message names the wall. Move the stair or the wall. |
| Stair reaches outside the floor | warning | Part of the stair is outside the walls and rooms of its floor. |
| Stair arrives outside every room | warning | The top of the stair is not inside any room of the floor above. |
| Stair has no floor above | error | The floor above was removed. The stair is left out. Add a floor again, or remove the stair. |
| Unknown stair type | error | The plan names a type this copy of Plynth does not have. The stair is left out. |

## Roofs

You do not draw a roof. Plynth works it out. Every part of a floor that has nothing above it is a **roof section**. A house with one floor has one section. An upper floor over half of the ground floor leaves the other half as a section of the ground floor, and the top of the upper floor is a second one. When you move a wall, the sections follow. A part narrower than 0.2 m, such as a thin strip left beside the floor above, gets no roof. Plynth gives it a plain flat top in the outside wall material instead.

Each section takes its settings from the **roof default**, unless it has settings of its own. A new project's default is a flat roof with a 0.6 m parapet. A pitch of 30 degrees and an overhang of 0.3 m are kept ready for when you switch to a pitched type.

### Seeing and choosing sections

Turn on **View > Roof sections** (Alt+R) to see the roof sections of the shown floor as a blue hatch. Each has a label in the mono font with its type and its main setting, and an arrow for the direction of a ridge or a fall. Click a section to select it. A selected section is amber.

### Settings

With nothing selected, the **Roof default** group in the Properties panel sets the default. With a section selected, the same fields set that section. Only the fields the type uses are shown.

| Type | Pitch | Direction | Overhang | Parapet | Shape |
|---|---|---|---|---|---|
| Flat | no | no | yes, but not with a parapet | yes | A thin layer, with a low wall round the edge. |
| Gable | yes | yes, the ridge | yes | no | Two slopes meeting at a ridge, with a vertical triangle at each end. |
| Hip | yes | no | yes | no | A slope on every side, meeting at a ridge or a point. |
| Shed | yes | yes, the way it falls | yes | no | One slope. |
| Sawtooth | yes | yes, the ridges | yes | no | A row of 3 m wide slopes, each with a vertical glass face on its high side. |

- **Pitch** is the slope in degrees above level. A pitched roof needs at least 5 and at most 75.
- **Direction** is in degrees, measured clockwise on the plan from the right. Tick **Automatic direction** to let Plynth run the ridge along the longer side of the section. A shed with automatic direction falls toward the bottom of the plan when the long side runs left to right.
- **Overhang** is how far the roof reaches past the outer walls, up to 2 m. It stops at the walls of the floor above, so the roof of a low wing never pokes into the taller block.
- **Parapet** is the height of the low wall round a flat roof, up to 3 m. Zero means none. The wall is 0.2 m thick. A flat roof with a parapet has no overhang.

Roofs are 15 cm thick. A pitched roof that rises against a taller storey can cover its windows, because the slope does not turn away from the wall. For a low wing, choose a flat roof, or a shed that falls away from the wall.

**Hip needs a convex section**, one with no inward corners and no holes. A section shaped like an L cannot have a hip roof. It is built as a gable roof instead, and the Build report window says "A hip roof needs a section without holes or inward corners, so it is built as a gable."

### Settings of one section

Change a section that follows the default and it gets settings of its own, tied to a point inside it. The roof section also has a **Covering** material. **Use the default** takes the settings away again, and the section follows the default.

If you later change the walls so that the point is no longer in that section, the settings are kept but have no effect, and the Build report window says "The roof entry rf1 is not in any roof section, so it has no effect." (with the id of the entry). Remove them in the app: with nothing selected, open the **Roof default** group in the Properties panel and press **Remove** next to the unused entry. Or use `plynth roof clear`, or redraw the walls.

### Materials

Pitched roofs use the project default **Roofs** (clay tiles to start with), flat roofs use **Flat roofs** (concrete) and stairs use **Stairs** (oak planks). They are three more rows in the default materials, and `roof_tiles` and `concrete` come with the sample materials. Undersides use the ceiling default, roof edges the wall top default, and gables, parapets and slab bands the outside wall default.


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

- Deleting a point deletes the walls that end at it, the openings on those walls, and the rooms that use those walls. Wall items on those walls stay where they are.
- Deleting a wall deletes the openings on it and the rooms that use it. Wall items on it are taken off the wall and stay where they are.
- Deleting an opening deletes only the opening.
- Deleting a room deletes only the room. Its walls stay, and so does the furniture in it.
- Deleting a piece of furniture deletes only that item.
- A point left with no wall because of a delete goes too, so no stray dots stay behind. A point you placed on its own is only removed when you select it.


## Undo and redo

Every change to the plan can be undone. Use **Edit > Undo** (Ctrl+Z) and **Edit > Redo** (Ctrl+Y or Ctrl+Shift+Z). The menu names the change, for example "Undo Set plan image". If you undo back to the point where you last saved, the unsaved mark goes away.

## Saving

The title bar shows the project name, with `*` after it when there are unsaved changes.

- **Save** (Ctrl+S) writes `project.json` and `plan.json`. Images from outside the project are copied into `plans/`.
- **Save as** (Ctrl+Shift+S) writes to another folder, copying the images. The old folder is not changed. Plynth refuses a folder that already holds a different project. The project keeps its name.
- The title bar shows the name of the project after every save, save as, open and create.
- Closing, opening or starting a new project with unsaved changes asks "Save changes to <name>?" with Save, Don't save and Cancel.

A save writes each file to a temporary name first and then renames it, so a crash during a save does not leave a half written file.

### Older projects

Projects made with earlier versions of Plynth open as they are and upgrade by themselves. Nothing is asked and nothing is lost: doors and windows you already had get a hinge on the start of the wall and a swing to the right, the project gets the default sizes, every floor starts with no furniture, and the wall item centre is 1.2 m. Rooms get no wall material of their own, and the project gets the default materials listed under "Materials on walls, rooms and the project". Every floor gets a 0.2 m slab, no stairs and the default roof, so in a project with several floors each upper floor moves up by 0.2 m for every floor below it. The files are written in the new format the next time you save. A project saved by this version cannot be opened by an older version of Plynth, so keep a copy if you still need that.

### Autosave and recovery

Every 30 seconds, if there are unsaved changes, Plynth writes a copy into the project's `autosave/` folder. Saving or choosing Don't save removes it.

If Plynth was closed unexpectedly, the next time you open that project it asks: "This project has unsaved changes from <date and time>. Recover them?" If the saved plan is newer than the copy, the question tells you. Recover loads the copy as unsaved changes, so you still decide whether to save. Discard removes it.

## Building

Building turns the plan into a real 3D building. Plynth starts Blender in the background, raises the walls, cuts every opening, fits the doors and windows, lays floors and ceilings and places your furniture. You never see Blender's window.

**To build**, press the blue **Build** button in the bar above the plan, press Ctrl+B, or use **Build > Build**. The project has to be saved first. If it never was, Plynth asks "The project has to be saved before it can be built. Save it now?" The build uses what is in the window, saved or not, so you do not need to save after every edit.

While it runs, the bar shows a progress bar and the step it is on, for example "Cutting openings", with a **Cancel** button. Cancel stops the build. The last building stays as it was, so a cancelled or failed build never leaves you with half a file. A small flat takes a few seconds. Many openings and a large plan take longer, because every opening is cut one at a time. You can look at the plan while you wait, but you cannot start a second build.

When a build finishes, the 3D view opens by itself and the bar says "Built at 10:42 in 8.2 s". If you change the plan afterwards, the bar says "The plan changed since this build". Build again to catch up. If Blender fails, the bar says "The last build failed. See the Build report window." and the Build report window says why.

**Build is blocked** when a required item of the setup check is missing, such as Blender or the graphics driver. The button then opens the setup check and the status bar says what to fix. See [Setting up Plynth](SETUP.md).

### What gets made

- **Walls** with their real thickness and height. Walls of different heights meet without doubled faces, and a half wall stops at the face of a full one. Curved walls and rounded corners follow the plan.
- **Corners.** Where exactly two walls of the same height meet at a corner, both ends are cut on the diagonal so the outer faces join cleanly. T junctions, rounded corners, very sharp angles (under 25 degrees), walls of different heights and very short walls keep square ends.
- **Openings**, cut in their exact shape: an arch is round, a keyhole is a keyhole.
- **Doors and windows.** A door gets a frame, a closed leaf and a handle on both faces, on the side away from the hinge. A window gets a frame and a thin pane of glass. A plain opening stays empty. A doorway that reaches the floor also gets a strip of floor through the wall.
- **Floors and ceilings**, one flat face for each closed room. A room whose walls do not close gets none. The ceilings are not drawn when you look from above, so the top view shows the floors.
- **Furniture** at its marker, turned the way the plan shows, as a copy of the asset's own model. An item whose asset is missing is left out, with a problem at its marker. Plynth never puts a placeholder box there.
- **Edges** rounded by 1 cm, so they catch the light.
- **Materials.** Every wall face, floor, ceiling, door leaf, frame, glass pane and handle gets a material from your materials catalog, by the rules in "Materials on walls, rooms and the project". A surface with no material, or whose material is missing, gets a plain neutral color. Inside wall faces are a little lighter than outside faces, so you can tell them apart.
- **Slabs** between the floors, as thick as the floor's slab thickness. The slab shows from outside as a band. Where an upper floor reaches past the one below, the underside of the slab is built so the overhang is closed.
- **Stairs**, one solid step at a time, with the hole cut through the slab, the ceilings and the floors.
- **Roofs** on every part of a floor with nothing above it, each section by its type.
- **Upper floors** stacked on their slabs. A floor starts at the level below plus that floor's ceiling height plus its slab.

The 3D view shows the materials, metal and see-through glass included, on furniture too. See "Materials in the 3D view" and "Furniture materials".

- **Collision.** Closed convex shapes for walls, floors, ceilings, slabs, stairs, roofs and furniture, named `UCX_...`. See "Collision". They are in `building.blend` and in the export.
- **Slot names.** Materials are named for the surface, such as `Living_Floor`. See "Material slot names".

### Where the files go

Everything is written into the project's `build` folder:

- `building.blend`, the Blender file. Open it in Blender with **Build > Open building.blend in Blender**, edit it, render it.
- `building.fbx`, the whole building in one file.
- `preview`, a folder with one file for each floor, one for the slab above it and one for its roofs, and `parts.json` that lists them. The 3D view loads these so it can hide roofs and floors. They are written after `building.fbx`. If they cannot be written, the 3D view loads `building.fbx` instead.
- `materials.json`, written next to the FBX. It tells the 3D view which image maps each material uses.
- `textures`, the plain neutral images for surfaces that have no material.
- `preview_textures`, copies of textures larger than 1024 pixels, scaled down for the 3D view.
- `built-plan.json`, the plan of the last successful build. "Build is up to date" is judged against it.
- `export_work`, the files of the last export job.
- `build.json`, the report of the last build, which fills the Build report window when you open the project again.
- `job.json`, `result.json` and `blender.log`, for finding out what went wrong.

**Build > Open build folder** opens it in Explorer. The `build` folder can be deleted at any time. Plynth writes each model file under a temporary name and renames it at the end, so a failed build keeps the last good files.

You can also build from a terminal with `plynth build`. See the [Command line guide](CLI_GUIDE.md). A build from there and a build from the app give the same result.

## The 3D view

Press **3**, click **3D** in the bar above the plan, or use **View > 3D view** to look at the built building. Press 3 again, or click **Plan**, to go back. The 3D view only shows. Nothing you do in it changes the plan.

- **Orbit:** drag with the left button. The view turns around a point in the building.
- **Pan:** drag with the middle or right button.
- **Zoom:** the mouse wheel. It zooms toward the point the view turns around.
- **Fit:** press Ctrl+0, or use View > Fit to window, to frame the whole building again, furniture included.
- **Top** and **Three quarter:** two small buttons in the top right corner. Top looks straight down. Three quarter looks from the front right and a little above, the same angle as the thumbnails.

The background is the theme's ground color, the same as behind the plan, and the scene has a soft light from above so walls and furniture have shading and shadows.

**Roofs and floors.** Two items in the View menu change what the 3D view shows. They need the files in the `preview` folder, so build again once if the project was built before roofs and stairs existed.

- **View > Show roofs in 3D** (on by default). Turn it off to see the rooms under the roofs.
- **View > Cut 3D at the shown floor** (off by default). Turn it on to see the floors up to the shown one, without the shown floor's roof and without the floors above. Show the ground floor and turn it on to look at that floor from above, stair included.

The overlay on the plan draws only the shown floor.

Before the first build the 3D view says "Build the plan to see it in 3D. (Ctrl+B)". That is not an error. When a project is opened again, the 3D view shows the last build, with the files in `preview` when they are there and `building.fbx` when they are not.

## Build problems

A build always finishes with a list of problems, even when it worked. Each problem points at something in the plan.

- When there are problems, an amber **problems** button appears in the bar, such as "3 problems". Click it to open the **Build** tab.
- The **Build report** window has four parts. **Summary** says when the build ran, how long it took, which Blender, and how many objects, triangles, cuts, doors and windows, rooms and items. **Problems** lists each one. **Files** shows where the files are, with **Open folder** and **Open in Blender**. **Steps** lists the steps with their times in milliseconds. **Build > Build report window** (Ctrl+Shift+B), the **Report** button in the bar above the plan, or the amber **problems** button opens it.
- Each problem has an amber icon, a sentence and the id of its object. A filled icon is an **error**: the object was left out or is incomplete. An outline icon is a **warning**: it was built, but check it. Click a problem to go to the plan, select that object and centre the plan on it.
- **Markers.** The plan shows an amber triangle at each problem, filled for an error and outlined for a warning. Point at one to read the sentence. Turn them off with **View > Build problems on plan**. The plan shows the markers of the floor you are working on.

What the problems mean:

| Problem | Kind | What it means and what to do |
|---|---|---|
| Opening does not fit | error | The opening reaches past its wall or its height. It was not cut. The sentence says by how much. Move or resize it. |
| Room is not closed | error | The room's walls do not close, so it has no floor or ceiling. Close the gap or redraw the room. |
| Missing asset | error | The item's asset is not in the library. The item was left out. Put the asset back, or swap it. |
| Asset is broken | error | The library has the asset, but it is marked broken or its model file is gone. The item was left out. Import the asset again or swap it. |
| No library | error | No library folder is set, so every item was left out. Set one in the setup check. When the plan has no items and only uses materials, it is a warning and every surface gets the neutral look. |
| Could not cut | error | The opening could not be cut cleanly, so the wall was left uncut there. Try moving the opening a little. |
| Could not fit the door or window | warning | The opening is cut but empty, for example because it is too small for a frame. Make it larger. |
| Could not load the asset | error | Blender could not open the asset's model, so the item was left out. Press Retry render on the asset. |
| Could not round the edges | warning | The wall edges stay sharp. The building is otherwise fine. |
| Stair problems | warning or error | See "Stair problems" under Stairs. |
| A hip roof needs a section without holes or inward corners | warning | A hip roof was asked for on a section that is not convex or has a hole, so it was built as a gable. Pick another type or reshape the walls. |
| The roof entry is not in any roof section | warning | A roof entry's point lies in no roof section, or in a section another entry already governs. It has no effect. |
| Unknown roof type | warning | The plan names a roof type this copy of Plynth does not have, so the section is built flat. |
| Could not build a slab, stair or roof | error | Blender could not make that part from its outline, so it is left out and the building is incomplete. The export is blocked until it is fixed. The rest of the building is built. Check the walls round it. |
| Could not write the preview parts | warning | The 3D view loads the whole building file instead, so Show roofs and Cut at the shown floor do nothing until the next good build. |
| Furniture warnings | warning | Not in any room, overlaps a wall and the others in "Furniture warnings" are shown here too. |
| Missing material | warning | A wall, room or project default names a material that is not in the library. The surface got the neutral look. Import the material again or pick another. For a project default the problem has no object, so there is no marker. |
| Material is broken | warning | The library has the material but it is marked broken. The surface got the neutral look. Select the material in the Materials window to see why, and press Retry render. |
| Could not load a material map | warning | An image of a material would not open while building. The surface got the neutral look. |
A missing asset can show up as "Asset is broken" or "Missing asset" depending on how it went missing. If you removed or renamed the asset's folder by hand while the library still lists it, it is broken. If the library does not know the asset at all, it is missing.

Problems do not stop a build. Fix what you care about and press Build again.

## The 3D overlay on the plan

**View > 3D overlay on plan** (Alt+3) draws the top view of the built building over the plan image, so you can check that the walls sit on the walls of the drawing. It needs a build and a scale. It is drawn a little see-through, over the plan image and under the marks and handles, and it pans and zooms with the plan. It is redrawn after each build.

The plan view draws the same mitred corners as the build, so the overlay lines up with the plan's walls at corners and at T junctions.

## The live asset preview

The preview of an asset is now the real model, lit from the front right, as soon as its model file is ready. It appears in two places:

- **The furniture picker.** The hover preview panel shows the model for the asset you point at. It is shown from the front right and takes no mouse.
- **The Assets window.** The details of the selected asset show the model. Drag to orbit it and use the wheel to zoom.

The thumbnail shows first and stays when the asset has no 3D preview file, or when the model could not be drawn. Each asset gets that file, `asset.fbx`, when it is imported. After the update to this version, Plynth makes it once for every asset that lacks it, in the background. On a big library that means many short Blender runs, so give it a little time on the first start.

## If the 3D view stays empty

"Build the plan to see it in 3D" is normal before the first build. If you built and still see nothing:

- **Check the Build report window.** A problem there may explain it. If the build failed, there is no model to show.
- **Press Ctrl+0** to fit the view. You may be looking at empty space.
- **The message "The 3D view cannot start."** means your graphics card or its driver cannot run the 3D view. Plynth needs a card that supports Direct3D 11. Update the graphics driver from the maker of your card (NVIDIA, AMD or Intel), restart Plynth, and open **Help > Setup check** to see the Graphics driver row. On a laptop with two graphics cards, open Windows Settings, System, Display, Graphics, add Plynth and choose High performance. A remote desktop session or a virtual machine without a graphics card often cannot run it.
- **The message "The 3D model could not be loaded."** means `building.fbx` is missing or damaged. Delete the project's `build` folder and press Build again.
- **If the Graphics driver row says Found and the message still appears,** the program files may be damaged. Install Plynth again.

While the Graphics driver row says Missing, the Build button opens the setup check instead of building. `plynth build` in a terminal does not need the 3D view and still works.

## Setup check and the library

Before you import models, Plynth needs Blender, a library folder and a projects folder. **Help > Setup check** shows what is ready and what is not, and it opens by itself on the first run. Plynth never installs anything: if Blender is missing, you install it from blender.org yourself. Every item is explained in [Setting up Plynth](SETUP.md). The Graphics driver item is checked by the app when it starts, and it decides whether Build is allowed.

The **library** is one folder on your computer that holds your assets catalog. It is shared by all projects. Choose or create it in the setup check. **Catalog > Open library folder** opens it in Explorer.

## Updates

Plynth installed with `PlynthSetup.exe` can update itself from the Releases page on GitHub. Nothing updates without you asking.

**The check at start.** Once each time Plynth starts, after the window shows, it asks GitHub whether a newer release exists. On the first run, and after an update, it waits until you close the setup check, so you can switch the check off there first. When a newer release is found, a notice appears under the title bar with the new version. **Update** starts the update, **Later** hides the notice until the next start. A check at start that fails says nothing.

**Switching it off.** Use **Help > Check for updates at start**, the Updates row of the setup check, or `plynth settings set --update-check false`. When it is off, Plynth sends nothing at start. You can still check by hand.

**Checking by hand.** **Help > Check for updates** says whether a newer release exists, or why the check failed. A release whose signature does not match is skipped, and the check names it.

**Updating.** **Help > Update Plynth**, or Update in the notice, opens a window with this version, the new version and, once known, the size of the download. You can keep working while it downloads. Cancel stops it and nothing is installed. When the installer is downloaded and its signature verified, press **Install now**. Plynth asks about unsaved changes as it does when you close it, and Cancel there means nothing is installed. Then the installer starts, Plynth closes, and the installer shows its progress and opens Plynth again at Home. Your settings, libraries and projects are kept.

The update waits, and says why, while a build, export, import, material refresh or wall detection runs in the window, while a command line edit session is open on one of your projects, or while another Plynth window or command runs. Nothing is stopped for it. Finish or close what it names, then try again.

**What is checked.** Only a release signed with the key built into Plynth is offered, only a version higher than yours, and never a version lower than the highest one Plynth has seen before. A download that fails the signature check is never run. Versions below 1.0 are marked as an early preview.

**What is sent.** The check asks GitHub for the list of releases, then for the signature files of the versions newer than yours. Plynth sends no version, user, computer, install or project id, no path and no cookie or login. GitHub still sees your IP address, the time, and which signature files were asked for, which hints at how old your copy is. Your own proxy settings are used.

**A copy that was not installed.** A Plynth you built yourself, or one unpacked from the portable zip, never checks and never updates. **Help > Check for updates** and **Help > Update Plynth** say so, with a button that opens the Releases page only when you press it. To update it, build it again from the source or install Plynth with `PlynthSetup.exe`. `plynth update` answers `update_not_installed` in the same case.

`plynth update`, `plynth update --check` and `plynth update --status` do the same from the command line. See the [Command line guide](CLI_GUIDE.md).

## The assets catalog

The catalog is the list of 3D models you can use in your plans, such as chairs, tables and lamps. You bring your own model files. Plynth cleans each one up so they all behave the same way, draws a thumbnail, and keeps them in the library.

Open it with **Catalog > Assets catalog** (Ctrl+Shift+A), or click **Assets** in the bar above the plan. It opens in a window of its own. See "The app's windows". With no library set, the Assets window says "No library folder is set." and has an **Open setup check** button.

### Finding assets

At the top of the Assets window:

- **Search assets.** Type part of a name, tag or category. The grid filters as you type, after a short pause. Typing `cha` finds "Chair" and "Armchair". Several words must all match. Accents are ignored.
- **Room.** "All rooms", or one kind of room. Only assets in categories that belong in that room are shown. A living room shows sofas and lamps, a bathroom shows baths and mirrors.
- **Category.** "All categories", "Uncategorized" (assets you have not sorted yet), or one category. After you pick a room, the list holds only that room's categories.

Under the controls, a count says how many assets match. Each tile shows the thumbnail, the name, and the size in metres as width x depth x height, for example `0.45 x 0.50 x 0.90 m`. An asset that has not been measured yet reads "not measured" instead of a size. It cannot be placed until you press **Retry render**, which measures it.

**Drag a tile onto the plan, or onto the wall view, to place the asset.** See "Placing furniture".

A tile can say something other than a picture:

- **Rendering** (grey): the thumbnail is being drawn.
- **Render failed** (amber): Blender could not draw the thumbnail. The model is fine and can still be used. Select it and press **Retry render**.
- **Broken** (amber): the asset's folder cannot be read. Select it to see why.

Plynth never shows a stand-in picture. A thumbnail is always a real picture of the model.

### Importing

You can bring in models three ways.

1. **The Import button** (or **Catalog > Import asset...**, Ctrl+I). A file dialog opens. You can pick several files at once.
2. **Drag and drop.** Drop files, zips or folders on the Assets window. This is also the way to import a folder.
3. **The inbox folder.** Copy files into `Library\assets\inbox\`. Plynth notices within a few seconds and imports them, while it is running. If Plynth is closed, `plynth assets inbox` imports what is waiting.

Plynth reads `.blend`, `.fbx`, `.obj`, `.gltf` and `.glb` files, zips that hold one model, and folders that hold one model. A zip with more than one model is refused, and the message lists them. Zips downloaded from Fab and items from BlenderKit have their own way in: see [Importing from stores](#importing-from-stores).

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

Your original file is not changed. The cleaned model is saved as `asset.blend` in `Library\assets\items\<id>\`, with `asset.fbx` for the live preview, `thumb.png` and an `info.json` that holds the name, categories, tags and license.

### Warnings

Problems with a model never stop an import. They show in amber under the asset's details and in the `warnings` list of the command line:

- **Very high triangle count.** More than 500 000 triangles. Large models slow everything down. You can change the limit with `plynth settings set --triangle-warning`.
- **Suspicious size.** The largest side is above 30 m or below 0.02 m, which usually means the file used other units. Import again with `--scale` (for example `--scale 0.01` for a model drawn in centimetres).
- **Missing texture.** A picture the model asked for was not found, so that part has a plain color.
- **Removed objects.** Cameras, lights or empties were left out.
- **3D preview file failed.** `asset.fbx`, the file the live 3D preview of the asset uses, could not be written. The asset still works in plans and in builds, and its thumbnail is shown instead of the live model.

### Thumbnails

A thumbnail is a 512 pixel render made by Blender with its Eevee engine: the model seen from the front right and a little above, on a dark background, with soft lights. It is drawn once, when the model is imported. Next to it, the import makes `asset.fbx`, the file the live 3D preview uses.

If it fails, the tile says "Render failed". Select the asset and press **Retry render** in its details. This also measures the model again. **Retry render** is also what to use after you change `asset.blend` in Blender yourself.

### Details, and editing an asset

Click a tile to see the asset's details under the grid: the live 3D model (drag to orbit it, wheel to zoom; see "The live asset preview"), or its thumbnail when there is no model to show, the name, the id and size in metres, the triangle count, where it came from, and the warnings. Every edit is saved at once.

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

## The materials catalog

The second catalog of the library holds materials: the paint, brick, wood, tile, metal and glass that walls, floors and doors are made of. You bring your own texture images or Blender materials. Plynth turns each one into the same set of image maps, draws a thumbnail, and keeps it in the library.

Open it with **Catalog > Materials catalog** (Ctrl+Shift+M), or click **Materials** in the bar above the plan. It opens in a window of its own. With no library set, the window says "No library folder is set." and has an **Open setup check** button. With a library and no materials, it says "No materials yet." and has an **Add the sample materials** button. That button imports the nine samples that come with Plynth: `white_paint`, `red_brick`, `oak_planks`, `grey_tile`, `painted_wood`, `brushed_steel`, `clear_glass`, `roof_tiles` and `concrete`. New projects use them as their default materials, so add them before your first build. The same action is in **Catalog > Add the sample materials**. Running it twice does nothing the second time.

### Finding materials

- **Search materials.** Type part of a name, tag or group. The grid filters as you type. Several words must all match.
- **Group.** "All groups", "Uncategorized", or one group. The built-in groups are floor, wall, ceiling, exterior, trim, door, glass and metal. **Add group**, **Rename** and **Remove** change the list. The eight built-in groups cannot be renamed or removed (the tooltip says "Built-in groups cannot be renamed or removed."). Your own groups can, and a material can still be moved between groups. Removing a group takes it off every material that has it. The materials stay.

A count under the controls says how many materials match. Each tile shows the thumbnail and the name.

### The thumbnail

A thumbnail is a 512 pixel render from Blender: a sphere 1 m across on the left and a flat tile 1 m square on the right, lit like an asset thumbnail. The texture is drawn at its real world size. A brick material shows small bricks on the sphere, and a plank floor shows boards that are wide on the tile. A material that is see-through, such as glass, is drawn over a checkered background so you can see it. A tile can say **Rendering**, **Render failed** (select it and press **Retry render**) or **Broken**.

### Details, and editing a material

Click a tile to see its details under the grid. Every edit is saved at once.

- **Name.** Type a new name and press Enter. The id under it, such as `red_brick`, does not change. The plan and the command line use the id.
- **Tile size.** How far one repeat of the picture reaches in the real world, width along the wall or floor and height up the wall, in metres, for example `1.20 x 0.60 m`. See "Tile size" below. A new size makes Plynth render the thumbnail again.
- **Texture size**, for example `2048 x 2048 px`, the pixel size of the base color image. It is only shown.
- **Maps.** The images the material is made of, and how each was made: `from oak_col.jpg` (an image of the source), `value 0.85` (a single number from the source, saved as a tiny image), `baked` (made from a Blender material, see "Materials from a .blend file") or `none`.
- **License**, **Groups** and **Tags**, as for assets. The source line says where the material came from.

Buttons: **Retry render** cleans the maps again and draws a new thumbnail. Use it when a render failed, or after you changed an image in the material's folder. **Open folder** shows the folder in Explorer. **Remove** deletes the material and its folder after asking. Plans that use it keep its id and show it as missing. Catalog edits are not plan changes, so Undo does not undo them.

### The maps

Every material in the library has the same images, whatever it came from:

| Map | When | What it holds |
|---|---|---|
| Base color | always | The color. Saved as color data (sRGB). |
| Normal | always | The little bumps. Saved as plain data, in the OpenGL convention. |
| Roughness | always | How rough or shiny. Plain data, grey. |
| Metallic | always | How metal. Plain data, grey. |
| Opacity | only when see-through | How solid. Plain data, grey. |
| Occlusion | only when the source had one | Soft shadow in creases. Plain data, grey. |

Color data is shown as your eyes expect it. Plain data is numbers that only mean something to the lighting, so Plynth never changes the brightness of those images. A material that had only a number for roughness or metal, such as roughness 0.6, gets a tiny 4 pixel image with that value. Metal and see-through are always images. That is on purpose: the FBX file cannot carry them as numbers into the 3D view, and Unreal is more dependable with images.

### Tile size

One image covers one tile. The tile size says how big that tile is in the real world. If one brick image covers 1.2 m of real wall across and 0.6 m up, its tile size is `1.20 x 0.60 m`. The build lays the picture on a wall so that every 1.2 m along the wall is one repeat. A bigger tile size makes the texture look bigger. If the size is not given, Plynth takes it from `material.json`, and otherwise uses 1 m by 1 m. Tile sizes go from 0.01 m to 100 m.

### Rescan

**Catalog > Rescan materials**, or the **Rescan** button, reads the library's `materials` folder again and updates the index. Use it after you add, remove or change material folders by hand. It runs no Blender. A folder that has images and no `info.json` is listed as needing a refresh ("Not cleaned yet."), and so is one whose image changed. A material whose map file was deleted is Broken. Use **Retry render** on a material that needs a refresh. Plynth also watches the library while it runs and rescans by itself a couple of seconds after the folders change.

## Importing materials

There are three ways in.

1. **The Import materials window.** Press **Import** in the Materials window, or use **Catalog > Import materials...**. Add folders (**Add folder...**) and files (**Add files...**), or drop them on the window. Fill in what you want, then press **Import**.
2. **Drag and drop** on the Materials window.
3. **The inbox folder.** Copy folders, zips, images or `.blend` files into `Library\materials\inbox\`. Plynth notices within a few seconds and imports them while it runs. Imported entries move to `inbox\done`, failed ones to `inbox\failed` with a text file that says why. If Plynth is closed, `plynth materials inbox` imports what is waiting.

Imports run one after another in a list under the grid, with progress and **Cancel**, as asset imports do. When an import finishes, Plynth shows the new material's thumbnail. If you ticked no group, it asks for one.

The Import materials window has these fields. Only the sources are needed.

- **Name** (one source only), **Groups**, **Tags** and **License**.
- **Set the tile size**, with a width and a height in metres. Off takes it from `material.json`, or 1 m.
- **Normal map type.** "Automatic (from the file name)", "OpenGL" or "DirectX (flips green)".
- **Material name inside a .blend file.** Needed when the file holds several materials.
- **Bake size for .blend materials.** 256 to 4096 pixels, 1024 by default.
- **Image files per channel** (one source only): Base color, Normal, Roughness, Metallic, Opacity and Occlusion, each with **Browse...**. "Leave empty to match by file name."

### Texture sets

A texture set is the pictures that make one material. Plynth reads a **folder**, a **zip file**, or one **image file** (`.png`, `.jpg`, `.jpeg`, `.tga`, `.tif`, `.tiff`, `.bmp` or `.exr`). A folder may hold the pictures one folder down. A single image becomes the base color of a material.

Plynth matches pictures to maps by their names. It splits a name at `_`, `-`, spaces, dots and where a lower case letter meets a capital, so `NormalDX` becomes `normal` and `dx`. The last word it knows decides:

| Map | Words it knows |
|---|---|
| Base color | basecolor, albedo, diffuse, diff, color, colour, col, base |
| Normal | normal, nor, nrm, norm |
| Roughness | roughness, rough, rgh |
| Glossiness (turned into roughness) | gloss, glossiness |
| Metallic | metallic, metalness, metal, mtl |
| Opacity | opacity, alpha, mask |
| Occlusion | ao, ambientocclusion, occlusion, ambient |
| Packed (split into occlusion, roughness and metallic) | arm, orm |
| Ignored | height, displacement, disp, bump, specular, spec, emission, emissive, emit, preview, thumb, thumbnail |

`brick_nor_gl.png`, `oak_Roughness.png` and `wood_painted_rgh.png` all match. Height and displacement images are left out, because the build does not use them. A normal map named with `dx` or `directx` is DirectX and gets its green channel flipped. One with `gl` or `opengl` is OpenGL. With neither word it is taken as OpenGL, and bumps that look inverted mean it was DirectX: import again with "DirectX" or `--normal directx`. A material needs a base color, as an image or as a color in `material.json`, or the import fails with "no base color".

Plynth warns, and still imports, when:

- **Several images fit one map.** It uses the largest and says which (`ambiguous_map`).
- **An image matched no map** and was not used (`unused_image`).
- **Maps are different sizes** (`map_size_mismatch`).
- **The base color may show seams** where it repeats, because its left and right edges, or top and bottom edges, differ a lot (`visible_seams`).
- **The base color is low resolution**: under 256 pixels for each metre of the tile (`low_resolution`).

The warnings show under the material's details.

### Fixing a wrong match

If a name is odd and Plynth picks the wrong picture, name the picture yourself. Use the **Image files per channel** fields of the Import materials window, or `--map base_color=wood.jpg` on the command line. A picture you name beats the file names. You can also give a single number for roughness, metallic, opacity or occlusion (`--value metallic=1`).

### material.json

An optional file named `material.json` in the folder (or at the top of the zip) tells Plynth what the set is, so you do not have to type it each time. Every field is optional:

```json
{
  "format": "plynth-material-source",
  "version": 1,
  "id": "red_brick",
  "name": "Red brick",
  "groups": ["wall", "exterior"],
  "tags": ["brick", "red"],
  "license": "own_work",
  "tile_size": { "width": 1.2, "height": 0.6 },
  "maps": { "normal": "brick_nor_gl.png" },
  "values": { "roughness": 0.8 },
  "base_color_value": [0.55, 0.2, 0.15],
  "normal": "auto"
}
```

What you type in the Import materials window wins over the file, and the file wins over the file names. A folder that holds only a `material.json` with `values` or a `base_color_value`, and no images, imports too. That is how the sample `clear_glass` is made. The sample materials all use this file, so they keep their ids and the project defaults can name them. An `id` that is already taken gets `_2` added.

### Materials from a .blend file

A `.blend` file with one material imports as a material. If it holds several, the import fails and lists them, and you pick one in "Material name inside a .blend file". The file itself is never changed.

Plynth turns the material into the same image maps as any other. Blender's own baking needs the Cycles renderer, and Plynth never uses Cycles, so it works in three ways, one map at a time:

- **An image wired straight in** is copied. The map says `from`, with the image's name.
- **A plain number** (an unconnected value) becomes a small constant image.
- **Anything else**, such as a noise texture or a color mixed from several nodes, is drawn with Eevee. Plynth lays a flat tile of the material's tile size under a camera that looks straight down, shows just that map, and saves the picture at the bake size. The map says `baked`.

Two limits come with that. A procedural pattern that does not repeat over one tile shows a seam where tiles meet, and Plynth warns `visible_seams`. A material that depends on the object's own coordinates, or on the world, can look different on the flat tile than on the original object. If that matters, import the material as images instead.

### Add to materials catalog, from an asset

An asset keeps its own materials. To also use one as a surface, open the asset in the Assets window. Its **Materials** list has a button **Add to materials catalog** for each material. It makes a new catalog material from the asset's `asset.blend`. The asset is not changed.

## Importing from stores

Plynth can bring in models and materials from BlenderKit and from zips you download from Fab. Both are for your own use. Plynth never downloads from Fab and never bundles store items, so each person downloads their own copy.

### The Sources window

Open it with **Catalog > Sources and integrations** (Ctrl+Shift+I) or **Sources** in the bar above the plan. **Catalog > Import from BlenderKit...** opens it with the cursor in the address box, and **Catalog > Import Fab zips...** goes straight to picking zips. It has one card for each place assets and materials come from: **Own files**, **BlenderKit** and **Fab**. A card shows:

- the name, a state (**Ready**, **Needs setup**, **Not checked** or **Checking...**) and a sentence on what it needs;
- a reminder, when there is one. BlenderKit shows "Downloads run Blender with your own settings and enabled add-ons.";
- buttons to import, and the options you can change, saved with the library;
- a link to the official website.

**Where imports from the Sources window show.** An import you start on a card shows on that card, under its import buttons: a row with the name, what Blender is doing, a progress bar and **Cancel**. If it fails, the row stays with the reason, **Show log** and **Dismiss**, and a Fab pack row offers **Pick the model...**. A sentence in amber under the buttons says why an import did not start, for example "Paste an address first.". When an import ends, the card says "Added Chair to Assets." (or "Added Oak planks to Materials.") with **Show in Assets** (or **Show in Materials**), which opens that window with the new item selected. When the item has no category or group yet, the line asks you to pick one there. The Assets and Materials windows do not open by themselves and do not list these imports; the new item simply appears in them. **Own files** works the same way for models. **Import material...** on the Own files card opens the Import materials window, where that import shows.

The first time you open the tab, and when you press **Check again** on the BlenderKit card, Plynth runs the setup check. It starts Blender once with a clean start, runs none of your add-ons and reads your saved Blender preferences only as data (see [Setting up Plynth](SETUP.md)). A card with no add-on to check is always Ready. Options are saved as soon as you change them, in the library, and apply to every later import from that card. They need an open library. Every option the command line has is on the card, and the same options are saved by `plynth library integration set` (see the [command line guide](CLI_GUIDE.md)). An option saved with a value the card does not list is shown as it is, and it stays until you pick another.

### Importing from BlenderKit

You need your own BlenderKit account and the BlenderKit add-on, installed, enabled and signed in inside Blender. Plynth never installs it, never signs in for you and never shows, logs or copies your key. [Setting up Plynth](SETUP.md) shows how.

1. Copy the item in one of these forms:
   - the address of the item's gallery page in the browser, `https://www.blendkit.com/asset-gallery-detail/<id>/`. This is the most reliable form. Addresses on www.blendkit.com (BlenderKit's site) and on its old site www.blenderkit.com both work.
   - the text BlenderKit copies inside Blender when you copy the item's asset ID, such as `asset_base_id:<id> asset_type:model`.
   - the id alone, five groups of 8, 4, 4, 4 and 12 digits and letters a to f, such as `0c1d4a2e-1111-2222-3333-444455556666`.

   A Get link, such as `https://www.blendkit.com/get-blendkit/<id>/`, does not work: it holds the id of one version of the item, which Plynth cannot look up. The card says so as soon as you paste it.
   If BlenderKit finds no item with the id, the import says so. Copy the gallery page address and try again.
2. On the BlenderKit card, paste it into the address box. If the text is none of these, the card says why, and the buttons stay off.
3. Press **Import as asset** for a model or **Import as material** for a material.

The import shows on the BlenderKit card, under the import buttons, as described in "Where imports from the Sources window show". The row shows the steps: checking the add-on, downloading and copying. A big item can take a few minutes. Plynth waits up to 10 minutes by default. The card's wait option raises that, from 60 to 3600 seconds, as `wait_seconds` does from the command line (see the [command line guide](CLI_GUIDE.md)). The **Texture resolution** option says which size of textures to ask for, 2k by default.

**What runs.** Plynth starts Blender in the background, with no window, and the BlenderKit add-on downloads the item. There is no option to show a Blender window. This download runs Blender with **your own settings and the add-ons you have enabled**, because BlenderKit needs your add-on and your sign in. Other Blender jobs in Plynth use a clean start, but this one cannot. After the download, Blender cleans the model the same way as for your own files, with a clean start.

**BlenderKit's helper.** While the download runs, the BlenderKit add-on starts its helper program. During a download from Plynth the helper may show no tray icon. To stop the download, press **Cancel** on the row in Plynth. The helper ends with the download.

**What ends up where.** The files go into the library like any other asset or material. The name and license come from BlenderKit, and the item remembers its address. The author is not saved yet. Every download goes into Plynth's own fetch folder, never into the add-on's own download folder, and Plynth never changes or removes anything there.

If it fails, the row says why, and **Show log** opens the log. These are the usual reasons:

- **The add-on is not installed or not enabled.** Install or enable it in Blender (Preferences, Add-ons).
- **You are not signed in.** Open Blender, go to Preferences, Add-ons, BlenderKit and log in. If BlenderKit's saved settings file is damaged, Plynth leaves it exactly as it is, adds "The saved BlenderKit settings file could not be read and was left as it is." to the message, and you sign in again inside Blender.
- **Wrong tab.** A model pasted as a material, or a material pasted as a model, is refused with a sentence that names the right tab. Nothing is added. Other kinds of items, such as HDRs, cannot be imported.
- **Nothing arrived in time.** Try again, raise the wait on the card, or check the item in BlenderKit inside Blender.

### Importing Fab zips

Fab items download as a zip from fab.com in your browser. Plynth never goes to Fab for you. A zip often holds one model in several formats and qualities, with textures at several sizes. Plynth reads the list of files in the zip without unpacking it, picks one version, and unpacks only that.

1. Download the zip from Fab.
2. On the Fab card, press **Import model zips...** or **Import material zips...**, and pick one or more zips. (A Fab zip dropped in the library's inbox is picked up too, but its license is Unknown. See "Which license is saved" below.)
3. A window opens that shows, for each zip, what Plynth would take. Use **Previous** and **Next** with several zips. Tick **Leave this zip out** to skip one.
4. Press **Import**. The zips are imported one after another, and each shows on the Fab card (see "Where imports from the Sources window show").

**How a version is picked.** Plynth reads the model format first: FBX, then glTF, then OBJ, then Blend, as set in the card's **Preferred format**. Among the models of that format it takes the **Quality** you set (Medium by default), or the nearest one if there is none. It takes the lowest level of detail (LOD). Textures come from the folder next to the model. When they come in several sizes, it takes the largest up to **Largest texture** (4096 px by default), or the smallest if all are larger. Pictures that only show a preview are never used as textures.

**In the window.**

- **Model.** The model taken. Open the list to pick another version of it, with its format and quality.
- **Textures.** The textures taken, and the size they were taken at. Each has a list that says which channel it is for. **Automatic** reads it from the file name. Change it when a texture is on the wrong channel. Next to it, say which material it is for: **Every material**, or the name of one material. A texture set for one material changes only that material, and the others keep their own textures. A material can have one texture for each channel. When you give a second texture the same material and channel, the first one goes back to **Automatic**.
- **Left out.** Files in the zip Plynth does not use, each with the reason.

A zip that holds several different models (a pack) cannot be imported until you pick one. The window says "This zip holds several models" and offers **Pick a model...**. If a queue row fails for this reason, its **Pick the model...** button opens the same window. Pick another model, and the new import replaces the row. To import several models of a pack, first tick **Keep the zip files after importing**, then import the zip once for each model. Without it, the zip goes to the Recycle Bin after the first import.

**Fixing textures.** Fab textures are matched to materials by their file names. If a material comes out with the wrong picture or none, import the zip again from the Fab card and set the channel and material of each texture by hand. You need the zip for that, so tick **Keep the zip files after importing** the first time, or get the zip back from the Recycle Bin. Plynth cannot see a model's material names before Blender reads it. After the first import, the asset's **Materials** list in the Assets window shows them. The **Normal maps** option says which way the normal maps point. **Automatic** reads the file name and takes OpenGL when it says nothing. Pick **DirectX** if your normal maps look inside out (Fab content made for Unreal often is).

If a zip holds no model Plynth can read, for example only `.uasset` files, the window says so. Download the FBX or glTF version from Fab instead. A zip that holds other zips has to be unzipped first.

**What happens to the zip.** Unless **Keep the zip files after importing** is ticked, a zip that imported well goes to the Windows Recycle Bin, so you can get it back. A Fab zip takes effort to download again, so tick it if you want to keep it.

A Fab zip with one plain model, with no versions, goes through the normal import and is treated as your own file. Import it from the Fab card if you want Plynth to match its textures by name.

**Which license is saved.** A zip you import from the Fab card, or with **Catalog > Import Fab zips...**, gets the card's **License**, Royalty free unless you changed it. You told Plynth the zip is from Fab, so the store's license applies. A zip that reaches Plynth another way, with the import button of the Assets or Materials window, by drag and drop, or through the inbox, gets **Unknown**, even when Plynth sees that it looks like a Fab zip and picks a version for you. Plynth only guessed where it came from. **Pick the model...** on such a row keeps Unknown. You can set the license in the catalog afterwards, or import the zip from the Fab card instead.

### Store items

An item from BlenderKit or Fab is a normal asset or material. It shows in the catalog and works in plans like any other. It remembers where it came from: the address for BlenderKit, the zip's file name for Fab. The license is set from the store: BlenderKit gives the item's license, and a zip imported from the Fab card gets the card's **License** (Royalty free by default). A Fab zip that came in another way gets Unknown. You can change any license in the catalog. Remember that the store's own terms apply to your use of the item.

## Materials on walls, rooms and the project

Every built surface gets a material by one set of rules. The most specific setting wins.

- **A wall face that looks into a room:** the wall's own **Inside** material, otherwise the room's **Wall faces** material, otherwise the project default **Wall inside**.
- **A wall face that looks outdoors:** the wall's **Outside** material, otherwise the project default **Wall outside**.
- **A floor or ceiling:** the room's own, otherwise the project default.
- **The top and bottom of walls:** the project default **Wall top**.
- **The sides of a door or window cut** (the reveal): what the wall shows on the opening's right side, or on the left when no room is on the right. The right side is the one in "Left and right" of the command line guide.
- **A doorway's floor strip:** the floor of the room the door swings into, otherwise the other side's, otherwise the project default **Floor**.
- **Door leaves, frames, glass and handles:** the project defaults **Door**, **Frame**, **Glass** and **Handle**.

A wall between two rooms has two inside faces, one for each room. A room's **Wall faces** material applies only to the faces that look into that room. So a bathroom can have tiled walls while the bedroom on the other side of the same wall stays painted. A wall's own **Inside** material wins on both sides, so use it when you want one material on both.

### The pickers

Select something and look in the Properties panel.

- **A wall** shows **Inside** and **Outside**.
- **A room** shows **Floor**, **Wall faces** and **Ceiling**.
- **A stair** shows **Steps**. **A roof section** shows **Covering**.
- **Nothing selected** shows **Default materials**, with twelve surfaces: wall inside, wall outside, wall top, floor, ceiling, door, frame, glass, handle, roofs, flat roofs and stairs.

Each row shows a small swatch, the name of the material that applies now, and in grey where it comes from, such as "from the room", "set on this room" or "project default". A material the library does not have shows its id and "not in the library" in amber.

Click a row to open the picker. Pick a group, then a material. Hover a material to see its thumbnail, name and tile size. Click to use it. The first entry, **Use the default**, takes the setting away so the next rule applies. For defaults, **No material** gives that surface the neutral look. The picker only lists materials, never models. With several walls or rooms selected, one pick changes them all, and Ctrl+Z undoes it in one step.

### New and old projects

A new project uses these defaults: wall inside `white_paint`, wall outside `red_brick`, wall top `white_paint`, floor `oak_planks`, ceiling `white_paint`, door `painted_wood`, frame `white_paint`, glass `clear_glass`, handle `brushed_steel`, pitched roofs `roof_tiles`, flat roofs `concrete` and stairs `oak_planks`. Projects from earlier versions get the same defaults when they open. You can change any of them. If the sample materials are not in the library, each build says "Missing material" for them and the surfaces get the neutral look. Press **Add the sample materials** to fix that.

## Materials in the 3D view

After a build, the 3D view shows each material with its color, bumps, shine, metal and see-through parts. The brick outside, the paint inside, a handle that reflects its surroundings and glass you can see through all show. The view tones the light the way a camera would and lights the scene from a neutral sky, so colors are not washed out. Textures are smoothed when you zoom out.

The FBX file does not carry metal, roughness and see-through values, so the build writes a second file, `build\materials.json`, next to it. The 3D view reads it and gives each surface its maps. Every built material has its own base color image, so the view can tell the materials apart. If `materials.json` is missing, for example after you copied only the FBX, the view shows the FBX's own plain colors.

The texture paths in the file point into your library. If you move the library, build again so the paths are written again. Furniture materials are in the file too, so a metal or glass part of a chair shows as it will in Unreal. To keep the view fast, it loads copies of textures no larger than 1024 pixels. The export keeps the full size.

## Exporting for Unreal

Exporting turns the last build into FBX files that Unreal can import, with their textures and collision shapes. A single project writes one file. A grouped project writes one file for each group (see "Grouped export"). It does not build. It takes `build\building.blend` as it is, hand edits included, so build first.

**To export**, open the Export window with **Export** in the bar above the plan or **Build > Export window** (Ctrl+Shift+X) and press **Export**, or press Ctrl+E, or use **Build > Export**. First the quality checks run (see "The quality gate"). If none failed, the files are written. If the project has not been built, the window says "Build the project first." and has a **Build** button.

**Check export** (Ctrl+Shift+E, or the **Check** button) runs the same checks and writes no FBX and no textures. Use it to see what is wrong before you export.

The export runs in the background with a progress bar and a **Cancel** button. It cannot start while a build is running, and a build cannot start while it runs.

### What is in the export folder

Everything goes into the project's `export` folder:

- A single project: `<Name>.fbx`, named after the project. A project called "My Flat 2" gives `My_Flat_2.fbx`. Walls, floors, ceilings, slabs, stairs, roofs, doors, windows and furniture are all in it. The collision shapes go in the same file.
- A grouped project: one FBX for each group, named `<Name>_<Group>.fbx`, for example `My_Flat_2_Walls.fbx`. See "Grouped export".
- With the modular kit switched on: `<Name>_Kit.fbx` and `<Name>_Kit_layout.json`. See "Modular kit".
- `Textures`, the images the FBX uses and no others. The names are `T_<Material>_<Channel>`, for example `T_OakPlanks_BaseColor.jpg` and `T_OakPlanks_Normal.png`. A surface with no material gets a plain image named after its slot.
- `export_report.json`, the report of the last export. It lists the checks, the files and the time.

The FBX holds only relative texture paths. You can copy or move the whole `export` folder anywhere, and the textures are still found as long as `Textures` stays beside the FBX. Textures keep their full size. Only the 3D view uses smaller copies.

**Open folder** in the Export window, or **Build > Open export folder**, shows the folder in Explorer. The folder can be deleted at any time. A new export replaces the old files. An export that is blocked by a failed check leaves the old files exactly as they were, so check the time in the tab to know which export is on disk.

The Export window shows the last export: when it ran, how long it took, the result, the number of objects, triangles and collision meshes, and the file paths. A grouped export lists each file with its group's name. If the plan changed after the last build, the "Build is up to date" check fails. Build again, then export. The window shows the last export again when you open the project later. If you built again since that export, an amber line says the export was made from an older build; export again to bring the files up to date.

### Export anyway

When a check fails, the tab says "Export blocked: 2 checks failed." and shows **Export anyway...**. Pressing it asks first. If you confirm, the files are written even though the checks failed. The report records that the export was forced and names the failed checks it went past, so you can see later what was ignored. Warnings never block an export.

### Export mode

A project exports in one of two modes:

- **Single FBX**: one file for the whole building. Import it once and everything is in place.
- **Grouped FBX**: one file for each group of things, such as the walls, the floors, and the furniture of each room. See "Grouped export".

You choose the mode when you make the project, and you can change it at any time. With nothing selected, open **Properties** and set **Export mode** in the project section. It is one undo step, like the grid size. Changing it does not make the build out of date, so you do not have to build again. Your group edits stay in the project when you switch back to single, and are used again when you switch to grouped.

From the command line the same change is `plynth project set --export grouped` (or `single`).

A single project shows one line in the Export window: "Single FBX: one file. Switch to grouped in Properties > Project."

### Grouped export

A grouped export writes several FBX files instead of one. Every file uses the building's own origin, so when you bring them all into the same spot in Unreal they line up with no manual placing.

**The automatic groups.** Plynth makes these for you:

| Group | File ends with | What is in it |
|---|---|---|
| Walls | `_Walls.fbx` | The walls of every floor, and the door and window parts. It also takes anything Plynth cannot place, such as an object you added by hand in `building.blend`. |
| Floors | `_Floors.fbx` | Every room's floor and ceiling, the door thresholds, and every slab, stair and roof. Lights go here too once the plan has them. |
| Furniture, one for each room | `_Furniture_Living.fbx` | The furniture of one room, named by the room type. Two rooms of one type get the room id added to the name, so their files stay apart. |
| Furniture | `_Furniture.fbx` | Furniture that is not in any room. Only made when there is some. |

A group with nothing in it is not written. A collision shape always goes into the same file as the thing it belongs to.

**The groups come from the last build.** The list is made from the plan as it was when you last built, because that is what is in `building.blend`. After you change the plan and build again, the groups are made again from the new plan.

**Reviewing the groups.** Open the **Export** tab. For a grouped project it has a **Groups** section below the Export and Check buttons, followed by the modular kit tick box:

- One row for each group, with its name, its file name and how many members it holds. Open a row to see its members, such as "Floor of Living r1" or "Window o3".
- The group that takes anything left over (Walls, at first) says "Also takes anything added by hand".
- A group with no members says "Not written: nothing in it".
- Before the first build the section says "Build the project to see its groups."
- While a command line session has the project open, the list is read only (see "When the command line is editing").

**Editing the groups.** The edits are kept in the project, so every later export uses them:

- **Rename.** Double click a group or press F2. The file is renamed too, and the old file is removed at the next export. A name uses English letters (A to Z, a to z), digits, spaces, hyphens and underscores only, with at least one letter or digit, up to 64 characters. Two groups cannot have the same name.
- **Merge.** Right click a group and choose **Merge into**, then the group that should take its members. The first group goes away.
- **Move a member.** Right click a member and choose **Move to**, then a group or **New group...**. Or drag the member onto a group.
- **New group.** The **New group...** button makes an empty group. Move members into it.
- **Reset groups.** The button asks first, then throws away every edit and goes back to the automatic groups. The kit switch is not touched. If `export-options.json` is damaged, **Reset groups** also repairs it: the old file is kept beside it as `export-options.json.bad`.

A refused edit shows its reason in amber under the list. Group edits are export settings, not plan changes, so they have no undo. Use **Reset groups** to go back.

**What happens to edits when the plan changes.** Plynth keeps an edit as what you did (move this item to that group, merge these two groups), not as a finished list. After a new build it makes the automatic groups again and applies your edits in this order: new groups, merges, moves, renames. An edit that no longer fits, for example a move of a sofa you have since deleted, is skipped. A line in amber above the list says which one and why, for example "Group furniture_r9 is gone, so its new name was not used." Nothing else breaks.

**File names.** A file is called `<Project>_<Group name>.fbx`, with anything other than letters, digits, `_` and `-` turned into `_`. A project "My Flat" with a group "Lounge props" gives `My_Flat_Lounge_props.fbx`. If two names would give the same file, the second gets `_2`.

**The checks run over all the files together.** The new **Groups** check fails when an object of the building belongs to no group, so nothing is left out in silence, and warns about a group that gets no object. The FBX scale check reads every file back and compares the walls' size over all of them.

### Modular kit

The modular kit is an optional extra of either export mode. Instead of one large mesh of walls, it gives you the few unique wall pieces the building is made of, each once, and a layout file that says where each copy goes. In Unreal you can place the pieces yourself, reuse them in other levels or build new walls from them.

To switch it on, tick **Also write the modular kit** in the Export window. From the command line it is `plynth export kit on`. The kit needs the library, because its pieces use the same material slot names as the building. Without one the export goes on without the kit and the Export window says "No library is set, so the kit was left out." in amber. A plan with no walls gives no kit. The kit uses the custom opening shapes of its library. If an opening cannot be built, the kit is left out and the tab says why, in amber; the rest of the export still goes ahead. After an export with the kit, the tab shows the kit file and the layout file, how many pieces there are and how many placements.

**What a piece is.** A piece is a short stretch of wall, cut so it can be used again. There are four kinds:

- **Wall**: a straight stretch with nothing in it.
- **Opening**: a stretch that holds a door or a window, with the hole cut and the door or window in it.
- **Corner**: where walls meet, such as an L corner, a T junction or a cross. It has a cut face on each arm.
- **Curved**: one piece for each curved wall.

Floors, ceilings, thresholds, slabs, stairs, roofs and furniture are not in the kit.

**How the walls are cut.** Each wall is cut on the project's grid (the grid size in Properties, 0.5 m at first). Plynth finds the places where walls meet and keeps them for the corner pieces. Between them it cuts the wall at every whole multiple of the grid, counted from the wall's lower end (the end with the smaller X, then the smaller Y, on the plan's world axes), not from where you started drawing. A door or window is never cut through. If a grid line would fall inside an opening, that cut is dropped, so the opening sits inside one piece. Grid cuts that would leave a sliver under 5 cm are dropped, but a stretch between two junctions can still be shorter than that. A plain square room with 4 m walls, 0.2 m thick, on a 0.5 m grid gives one corner piece used four times and two wall pieces, 0.5 m and 0.4 m long, used between the corners.

**The same piece is made once.** Two pieces are the same when their shape, materials, openings and heights match within a millimetre. A wall drawn from right to left gives the same piece as one drawn from left to right, and a corner matches when turned. A piece used many times is one mesh in the kit and many placements in the layout.

**Names.** A piece is called `<Kind>_<length>x<height>x<thickness>_<hash>`, with the sizes in whole centimetres, for example `Wall_50x300x20_a1b2c3` (an example, not a real hash). The mesh in the FBX is `SM_Kit_` followed by that name. The hash comes from the piece's shape, so a piece keeps its name from one export to the next while it does not change. Its collision meshes are `UCX_SM_Kit_<name>_<number>`.

**Cut faces are not rounded.** The building's wall edges are slightly rounded (bevelled). The faces where one piece is cut from the next are left sharp, so two pieces placed side by side meet with no groove.

**Each piece has its own origin.** In the kit FBX every piece sits at its own origin, so they overlap in the file. Import it with **Combine Meshes** off, and each piece becomes its own static mesh. A wall, opening or curved piece has its origin at floor level at one end of the piece, with X along it. Which end depends on the piece's shape and not on the way the wall was drawn, so always place pieces by the layout's `location` and `rotation_z_degrees`. A corner has its origin at the plan point where its walls meet. Its X runs along one of those walls.

**The layout file.** `<Name>_Kit_layout.json` lists the floors with their grid, every piece (`id`, `mesh`, `kind`, `length`, `height`, `thickness`, `count`) and every placement. A placement has the piece, its mesh, the floor, the wall or plan point it comes from, a `location` and a `rotation_z_degrees`. The coordinates are Blender's world: metres, X as the plan's x, Y as minus the plan's y, Z up from the first floor. That is the frame every exported FBX is written in. A rotation is in degrees about Z, turning anticlockwise seen from above, from 0 up to but not including 360. The shape of the file is in `schema/kit-layout.schema.json`.

**Placing the pieces in Unreal.** This is advice. Plynth does not run Unreal, and the kit was not tried there. Two things differ:

- Unreal counts in centimetres, so multiply every `location` by 100.
- Unreal's axes are not Blender's. The FBX import turns the model to suit Unreal, and the layout numbers have to be turned the same way. Expect the Y sign to flip, so a placement at (x, y, z) would go to (x*100, -y*100, z*100) and the turn would go the other way, -rotation_z_degrees. Treat this as a first guess.

To check the signs, import the walls file (or the single file) and the kit file with the same import settings, place one corner piece and one wall piece from the layout, and see whether they sit on the walls. If they are mirrored or turned, change the signs once. The same signs then hold for every placement, because every file shares one origin.

## The quality gate

The checks look at the real objects that go into the FBX. The Export window lists them in a column. Each row has the check's name, a sentence and a mark: blue for pass, an amber outline for a warning, filled amber for a fail, grey for skipped. Click a row to see what it found. Click a finding that names something on the plan, and Plynth switches to the plan and selects it.

"Build is up to date" is judged against the plan of the last successful build, saved in `build\built-plan.json`. A build that failed does not change what counts as built.

| Check | What it looks at | How to fix a fail |
|---|---|---|
| Build is up to date | The plan has not changed since the last successful build. | Press Build, then export. |
| Build is complete | The last build left nothing out: no missing assets, no openings that were not cut, no rooms that do not close, no slab, stair or roof that could not be built. Each finding names the item. | Fix the problems on the Build report window, then build again. |
| Scale and units | The scene is in metres with scale 1, and the walls are the size the plan says, within 1 mm. | Do not change the scale in `building.blend`. Build again. |
| Pivots and applied transforms | Every object sits at the building's origin with no rotation, scale or parent. | The export does this itself. A fail means a hand edit made a parent. Remove it. |
| Naming | Every mesh is called `SM_...` or `UCX_...`, names are not repeated, and names use only letters, digits and `_`. | Rename the object in `building.blend`. |
| UVs | Every textured mesh has UVs. A face whose UVs have no area, or furniture whose UVs overlap, is a warning. | Add UVs in Blender. Walls, floors and ceilings always have them. |
| Texture sharpness | How many pixels of texture each metre of surface gets. A warning below half the median of the building or below 256 pixels per metre. A fail below a quarter of the median. | Use a bigger texture, or a smaller tile size, for the material the finding names. |
| Triangle counts | A mesh with no triangles fails. A warning above 1,000,000 triangles in one mesh or 10,000,000 in all. | Remove the empty mesh, or use lighter furniture. |
| Materials and textures | Every mesh has materials, every slot has a real material, every image file exists, and every base color is an image. An unassigned material blocks the export. | Pick a material for the surface. |
| Collision | Every wall, floor, slab, stair, roof, furniture item and kit piece has a `UCX_` mesh, and each one is closed and convex. Ceilings, door leaves and window glass need none. | Build again. |
| Groups | Every object of the building belongs to a group, so no file leaves anything out. A group with no object is a warning, since its file is not written. | Move the object into a group. |
| FBX scale | After writing, Plynth reads the FBX files back and checks the walls' size over all of them and the object count. Skipped when nothing was written. | Report it. It means the file is wrong. |
| Modular kit | After writing, Plynth reads the kit file back. Every piece must be there once, with no move, turn or scale, and have collision. Skipped when the kit is off or was not written. | Report it. It means the kit file is wrong. |

A ceiling set to "No material" fails the Materials check. Choose a material for it, or use **Export anyway...** if you want it plain.

## Collision

Unreal needs simple shapes to stop the player walking through things. Plynth makes them for you. Each is a closed convex mesh named `UCX_<mesh name>_<number>`, for example `UCX_SM_f1_Walls_00`, and Unreal uses it as the collision of the mesh with that name.

- **Walls** are cut into pieces. A piece is convex, so a wall with a doorway becomes a piece left of the door, a piece right of it and a piece over it. You can walk through the doorway. A plain opening gets a piece under it. A **window** does not cut the wall: the glass area stays blocked.
- **Floors and ceilings** get a collision slab 10 cm thick, under the floor and above the ceiling, or as thin as the slab between the floors when that is thinner, so nothing pokes into the next floor. Rooms that are not rectangles are split into several slabs. Columns and holes stay open.
- **Door thresholds** get a slab too.
- **Slabs** between the floors get closed convex pieces from the bottom of the slab to its top. The hole for a stair stays open.
- **Stairs** get one piece for each step, landing and column.
- **Roofs** get sloped convex shapes under the covering, so you can walk on a roof. A flat roof's parapet gets a piece for each face that looks up. Gables and glazing get none.
- **Door leaves and window fills** get none.
- **Furniture** gets one convex hull around each item.

The collision meshes are in `building.blend` and in the export, and not in `building.fbx`, which the 3D view loads. If you import the exported FBX into Blender you will see a closed top over the building. That is the backs of the ceilings and the collision slabs. Unreal hides collision meshes and does not draw them.

## Material slot names

Every surface in the FBX has a material named for what the surface is, so you can give each one your own Unreal material. These are the slot names:

| Surface | Slot |
|---|---|
| A wall face inside a room | `<RoomType>_Wall`, for example `Living_Wall` |
| A wall face outside | `Exterior_Wall` |
| Top and bottom of walls | `Wall_Top` |
| A room's floor | `<RoomType>_Floor`, for example `Living_Floor` |
| A room's ceiling | `<RoomType>_Ceiling` |
| Door and window reveals | The slot of the wall face they follow |
| A doorway's floor strip | The floor slot it follows |
| Door leaf, frame, glass, handle | `Door`, `Frame`, `Glass`, `Handle` |
| Slab edges and ledge tops | `Exterior_Wall` |
| Slab and roof undersides | `Soffit` |
| The sides of a stair well | `Stair_Well` |
| Every part of a stair | `Stair` |
| A pitched roof's covering | `Roof` |
| A flat roof's covering | `Flat_Roof` |
| Roof edges | `Fascia` |
| Gables and parapets | `Exterior_Wall` |
| Sawtooth glazing | `Glass` |

The room type is written with a capital at each word: `guest_room` gives `GuestRoom`. A room with no type uses `Room`. Rooms of one type share slots across the whole building.

If one slot would show two different materials, each gets the material added. With oak floors in one bedroom and tiles in another, you get `Bedroom_Floor_OakPlanks` and `Bedroom_Floor_GreyTile`. A surface with no material gets `_Neutral`. Furniture materials are named `<Asset>_<Material>`, for example `Sofa_Fabric`.

The same names are used in `building.blend` and in `building.fbx`. Builds from earlier versions used the catalog ids as material names, such as `oak_planks`. Build again to get the slot names, and assign your Unreal materials again once.

## Importing into Unreal

This is advice, since Plynth does not run Unreal. Import `<Name>.fbx` (or every group file, and the kit file if you wrote one) with these settings in the import dialog:

- Turn **Combine Meshes** off, so each `SM_` mesh stays its own static mesh and picks up the `UCX_` collision that has its name.
- Keep the `Textures` folder beside the FBX when you import, so Unreal finds the images.
- Turn on **Generate Lightmap UVs** if you will use baked lighting. Plynth does not make a second UV set.

Repeated furniture becomes separate meshes in the FBX. Ten identical chairs are ten meshes. The group files all share the building's origin, so import them at the same location and they line up. Placing the modular kit is described under "Modular kit".

## Furniture materials

When you import a model, Plynth gives each of its materials the same image maps a catalog material has: base color, normal, roughness, metallic and opacity. Metal and glass on furniture then show in the 3D view and in Unreal. Images the model already used are copied as they are. A material made of nodes, such as noise, is drawn into an image over one square. A material that depends on the object's own coordinates looks a little different once drawn, and the import warns "material approximated".

Assets imported in earlier versions have no maps. Rescan lists them, and Plynth makes the maps once, in the background, from the asset's own model file. Hand edits you made to the asset stay.

## Add the sample shapes

Plynth comes with sample opening shapes, such as a keyhole, in `samples\shapes`. A new library gets copies in its `shapes` folder. For a library you made earlier, use **Catalog > Add the sample shapes**. The status bar says how many were added. Files already there are never replaced. The shapes then show in the opening shape picker. See "Shapes".

## When a plan image is missing

If the file of a plan image has been moved or deleted, Plynth still opens the project and still lets you edit and save it. The image path stays in the plan. After a save, the status bar shows in amber: "The plan image plans/floor1.png is missing. Its path was kept." Put the file back in the project's `plans` folder, or set another image, and the warning goes away. The plan view shows "The plan image ... could not be opened." until then.

## When the command line is editing

A script or another program can edit the project while Plynth has it open. To keep your work and its work apart, it first begins an edit session. While one is active:

- A banner under the title bar says "Being edited from the command line by <name>. Changes appear here as they are saved."
- The plan is dimmed. Undo, Redo, Save, Save as and the floor tab's Set plan image are disabled. New project and Open project still work.
- The Scale, Wall, Opening, Room and Furniture tools, Delete, dragging in the plan and the wall view, and every box in the properties panel are disabled. You can still click to select and look.
- Changes appear on screen within a second, with the zoom and position unchanged.
- When the session ends, or after 5 minutes without a command, the plan reloads once and becomes editable again.

A project you open while a session is active starts read only. The recovery question waits until it unlocks.

**Unlock now.** The button on the banner ends the session at once. Plynth asks first: "Unlocking ends the command line session. Its next edit will be refused until it begins a new one." Use it when a script has stalled or you want the project back.

If the app has unsaved changes, a script cannot begin a session. It has to wait until you save or discard.

**Builds and exports from the command line.** When a script runs `plynth build` or `plynth export` on the project you have open, with or without a session, the bar above the plan shows it as it runs: "Building from the command line" or "Exporting from the command line", the step it is on and a progress bar, as for a build of your own. Build and the export buttons are off until it ends, because only one build or export runs at a time. **Cancel** asks it to stop, the same as `plynth build cancel`. When a build ends, the Build report window shows its report, the 3D view loads the new building and opens by itself, and the status bar says "Built from the command line." If it failed, the bar says "The last build failed. See the Build report window." When an export ends, the Export window shows its report and the view stays as it is. A build or export that was cancelled leaves the last one as it was, and the status bar says so.

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

## Generators

A generator is a small Blender script that makes a piece of the building from a few values, such as a column of a given height or a railing of a given length. You place it like furniture, and the build runs it in Blender.

**Placing one.** Open the furniture picker with the Furniture tool (P) and click on the plan. The first list has a **Generators** row. Pick it to list the generators, and point at one to see its size, its parameters and the add-ons it needs. Click it to place it at the cross. A generator item is drawn and moved like any other item, and its footprint follows its size.

**Its values.** Select the item. The properties panel shows **Generator parameters**, one box for each value the generator declares: numbers in metres or degrees, whole numbers, on and off switches, choices and materials. A change is one undo step and the next build uses it. **Reset to defaults** gives every value its default. When the generator file changed since you placed the item, **Update** keeps the values that still fit and gives the rest their default, and the panel says what changed.

**Add-ons.** Some generators need a Blender add-on, such as Archimesh or Bagapie. **Catalog > Check add-ons** asks Blender which add-ons are switched on. A generator whose add-on is off shows greyed in the picker, and an item that uses it is left out of the build with the warning `generator_unavailable`. Plynth never installs or switches on an add-on in your own Blender. See "Add-ons" in [Setting up Plynth](SETUP.md).

**The generators folder.** Generators are the `.py` files in the `generators` folder of your library. A new library gets the sample generators. For a library made earlier, use **Catalog > Add the sample generators**. Files already there are kept. **Catalog > Open the generators folder** opens it in Explorer, and **Catalog > Reload generators** reads it again after you add or change a file. A file that cannot be read is left out and the others still work. `plynth generator list` names each file it left out and why.

**When the build runs one.** A generator that fails is a build problem named `generator_failed`, with the last line of its error. When what it made reaches more than 1 cm past its declared size, the build warns with `generator_size`. Only add generators from people you trust: a generator is Python code that Blender runs. To write your own, see [Writing generators](GENERATORS.md).

## Help

**Help > Help** (F1) opens the Help window. Its pages are the files in the `docs` folder that comes with Plynth, so the help in the app and the files are the same.

- The list on the left holds the pages: Getting started, User guide, Keyboard shortcuts, CLI developer guide, AI integration guide, Writing generators, About and licenses, and Setup check. A page whose file is missing is left out.
- Type in **Search help** to find words in every page. A hit in a heading comes first, then the other hits in page order. Each hit shows the page, the heading above it and the line. Click one to open the page at that heading.
- Links to other pages open in the window. Links to web pages open in your browser.
- **Back** (Alt+Left) and **Forward** (Alt+Right) step through the pages you saw.
- **Setup check** in the list opens the setup check. See [Setting up Plynth](SETUP.md).

The help links on the home screen open the same window.

Every button and field also has a tooltip with its name and, when it has one, its shortcut. Hold the mouse over it to see it.

### About and licenses

**Help > About Plynth** opens About in the Help window. It shows the version, the licenses and the third party notices.

- Plynth's source code is free software under GPL-3.0-or-later. The program includes Qt Quick 3D and Qt Quick Timeline, so the program you run is distributed under GPL version 3.
- The sample plans, models, textures, materials, shapes and sample projects are dedicated to the public domain under CC0 1.0. The scripts and generators among the samples are GPL-3.0-or-later code. The logo and the program icon are not part of the CC0 samples.
- The notices list every library, font and data file Plynth ships that was not written for it, with its license and copyright. Click a license text in the list to read it in full.

The texts are also in the `licenses` folder beside the program, and the notices file there is `THIRD_PARTY_NOTICES.md`.

## Samples

Plynth comes with samples drawn for it: plan images, furniture models, materials, an opening shape, generators and, when present, whole sample projects. They are dedicated to the public domain under CC0 1.0, so you may use them for anything.

- **Sample projects** on the home screen open a copy of the sample in your projects folder, named after it: "Apartment", then "Apartment (2)". The sample itself never changes. If the project uses items your library does not have yet, add the sample items and build.
- **Catalog > Add the sample items** imports every sample model and material with the license CC0 and copies the sample shapes and generators into your library. An item the library has already is skipped and keeps its own license.
- **Sample plans** on the home screen open the New project window and start the new project with that sample as the plan image of floor 1.

## Author and the license report

The assets and materials catalogs have an **Author** field under License. Type who made the item and press Enter. It is empty when unknown. Items imported from BlenderKit get their author from the store.

**Also write the license report** in the Export window makes every export write two more files beside the FBX: `<Name>_licenses.json` and `<Name>_licenses.txt`. They list every asset and material the last build used, with its name, where it came from (the file name or the web address, never your folders), its author and its license. The switch is saved with the project and is off for a new one.

- The report describes the last build, so build again after you change the plan. Without a build the export goes on and the tab says why no report was written.
- Notes in amber point at items with an unknown license, a CC BY item with no author, an item that is no longer in the library, or a personal use license. They never block an export.
- Materials inside an asset are covered by that asset. Generators and changes you make by hand in `building.blend` are not listed.
- The report lists what Plynth knows about each item. It is not legal advice.
