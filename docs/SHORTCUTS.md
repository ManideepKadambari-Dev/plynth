# Keyboard shortcuts

Every action with a shortcut is also in the menu, where the shortcut is shown next to its name.

## File

| Shortcut | Action |
|---|---|
| Ctrl+N | New project |
| Ctrl+Shift+N | New project from an image |
| Ctrl+O | Open a project folder |
| Ctrl+Shift+O | Set the plan image of the current floor |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save as, into another folder |
| Alt+F4 | Exit Plynth |

Ctrl+O used to open a plan image. It now opens a project. Use Ctrl+Shift+O, or drop an image on the window, to set the plan image.

Setting the plan image can be undone. A new project from an image starts with the image already in place.

## Edit

| Shortcut | Action |
|---|---|
| Ctrl+Z | Undo the last change |
| Ctrl+Y or Ctrl+Shift+Z | Redo the change that was undone |

| V | Select tool |
| S | Scale tool |
| W | Wall tool |
| R | Room tool |
| Delete or Backspace | Delete the selected point, wall or room, and what depends on it |

The menu shows the name of the change, for example "Undo Set plan image". The items are greyed when there is nothing to undo or redo, and while the command line is editing the project.

The tool keys work when the canvas has focus. While you type in a box in the properties panel, or in the terminal, a letter is typed into it and does not switch tools, and Delete and Backspace edit the text instead of deleting a plan item.

The Scale tool needs a plan image. The Wall and Room tools also need a scale, and the tooltip then says "Set the scale first (S)". While the command line is editing the project, the tools are disabled and the Select tool is used.

## View

The zoom actions work once a plan image is loaded.

| Shortcut | Action |
|---|---|
| Ctrl+= or Ctrl++ | Zoom in one step |
| Ctrl+- | Zoom out one step |
| Ctrl+0 | Fit the whole plan in the window |
| Ctrl+1 | Show the plan at 100 percent |
| Ctrl+` | Show or hide the terminal |
| G | Snap to grid on or off (on by default) |

Each zoom step is a factor of 1.25. The zoom range is 2 percent to 3200 percent.

## Drawing and selecting

These keys and clicks work with the tools. The Select tool is the default.

| Key or click | Tool | What it does |
|---|---|---|
| Left click | Select | Select the point, wall or room under the cursor. Click on nothing to clear the selection. |
| Left button and drag on a point | Select | Move the point. The whole drag is one undo step. |
| Esc | Select | Clear the selection |
| Left click | Wall | Place a point and, after the first one, draw a wall to it. The next wall starts where the last one ended. |
| Shift | Wall | Keep the wall exactly horizontal or vertical. Hold it while you click. |
| Ctrl | Wall | Place freely, with no snapping at all. Hold it while you click. |
| Esc or right click | Wall | End the chain. The next click starts a new one. |
| Left click | Scale | Mark the first point, then the second one. |
| Shift | Scale | Keep the two points exactly horizontal or vertical |
| Esc | Scale | Cancel the measurement |
| Left click | Room | Make a room inside a closed area, or change the type of the room you click |

Shift has no effect on a wall that starts a chain, because there is no earlier point to keep straight from. Snapping to existing points always wins, so you can still close a loop while Shift is held. Ctrl needs to be held when you click, and Alt is not used because Windows gives it to the menu bar.

Dragging a point snaps it to the grid, and to Shift for straight moves. It never snaps onto another point or wall, so two points do not end up on top of each other by accident.

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

While Space is held, the cursor becomes an open hand. It closes while you drag.

Space needs the canvas to have keyboard focus. Moving the mouse over the canvas gives it focus.

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
