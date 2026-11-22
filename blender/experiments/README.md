# BlenderKit background test

This is an optional experiment. It is not part of Plynth. It answers one question: can the BlenderKit add-on download an asset when Blender runs in the background (`blender -b`)? Plynth runs it only that way.

## What it tests

The script `blenderkit_background.py` checks that the add-on is installed and enabled and that you are signed in (it only asks whether a key is stored, it never reads out or prints the key). Then it asks the add-on's own download operator, `scene.blenderkit_download`, to fetch one asset by its base id, and waits up to 5 minutes. In background mode Blender does not run the add-on's timers by itself, so the script runs the add-on's own timer functions by hand while it waits.

The script never calls a BlenderKit web address itself. It copies a downloaded `.blend` only into the folder you give with `--out`, and it writes `report.json` there with: the add-on version, whether you are signed in, each step tried, whether a file arrived, where it is, how long it took, and every error text the add-on logged.

## Before you run it

1. Open Blender normally, enable BlenderKit and sign in to your own BlenderKit account inside Blender. Close Blender.
2. Pick an asset you may download for your own use and note its base id. Open the asset's page on blenderkit.com and copy the long id (letters, digits and dashes) from the address of the page.

The downloaded asset is for your own use only. Do not add it to the Plynth repository or share it.

## The command

Use your own Blender path if it differs. Run it from the Plynth repository folder.

```
"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b --python blender/experiments/blenderkit_background.py -- --asset-base-id <id> --out C:\temp\bk_background
```

The script refuses to run in a Blender with a window. It runs the add-ons you have enabled with your own settings, so close other Blender windows first.

Do not add `--factory-startup`: the add-on and your sign in live in your normal Blender settings. An optional `--timeout <seconds>` changes the 5 minute wait.

## What to send back

Send the `report.json` file and the text Blender printed in the console. The `result` field in the report says what happened: `downloaded`, `timeout`, `failed`, `not_signed_in`, `addon_not_installed`, `addon_not_enabled`, `download_not_started` or `script_error`.

The script never switches the add-on on. If it is not enabled in your normal Blender the report says `addon_not_enabled`; enable it in Preferences and run again.
