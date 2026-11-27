# Opening old projects

## The promise

Every Plynth release opens projects saved by any older release. You never need to convert a project by hand. When Plynth opens an old file, it upgrades it in memory. Nothing on disk changes until you save.

## What happens on the first save

The first time you save an upgraded project, Plynth copies the old files once into the project's `backup/` folder:

```
MyApartment/
  backup/
    plan.v7.json
    project.v6.json
    export-options.v3.json
```

The number is the format version the file had before the upgrade. The names above are only an example. A backup is never overwritten, so the copy always holds the file as it was before the upgrade. Later saves do not add more copies.

## A file from a newer Plynth

If a project was saved by a newer Plynth than the one you run, Plynth refuses to open it. It tells you to update Plynth. It changes nothing in the project, so you can open it later with a newer release.

## How we check

Every older file format is kept as a test file. Every build opens all of them and checks that they upgrade correctly.

## Going back to an older Plynth

1. Close Plynth.
2. Open the project's `backup/` folder.
3. Copy the backup files over the current ones, and remove the version part of the name. For example, copy `backup/plan.v7.json` to `plan.json` in the project folder.
4. Open the project with the older Plynth.

Keep a copy of the whole project folder before you do this.
