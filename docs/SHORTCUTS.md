# Keyboard shortcuts

Every action with a shortcut is also in the menu, where the shortcut is shown next to its name.

## File

| Shortcut | Action |
|---|---|
| Ctrl+N | New project (asks for its name and location) |
| Ctrl+Shift+H | Home: show the home screen. The open project stays open behind it |
| Ctrl+O | Open a project folder |
| Ctrl+W | Close the project and go to the home screen |
| Ctrl+Shift+O | Set the plan image of the current floor |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save as, into another folder |
| Alt+F4 | Exit Plynth |

Ctrl+O used to open a plan image. It now opens a project. Right-click a floor tab and choose Set plan image, press Ctrl+Shift+O, or drop an image on the window, to set the plan image.

Ctrl+W asks "Save changes to <name>?" when there are unsaved changes. The home screen lists your recent projects, and Ctrl+N and Ctrl+O work there too.

Setting the plan image can be undone. A new project from an image starts with the image already in place.

## Edit

| Shortcut | Action |
|---|---|
| Ctrl+Z | Undo the last change |
| Ctrl+Y or Ctrl+Shift+Z | Redo the change that was undone |

| Shortcut | Action |
|---|---|
| V | Select tool |
| S | Scale tool |
| W | Wall tool |
| O | Opening tool |
| R | Room tool |
| T | Stair tool, to place a stair |
| P | Furniture tool, to place furniture |
| D | Detect tool |
| ] | Turn the selected item 90 degrees clockwise |
| [ | Turn the selected item 90 degrees counterclockwise |
| Enter | Accept the proposed walls, openings and rooms, while the Detect tool is active |
| Delete or Backspace | Delete the selected point, wall, opening, item, room or stair, and what depends on it |

These Edit menu items have no key. They work on the selected item, and are greyed when they do not apply:

| Menu item | What it does |
|---|---|
| Turn item left, Turn item right | The same as `[` and `]` |
| Mount item on wall | Hang a floor item flat on the nearest wall |
| Take item off wall | Make a wall item a floor item again. It stays where it is |
| Swap item asset... | Pick another asset for the item |
| Update item size | Take the size the library has now for the item's asset |

The menu shows the name of the change, for example "Undo Set plan image". The items are greyed when there is nothing to undo or redo, and while the command line is editing the project.

The tool keys work when the canvas has focus. While you type in a box in the properties panel, or in the terminal, a letter is typed into it and does not switch tools, and Delete and Backspace edit the text instead of deleting a plan item.

The Scale tool needs a plan image. The Wall, Opening, Room, Stair, Furniture and Detect tools also need a scale, and the tooltip then says "Set the scale first (S)". The Furniture tool needs a library folder too. While the command line is editing the project, the tools are disabled and the Select tool is used.

## View

The zoom actions work once a plan image is loaded.

| Shortcut | Action |
|---|---|
| Ctrl+= or Ctrl++ | Zoom in one step |
| Ctrl+- | Zoom out one step |
| Ctrl+0 | Fit the whole plan in the window. When the 3D view is showing, fit the building in it |
| Ctrl+1 | Show the plan at 100 percent |
| Ctrl+` | Show or hide the terminal |
| Ctrl+Page Up | Show the floor above the shown one |
| Ctrl+Page Down | Show the floor below the shown one |
| Alt+R | Show or hide the roof sections of the shown floor on the plan |
| 3 | Switch between the plan and the 3D view |
| Alt+3 | Show or hide the 3D overlay on the plan |
| G | Snap to grid on or off (on by default) |
| E | Show or hide the wall view under the plan |
| F | Flip the wall view to the other side of the wall |

These View menu items have no key: **Align plan image** (see below), **Show roofs in 3D** and **Cut 3D at the shown floor**. The floor tabs along the bottom of the plan also work with the mouse: click to switch, double click to rename, right click for Rename, Add floor below and Remove floor, and the **+** button adds a floor above the top one.

Each zoom step is a factor of 1.25. The zoom range is 2 percent to 3200 percent.

E and F work when the canvas or the wall view has focus. F does something only while the wall view is showing.

## Build and the 3D view

| Shortcut | Action |
|---|---|
| Ctrl+B | Build the building in Blender |
| Ctrl+E | Export the last build as a plain FBX: the FBX, the textures and the report (Build > Export) |
| Ctrl+Shift+E | Check the export: run the quality checks and write no FBX (Build > Check export) |
| 3 | Switch between the plan and the 3D view (View > 3D view) |
| Alt+3 | Show the 3D overlay on the plan, or hide it (View > 3D overlay on plan) |
| Ctrl+0 | In the 3D view, fit the whole building in the window |

Ctrl+B builds while a build is not already running. Cancel the build with the **Cancel** button in the build bar or Build > Cancel build.

Ctrl+E and Ctrl+Shift+E need a build, and do nothing while a build or another export is running. Cancel an export with the **Cancel** button in the Export window. **Export anyway...** and **Open export folder** are in the Build menu and have no key.

In the 3D view the two small buttons in the top right corner look straight down (**Top**) or from the front right (**Three quarter**). They have no shortcut.

While the terminal or a text box has focus, the keys go there first, so click the plan or the 3D view before pressing 3. Ctrl+= and Ctrl+- zoom the plan only.

## The side panel and the app's windows

| Shortcut | Action |
|---|---|
| Ctrl+Shift+P | Open or close the Properties panel on the right (View > Properties panel) |
| Ctrl+Shift+A | Open the Assets window (Catalog > Assets catalog) |
| Ctrl+Shift+M | Open the Materials window (Catalog > Materials catalog) |
| Ctrl+Shift+I | Open the Sources window (Catalog > Sources and integrations) |
| Ctrl+Shift+B | Open the Build report window (Build > Build report window) |
| Ctrl+Shift+X | Open the Export window (Build > Export window) |

Each window opens once. Its shortcut brings it to the front when it is open already. The buttons **Assets**, **Materials**, **Sources**, **Report** and **Export** in the bar above the plan do the same, and **Properties** at the right end of the bar opens or closes the side panel. The Import materials window opens from Catalog > Import materials... and has no key.

The shortcuts with Ctrl, Alt or a function key work in the app's windows as well as in the main window. The single letter keys, such as the tool keys, work only in the main window. Alt+F4 in one of the app's windows closes that window.

## Catalog

| Shortcut | Action |
|---|---|
| Ctrl+I | Import model files into the library |

Rescan library, Import materials, Add the sample materials, Add the sample shapes, Rescan materials, Room types and categories, Open library folder, the generator entries (Check add-ons, Open the generators folder, Reload generators and Add the sample generators) and Help, Setup check have no shortcut. They are in the Catalog menu and the Help menu.

While you type in the search box or edit a name or the tags of an asset, letters go into the box and do not switch tools. Press Enter to save a name or the tags.

You can also import by dropping model files, zips or folders on the Assets window, or by copying them into the `inbox` folder of the library. See the [user guide](USER_GUIDE.md).

## Drawing and selecting

These keys and clicks work with the tools. The Select tool is the default.

| Key or click | Tool | What it does |
|---|---|---|
| Left click | Select | Select the point, opening, item, wall or room under the cursor. Click on nothing to clear the selection. |
| Left button and drag on a point | Select | Move the point. Drop it on another point or a wall to join it there. The whole drag is one undo step. |
| Ctrl | Select | While dragging a point: move it freely, with no snapping and no lining up. |
| Right click on a wall | Select | Open a menu with **Split here**, which cuts the wall in two at that place and adds a point. |
| Left button and drag on an opening | Select | Move the opening along its wall. It snaps to 5 cm. Ctrl moves it freely. |
| Left button and drag on an item | Select | Move the item. It snaps to the grid and flush against wall faces. Ctrl moves it freely. On a wall item it slides along its wall. The whole drag is one undo step. |
| Left button and drag on the round handle of a selected item | Select | Turn the item in steps of 15 degrees. Shift turns it freely. |
| Left button and drag on the blue ring of a selected wall | Select | Bend the wall. It snaps to straight near the middle and rounds to 1 cm. |
| Esc | Select | Clear the selection |
| Left click | Wall | Place a point and, after the first one, draw a wall to it. The next wall starts where the last one ended. |
| Shift | Wall | Keep the wall exactly horizontal or vertical. Hold it while you click. |
| Ctrl | Wall | Place freely, with no snapping and no lining up with other points. Hold it while you click. |
| Esc or right click | Wall | End the chain. The next click starts a new one. |
| Left click | Scale | Mark the first point, then the second one. |
| Shift | Scale | Keep the two points exactly horizontal or vertical |
| Esc | Scale | Cancel the measurement |
| Left click | Room | Make a room inside a closed area, or change the type of the room you click |
| Left click on a wall | Opening | Add an opening where the amber outline is. |
| Ctrl | Opening | Place freely instead of in steps of 5 cm. Hold it while you move and click. |
| Left click | Furniture | Mark a place and open the picker there. Pick an asset to place it. |
| Esc or click outside the picker | Furniture | Close the picker and place nothing |
| Enter | Detect | Accept the proposals as one undo step. Ctrl+Z takes them away again. |
| Left button and drag | Stair | Click the bottom step and drag toward the top of the stair. A preview follows. Let go to pick the type from a menu. |
| Alt | Stair | Turn the stair freely instead of in steps of 15 degrees. Hold it while you drag. |
| Esc | Stair | Stop placing the stair |
| Left button and drag on the move handle of a selected stair | Select | Move the stair. The whole drag is one undo step. |
| Left button and drag on the rotate handle of a selected stair | Select | Turn the stair. Alt turns freely. |
| Left click on a roof section | Select | Select the section while View > Roof sections is on |
| Left button and drag | Align plan image | Move the plan image of the shown floor. The walls, rooms, openings, items and stairs of that floor move with it. |
| Arrow keys | Align plan image | Nudge the plan image by 1 cm. Shift nudges by 10 cm. The marks move with it. |

Shift has no effect on a wall that starts a chain, because there is no earlier point to keep straight from. Snapping to existing points always wins, so you can still close a loop while Shift is held. Ctrl needs to be held when you click, and Alt is not used because Windows gives it to the menu bar.

Dragging a point snaps it like a click of the Wall tool: onto other points and walls, lined up with the x or y of nearby points (a thin dashed line shows it), to the grid, and to Shift for straight moves. Dropping it on another point or a wall joins it there, and its walls take the thickness of the wall they join. Hold Ctrl to move it freely.

## The wall view

The wall view appears under the plan when a wall or an opening is selected. It has its own mouse and key actions.

| Key or click | What it does |
|---|---|
| E | Show or hide the wall view |
| F | Flip to the other side of the wall |
| Left button and drag on an opening | Move it along the wall and up or down. It snaps to 5 cm. |
| Shift while dragging an opening | Keep the move in one direction only, along or up, whichever you drag further. |
| Ctrl while dragging | Move or resize freely, with no snapping. |
| Left button and drag on an item | Move a wall item up and down and along the wall. It snaps to 5 cm. Ctrl moves it freely. |
| Left button and drag on a handle | Resize the selected opening. There are eight handles. The opposite edges stay put. |
| Left click on the wall face, with the Opening tool | Add an opening at that place, at the default sill. |
| Left click on the wall face, with the Furniture tool | Open the picker. The asset you pick hangs there, with its centre at the wall item centre. |
| Drop an asset tile from the Assets window | Hang that asset at the place you drop it |
| Mouse wheel | Zoom at the cursor |
| Middle button and drag | Pan |
| Home | Fit the wall in the view again |
| Ctrl+Z | Undo. Each drag is one undo step. |

While a command line session is editing the project, the wall view cannot be dragged.

## Terminal

The terminal is a normal shell in the project folder. It opens from the round button at the bottom right of the plan, or with Ctrl+`. Hiding it keeps the shell running.

While the terminal has focus, almost every key goes to the shell. That includes Ctrl+S, Ctrl+Z and Ctrl+O, which do not save, undo or open anything in Plynth until you click outside the terminal. Ctrl+` always shows or hides the panel.

| Shortcut | Action |
|---|---|
| Ctrl+` | Show or hide the terminal |
| Ctrl+Shift+C | Copy the selected text |
| Ctrl+C | Copy the selected text. With nothing selected it sends Ctrl+C to the running program, which usually stops it. |
| Ctrl+V or Ctrl+Shift+V | Paste |
| Shift+PageUp | Scroll back through the output |
| Shift+PageDown | Scroll forward |

With the mouse, drag to select text. The wheel scrolls back through the last 5000 lines. If a program has asked for the mouse, the wheel and clicks go to it; hold Shift to select text instead.

## Mouse

| Action | What it does |
|---|---|
| Mouse wheel | Zoom in or out. The point under the cursor stays where it is. |
| Middle button and drag | Pan the plan |
| Hold Space, then left button and drag | Pan the plan |
| Drop an image file on the window | Set it as the plan image of the current floor |
| Drag an asset tile from the Assets window and drop it on the plan | Place the asset there. Ctrl places it freely. |

While Space is held, the cursor becomes an open hand. It closes while you drag.

Space needs the canvas to have keyboard focus. Moving the mouse over the canvas gives it focus.

## The 3D view mouse

| Action | What it does |
|---|---|
| Left button and drag | Orbit around the middle of the building |
| Middle button and drag | Pan |
| Mouse wheel | Zoom toward the point the view turns around |
| Hold the right button and move the mouse | Fly: look around from where the camera stands |
| Right button held, W, A, S, D | Fly forward, left, back and right along the view |
| Right button held, Q or E | Fly down or up |
| Right button held, Shift | Fly faster |
| Right button held, mouse wheel | Change the fly speed |
| F | Frame the whole building again |

The keys only fly while the right button is held and the 3D view has the keyboard. Click the 3D view first if the keys go to the plan tools. When you let go, the camera stays where it is, and orbiting turns around a point in front of it.

The 3D view only shows the building. Nothing you do in it changes the plan.

## Fit to window

When you open an image, Plynth fits it to the window. If you resize the window, it fits again. Once you zoom or pan, the view stays where you put it. Press Ctrl+0 to fit again.

At 400 percent and above, the image is drawn with hard pixel edges, so you can see single pixels. Below that it is drawn smooth.

## Supported images

| Format | File names |
|---|---|
| PNG | .png |
| JPEG | .jpg, .jpeg |
| BMP | .bmp |

Plynth looks at the contents of the file, not its name. A file named .png that is really a JPEG still opens.

Size limits:

- Width and height: at most 16384 pixels each.
- Total pixels: at most 64 million (about 8000 by 8000).

If an image is refused, a message says why, and the image you had open stays on screen. Files that are missing, empty, damaged or in another format are refused the same way.

## Opening from the command line

```
plynth plan-folder
plynth --image plan.png
```

The first opens a project folder. The second starts a new project with the image already loaded. With both, the image is set on the opened project as an unsaved change. To save a picture of the window, add `--screenshot out.png --size 1280x720`.

The command line can also create and edit projects without opening the window. See the command line guide.

## Help

| Shortcut | Action |
|---|---|
| F1 | Open the Help window |
| Alt+Left | In the Help window, go back to the page before |
| Alt+Right | In the Help window, go forward again |

Setup check and About Plynth are in the Help menu and have no key.
