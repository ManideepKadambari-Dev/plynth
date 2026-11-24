# Sample materials

Nine materials drawn for Plynth by `make_sample_materials.py`. Nothing here was downloaded, and the drawing
is the same every time it runs. Each folder is a texture set with a `material.json` that gives its id, name,
groups, tags, license (`own_work`) and tile size.

| Id | Tile size (m) | Maps |
|---|---|---|
| `white_paint` | 1.0 x 1.0 | base color, roughness value |
| `red_brick` | 1.2 x 0.6 | base color, normal, roughness |
| `oak_planks` | 2.0 x 2.0 | base color, DirectX normal, roughness |
| `grey_tile` | 0.6 x 0.6 | base color, normal, roughness, occlusion |
| `painted_wood` | 1.0 x 1.0 | base color, normal, roughness |
| `brushed_steel` | 0.5 x 0.5 | base color, roughness, metallic value 1 |
| `clear_glass` | 1.0 x 1.0 | values only: color, roughness 0.05, opacity 0.2 |
| `roof_tiles` | 1.0 x 1.0 | base color, normal, roughness (four rounded clay tiles across, five courses) |
| `concrete` | 2.0 x 2.0 | base color, roughness (panel joints 1 m apart) |

The file names use different styles on purpose, so the name matching of the importer is exercised.

Blender must not use your own settings folders for these commands. Set all five user variables to a folder you can throw away first (PowerShell):

    $p = "$env:TEMP\plynth-blender-private"
    foreach ($d in "", "config", "scripts", "extensions", "datafiles") { New-Item -ItemType Directory -Force "$p\$d" | Out-Null }
    $env:BLENDER_USER_RESOURCES = $p
    $env:BLENDER_USER_CONFIG = "$p\config"
    $env:BLENDER_USER_SCRIPTS = "$p\scripts"
    $env:BLENDER_USER_EXTENSIONS = "$p\extensions"
    $env:BLENDER_USER_DATAFILES = "$p\datafiles"

To draw them again:

    blender --background --factory-startup --python samples/materials/make_sample_materials.py

To draw only some of them, name them after `--`:

    blender --background --factory-startup --python samples/materials/make_sample_materials.py -- roof_tiles concrete

To use them, import the folders into the materials catalog (the app's "Add the sample materials", or
`plynth materials import samples\materials\*`).

## License

The textures and `material.json` files are dedicated to the public domain under CC0 1.0 (see `samples/LICENSE`). `make_sample_materials.py` is code under GPL-3.0-or-later.
