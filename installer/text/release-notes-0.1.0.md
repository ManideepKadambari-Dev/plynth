Plynth turns a 2D floor plan image into a game-ready 3D building for Unreal Engine. Load a plan, mark walls, rooms, openings and furniture, and press Build. Blender builds the building in the background and Plynth exports it as FBX with textures. Everything the app does is also available from the command line, so scripts can make buildings without the window.

This is the first early preview. It is free and open source.

## What it does

- Load a plan image, pan and zoom, set the scale, and draw walls, points and rooms by hand, or detect walls with sliders and edit the result.
- Openings with shapes you can add, a wall view, wall heights, curved walls and rounded corners.
- Several floors, stairs and roofs for each section.
- A furniture and assets catalog, with import from your own model files, thumbnails and a library folder you control.
- Materials catalog and material import, with different looks for the inside and outside wall faces, and baking.
- Optional imports from BlenderKit and from Fab zip files. Add a BlenderKit item by the item's page address or the copied asset ID. A Get link is refused with a message.
- Generators and add-on bridges for things such as columns and cabinets.
- Set dressing, variations, gameplay data, light markers and trim.
- A real 3D preview of the build result.
- Export as one FBX with textures and collision, as groups, or as a modular kit, with a quality check before export.
- A setup check, `plynth doctor`, and a full command line with JSON answers, a render command and a developer guide.
- A live view in the app of builds started from the command line, and `plynth render --plan` to render a plan.
- An `AGENTS.md` guide in each new project, for your own coding assistant.
- Help pages inside the app, sample projects and a license report.
- Assets, Materials, Sources, Build report and Export each open in their own window, and the properties panel can be closed.

## Downloads

- `PlynthSetup.exe`: the installer. It installs for your user only, needs no administrator rights, adds a Start menu entry and puts `plynth` on your command line path.
- `Plynth-0.1.0-windows-x64.zip`: the portable copy. Unpack it anywhere and run `plynth.exe`. It changes no PATH.
- `Plynth-0.1.0-third-party-sources.zip`: the exact sources of the libraries Plynth uses. It holds no program.
- `SHA256SUMS.txt`: checksums of the files above. To check one in PowerShell, run `Get-FileHash <file>` and compare the result with the line in this file.

Plynth's own source code is the tag `v0.1.0` of this repository.

## Requirements

- Windows 10 or 11, 64-bit.
- Blender, installed separately. Plynth was developed with Blender 5.2, and the setup check accepts 4.2 or newer. Plynth finds it on first run, and you can point to it by hand.
- The Microsoft Visual C++ runtime (x64), if your PC lacks it. The installer links to Microsoft's page. Plynth does not include it.

## Known items

- **The builds are not code signed.** Windows SmartScreen may warn the first time you start the installer or the program. Choose "More info", then "Run anyway". Smart App Control may block it with no such choice. Plynth does not ask you to turn any protection off.
- **Updates.** An installed copy checks the releases page of this repository when it starts. You can switch this off in Help, in the setup check, or with `plynth settings set --update-check false`. Use Help, Check for updates, then Update Plynth, or run `plynth update` (`--check` only looks, `--status` shows the state). Only installed copies check. Every update is verified against the owner's signature before it runs. Your settings, library and projects are kept. The portable zip never checks. Update it by replacing the folder.
- **The update signature is not Windows code signing.** It protects updates only and does not remove the SmartScreen warning.
- **This is a preview.** The plan file format may change between releases. Older plans will be migrated when you open them, but keep a copy of projects you care about.
- **Import into Unreal Engine is untested** for this release. The export follows the Unreal rules for scale, naming, pivots and collision.
- **BlenderKit and Fab imports need your own account.** You follow each store's terms. Plynth never bundles store assets and never calls private store endpoints.

## License

The code is GPL-3.0-or-later. The sample art and sample projects are CC0. The logo is not CC0. The licenses of the libraries Plynth uses are in the `licenses` folder of the download.
