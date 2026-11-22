# Getting started

This page takes you from a fresh install to a plan with walls, rooms and furniture, built as a 3D building you can walk around. It takes about fifteen minutes. Each step names the page that goes deeper.

## 1. Install Blender

Plynth uses Blender in the background to clean up the 3D models you import, to draw their thumbnails and to build your building. You never see its window.

1. Download Blender 4.2 or newer from blender.org and install it.
2. That is all. Plynth finds it by itself.

Plynth never installs or downloads anything for you. See [Setting up Plynth](SETUP.md) if Blender is somewhere unusual.

## 2. Run the setup check

The setup check opens by itself the first time you start Plynth. You can open it again from **Help > Setup check**, or with the **Fix** button on the home screen.

1. Press **Create** next to the library folder. This is where your models are kept.
2. Press **Create** next to the projects folder. This is where new projects are saved.
3. Press **Check again**. When everything required says Found, press **Done**.

## 3. Make a project from a plan image

On the home screen, press **New project from plan image** and pick a PNG, JPEG or BMP of your floor plan. To try Plynth first, click one of the **Sample plans** on the left of the home screen instead.

Zoom with the mouse wheel and pan with the middle button, or hold Space and drag. Press Ctrl+S to save the project into its own folder.

See "The plan image" in the [User guide](USER_GUIDE.md).

## 4. Set the scale

Plynth needs to know how big the drawing is.

1. Press **S**, the Scale tool.
2. Click one end of a length you know, such as a wall length written on the plan, then click the other end.
3. Type the real distance in metres and press **Set scale**.

Nothing else works in metres until this is done.

## 5. Detect or draw the walls

- **Detect.** Press **D**. Plynth proposes walls, doors, windows and rooms as dashed blue outlines. Move the sliders until they match, then press Enter to accept. Ctrl+Z takes them away again.
- **Draw.** Press **W** and click along the walls. Each click draws the next wall. Press Esc to stop.

You can mix the two: accept what detection found, then draw what it missed. Press **O** to add doors and windows that are missing. See "Detecting walls" and "Drawing walls" in the [User guide](USER_GUIDE.md).

## 6. Add rooms

Press **R** and click inside each closed area. Pick a type from the menu, such as Living room or Bedroom. If you used Detect, the rooms are already there. Click one to change its type.

The type matters for furniture. The furniture picker starts with the categories that belong in the room you click.

## 7. Import and place furniture

1. Press Ctrl+I, or **Import** in the Assets tab, and pick a model file. Plynth comes with a sofa, a bed, a table and a TV in `samples\models`. Import each one and tick a category for it, such as `sofa`.
2. Press **P**, the Furniture tool, and click in a room. A picker opens. Pick a category, then an asset, and it is placed there.
3. Press **V** to go back to the Select tool. Drag the item to move it. Drag its round handle, or press `]` and `[`, to turn it.
4. To hang the TV on a wall, press P and click the wall face in the wall view, or select the TV and press **Mount on wall**.

See "The assets catalog" and "Placing furniture" in the [User guide](USER_GUIDE.md).

## 8. Build, and look at it in 3D

1. Press Ctrl+S if the project has never been saved. A build needs the project's folder.
2. Press **Build** in the bar above the plan, or Ctrl+B. A progress bar shows each step. It takes a few seconds for a small flat.
3. When it is done, the 3D view opens. Drag with the left button to turn the building, drag with the middle or right button to pan, and use the wheel to zoom. Press Ctrl+0 to fit it in the window again. The **Top** and **Three quarter** buttons in the corner jump to two good views.
4. Press **3**, or click **Plan**, to go back to the plan. Press Alt+3 to lay the top view of the building over the plan and check that it lines up.

If the bar shows an amber **problems** button, click it. The Build tab lists what the build left out or could not finish, and clicking a line shows it on the plan. A furniture item whose asset is missing is the usual one. Fix it and press Build again.

If the 3D view stays empty, see "If the 3D view stays empty" in the [User guide](USER_GUIDE.md).

See "Building", "The 3D view" and "Build problems" in the [User guide](USER_GUIDE.md).

## Where to go next

- [User guide](USER_GUIDE.md): every screen and feature.
- [Keyboard shortcuts](SHORTCUTS.md): the full list of keys and mouse actions.
- [Command line guide](CLI_GUIDE.md): do all of this from a terminal or a script. For example, `plynth place sofa --room living` places the sofa from step 7, and `plynth build` does step 8.
- [Setting up Plynth](SETUP.md): what each item of the setup check means.
- [Writing generators](GENERATORS.md): add your own opening shapes and importers.
