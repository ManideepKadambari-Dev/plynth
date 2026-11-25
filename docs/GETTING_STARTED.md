# Getting started

This page gets you from a fresh install to your first building in about ten minutes. You use the Apartment sample that comes with Plynth, so you do not need a plan image of your own yet.

## 1. Install Blender and run the setup check

Plynth uses Blender in the background to clean up models, draw thumbnails and build your building. You never see its window.

1. Download Blender 4.2 or newer from blender.org and install it. Plynth finds it by itself and never installs it for you.
2. The setup check opens the first time you start Plynth. Press **Create** next to the library folder and next to the projects folder, then **Check again**. When everything required says Found, press **Done**.

You can open the setup check again from **Help > Setup check**. See [Setting up Plynth](SETUP.md) if something is not found.

## 2. Add the sample items

The Apartment uses the sample sofa, bed, table and other models, and nine sample materials. Add them to your library once.

1. Open the **Catalog** menu and choose **Add the sample items**.
2. Wait a short while. Blender runs in the background and draws a thumbnail for each model and material.

Without them the build still works, but furniture is left out and surfaces get a plain color.

## 3. Open the Apartment sample

1. On the home screen, find **Sample projects** and click **Apartment**.
2. Plynth opens a copy in your projects folder. Change it as much as you like. The original stays as it was.

## 4. Look at the plan and the 3D view

The plan shows the walls, five rooms, nine doors and windows, and the furniture on top of the plan image.

- Zoom with the mouse wheel. Pan with the middle button, or hold Space and drag.
- Click a wall, a room or a piece of furniture. The Properties panel shows its details.
- Press **3**, or click **3D**, to see the model. Drag with the left button to turn it, drag with the middle or right button to pan, and use the wheel to zoom. Press Ctrl+0 to fit it in the window. The **Top** and **Three quarter** buttons jump to two good views.
- Press **3** again, or click **Plan**, to go back.

The 3D view is empty until the first build, so do the next step to fill it.

## 5. Build

1. Press **Build** in the bar above the plan, or Ctrl+B. A progress bar shows each step. It takes a few seconds for a small flat.
2. When it is done the 3D view opens, with the materials on the walls, floors and doors. Turn it around and look at each room.

If the bar shows an amber **problems** button, click it. The Build report window lists what the build left out or could not finish. Click a line to see it on the plan. The Apartment sample should build without problems. If it does not, check that step 2 finished.

## 6. Look at the result and export

1. Press Ctrl+Shift+E to run the quality checks without writing anything. The Export window lists each check. Blue means it passed.
2. Press Ctrl+E, or **Export**. Plynth writes the project's `export` folder: `Apartment.fbx`, a `Textures` folder and `export_report.json`.
3. Press **Open folder** in the Export window to see them. The textures are found by relative paths, so you can copy the whole folder somewhere else. In Unreal, import the FBX with Combine Meshes off and keep `Textures` beside it.

If a check fails, the window says "Export blocked" and names the check. Click a finding to see it on the plan.

## Do it from a terminal

The same ten minutes, with the command line. Run these in a terminal. The [Command line guide](CLI_GUIDE.md) explains each command.

```
plynth samples add
plynth samples open apartment --to D:\Work
plynth build --project D:\Work\Apartment
plynth export --project D:\Work\Apartment
```

`samples add` is step 2, `samples open` is step 3, `build` is step 5 and `export` is step 6.

## Your own plan

When you are ready to use your own drawing:

1. On the home screen press **New project from plan image** and pick a PNG, JPEG or BMP.
2. Press **S**, click both ends of a length you know, type the distance in metres and press **Set scale**.
3. Press **D** to detect the walls, doors, windows and rooms, adjust the sliders and press Enter to accept. Or press **W** and click along the walls to draw them.
4. Press **R** and click inside each closed area to make a room and pick its type.
5. Press **P** and click in a room to place furniture from your library.
6. Build and export as above.

See the [User guide](USER_GUIDE.md) for each of these in full, including second floors, stairs and roofs. The House sample shows all three.

## Where to go next

- [User guide](USER_GUIDE.md): every screen and feature.
- [Keyboard shortcuts](SHORTCUTS.md): the full list of keys and mouse actions.
- [Command line guide](CLI_GUIDE.md): do all of this from a terminal or a script.
- [Setting up Plynth](SETUP.md): what each item of the setup check means.
- [Writing generators](GENERATORS.md): add your own generators, opening shapes, importers and material importers.
- In the app, F1 opens these pages in the Help window.
