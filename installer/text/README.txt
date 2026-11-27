Plynth 0.1.0 preview

Plynth turns a 2D floor plan image into a game-ready 3D building. You load a
plan, mark walls, rooms, openings and furniture, and press Build. Blender
builds the result in the background and Plynth exports it as FBX with
textures, ready for Unreal Engine. Everything the app does is also a command
on the command line.

This is an early preview for Windows 10 or 11, 64-bit.

Starting Plynth
  App:           run plynth.exe
  Command line:  open a terminal and run plynth help
                 The program is cli\plynth.com. Do not run it by double click.
                 It starts plynth.exe and passes the answer through.
  The installer adds the cli folder to your PATH. In the portable copy it does
  not, so use the full path, or add the cli folder to your PATH yourself.

What you need
  Blender, installed on its own. Plynth does not include it.
  Plynth was developed with Blender 5.2 LTS. The setup check accepts 4.2 or
  newer.
  The Microsoft Visual C++ runtime (x64). If Plynth does not start and
  Windows names a missing vcruntime or msvcp file, install it from
  https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist

How Plynth finds Blender
  It looks in the usual places: Program Files, your local programs folder,
  the list of installed programs, the Microsoft Store, Steam and PATH. If you
  installed Blender somewhere else, open Help, Setup check and press Browse.
  The --blender option and the PLYNTH_BLENDER variable beat the saved choice.

Your files
  Library folder   your assets and materials. Suggested: Documents\Plynth\Library
  Projects folder  your projects.  Suggested: Documents\Plynth\Projects
  Settings         %APPDATA%\Plynth\settings.json
  Choose or create the two folders in Help, Setup check. It opens by itself
  the first time you start Plynth.

Help
  Help menu, then the page you want. The pages are the Markdown files in the
  docs folder, so you can also read them in any text editor. Start with
  docs\GETTING_STARTED.md. docs\CLI_GUIDE.md lists every command.
  docs\AI_INTEGRATION.md explains how to connect your own coding
  assistant to the command line. SKILL.md in this folder is the Plynth
  skill for such an assistant; that page says where to put it.

License
  See LICENSE. The code is GPL-3.0-or-later. The sample art and sample
  projects are CC0. The logo is not CC0. AUTHORS names the authors. The
  licenses of the libraries Plynth uses are in the licenses folder.

Removing the portable copy
  Close Plynth and delete the Plynth folder. Nothing else was changed on your
  PC. Your library, projects and settings stay where they are. To remove them
  too, delete those folders and %APPDATA%\Plynth.
  An installed copy is removed from Windows Settings, Apps. It also keeps
  your library, projects and settings.

Updates
  An installed copy checks the releases page on GitHub when it starts. Turn
  this off in Help or the setup check, or run
  plynth settings set --update-check false
  Help, Check for updates, then Update Plynth installs a new version. On the
  command line use plynth update (--check only looks, --status shows the
  state). Every update is verified against the owner's signature before it
  runs. Your settings, library and projects are kept.
  The portable copy never checks. To update it, replace the folder.
  This signature is separate from Windows code signing. It does not remove
  the SmartScreen warning.
