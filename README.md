# Plynth

Turn a 2D floor plan image into a finished, game-ready 3D building for Unreal Engine.

Load a plan, mark walls, rooms, openings and furniture, then click Build. Blender constructs the building in the background and Plynth exports FBX files with textures and collision, ready to import.

Everything the app does is also available from the command line.

## Status

Early development. Not ready for use yet.

## Requirements

- Windows 10 or 11
- Blender (installed separately)

## Building from source

### Prerequisites

- Visual Studio 2026 with the "Desktop development with C++" workload.
- Git.
- vcpkg, with the `VCPKG_ROOT` environment variable pointing at its folder.
- Qt 6.8 with the MSVC 64-bit kit, with the `QTDIR` user environment variable pointing at the kit folder, for example `C:\Qt\6.8.3\msvc2022_64`.

Get Qt from the Qt online installer (free account needed), or with `aqtinstall` if you prefer no login:

```
pip install aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt
```

Set `VCPKG_ROOT` and `QTDIR` as user variables, then restart any open terminal or Visual Studio so they are picked up.

### Visual Studio

1. Start Visual Studio after the variables are set.
2. File > Open > Folder, and pick the repository folder.
3. Choose the `msvc` configure preset and the Debug configuration.
4. Choose `plynth.exe` as the startup item and press F5.

The first configure downloads and builds the vcpkg packages, so it takes a few minutes. One of them, libvterm (used by the built-in terminal), comes from the overlay port in `ports/`. vcpkg finds it through `vcpkg-configuration.json`, so there is nothing extra to install.

### Command line

Run these from the Developer PowerShell for Visual Studio. CMake ships inside Visual Studio and is on PATH there.

```
cmake --preset msvc
cmake --build --preset debug
ctest --preset debug
```

The app is built to `out/build/msvc/app/Debug/plynth.exe`. Next to it is `plynth.com`, a small console program with the same command line. Use it from a terminal so the answer is printed before the prompt returns. See `docs/CLI_GUIDE.md`.

### Screenshots

`plynth.exe --screenshot <file> --size <WxH>` opens the window, saves a PNG of it at that size and exits. For example:

```
plynth.exe --screenshot window.png --size 1280x720
```

## License

Plynth is free software under the GNU General Public License version 3 or later. See LICENSE.
