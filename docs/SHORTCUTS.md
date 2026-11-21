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

The menu shows the name of the change, for example "Undo Set plan image". The items are greyed when there is nothing to undo or redo, and while the command line is editing the project.

## View

The zoom actions work once a plan image is loaded.

| Shortcut | Action |
|---|---|
| Ctrl+= or Ctrl++ | Zoom in one step |
| Ctrl+- | Zoom out one step |
| Ctrl+0 | Fit the whole plan in the window |
| Ctrl+1 | Show the plan at 100 percent |
| Ctrl+` | Show or hide the terminal |

Each zoom step is a factor of 1.25. The zoom range is 2 percent to 3200 percent.

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
