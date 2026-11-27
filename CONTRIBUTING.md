# Contributing to Plynth

Thanks for helping. This page covers how to build Plynth, how a change is checked and the rules the code follows.

## Platform

Plynth builds on Windows only.

## What to install

- Visual Studio 2026 with the "Desktop development with C++" workload.
- Qt 6.8 with the MSVC 64-bit kit.
- vcpkg.
- Git.
- Blender 4.2 or newer. The tests that build real geometry need it. Plain builds do not.

Set two user environment variables, then restart any open terminal or Visual Studio:

- `QTDIR`: the Qt kit folder, for example `C:\Qt\6.8.3\msvc2022_64`.
- `VCPKG_ROOT`: the vcpkg folder.

Run `tools\check-setup.cmd` first. It only looks at your machine and tells you what is missing. It installs nothing.

## Build and test

In Visual Studio, use File > Open > Folder and pick the repository folder. Choose the `msvc` configure preset and the Debug configuration, then run `plynth.exe`. The first configure builds the vcpkg packages, so it takes a few minutes.

From the Developer PowerShell for Visual Studio:

```
cmake --preset msvc
cmake --build --preset debug
ctest --preset debug
```

The README has more on other terminals and on screenshots of the window.

The tests that run real Blender work only on a local machine. They never run on GitHub.

## How a pull request is checked

GitHub builds Debug and runs the tests on every push to a branch, except the Blender tests. A newer push to the same branch cancels the older run. A merge to `main` and a release tag also build Release and the installer. Please run the tests locally before you push.

## Folder layout

```
core/      plan model, commands, detection, catalogs, validation. No UI
app/       Qt windows and views
cli/       command line front end
blender/   Python: build steps, generators, importers, exporters, checks
schema/    JSON Schema for the plan and project files
samples/   example projects and plan images
docs/      help pages
tests/     tests
```

## Code rules

- C++20.
- `core/` has no UI code and no Qt Widgets includes.
- The app and the CLI are thin layers over `core`. Every feature works from both, and neither holds logic the other lacks.
- Every change to the plan is a command class. A view never edits the plan directly.
- Roof types, opening shapes, importers, exporters, generators and quality checks are registry entries. Adding one must not mean editing existing code.
- All plan values are in metres.
- File formats carry a version number. When a format changes, add a migration step.
- Write tests for `core/` as you build each piece.
- Colors, fonts and spacing come from the one theme file.
- Sample plan images must be drawn for the project. Do not add images taken from the web.

## Comments

Put one short comment above each function, in plain words, saying what it does. Explain why only when the reason is not obvious. No comments that repeat the code line by line, and no banner or divider comments.

## Wording

Use plain, direct words and short sentences, in code, comments, docs and messages. No em dashes, no double dashes used as punctuation, no decorative symbols.

## License

Plynth is free software under the GNU General Public License, version 3 or later. By contributing you agree your code is released under it. Files in `samples/` are CC0 1.0.
