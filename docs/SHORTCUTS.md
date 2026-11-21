# Keyboard shortcuts

Every action with a shortcut is also in the menu, where the shortcut is shown next to its name.

## File

| Shortcut | Action |
|---|---|
| Ctrl+O | Open a plan image |
| Alt+F4 | Exit Plynth |

## View

These actions work once a plan image is loaded.

| Shortcut | Action |
|---|---|
| Ctrl+= or Ctrl++ | Zoom in one step |
| Ctrl+- | Zoom out one step |
| Ctrl+0 | Fit the whole plan in the window |
| Ctrl+1 | Show the plan at 100 percent |

Each zoom step is a factor of 1.25. The zoom range is 2 percent to 3200 percent.

## Mouse

| Action | What it does |
|---|---|
| Mouse wheel | Zoom in or out. The point under the cursor stays where it is. |
| Middle button and drag | Pan the plan |
| Hold Space, then left button and drag | Pan the plan |
| Drop an image file on the window | Open it |

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
plynth.exe --image plan.png
```

This opens the window with the image already loaded. To save a picture of the window, add `--screenshot out.png --size 1280x720`.
