# Sample models

`chair/`, `sofa/`, `bed/`, `table/`, `tv/`, `door/`, `window/`, `shelf/`, `books/`, `vase/` and `picture/` hold small models drawn for this project. They are made by `make_sample_models.py`, so they belong to the project and is not a downloaded model. To regenerate them, run `python samples/models/make_sample_models.py` from the repository root. It needs only the Python standard library, and the output is the same bytes every run.

Every face of a model has its own place on the UV square, with a small gap round it and the same scale in metres for the whole model, so no two faces share UV space. A baked map or a lightmap then has room for each face, and the export's UV check finds no overlap.

## chair

Six boxes (`Back`, `Leg_BL`, `Leg_BR`, `Leg_FL`, `Leg_FR`, `Seat`) as `chair.obj` with `chair.mtl` and a 1024 by 1024 wood picture `chair_wood.png`, which the UV layout spreads over about 1.6 metres. The legs and the back use the material `Wood` with the picture, the seat uses `Fabric` with a plain color.

- Size is 0.45 wide, 0.50 deep and 0.90 high, in metres, with 72 triangles once every face is split in two.
- The model is placed away from the origin on purpose. After Blender's default OBJ import its lowest corner is at (1.0, 2.0, 0.5), so the asset cleanup has to move it to the floor at the origin.
- The backrest sits at +Y after the default OBJ import, so the front of the chair faces -Y, as asset conventions require.

The Blender tests copy this folder to a temporary place before using it. No downloaded model is ever added here.

## Furniture models

Each is built from plain boxes, moved away from the origin like the chair, with its front facing -Y after Blender's default OBJ import and colour only materials (`Kd`, no pictures). Sizes are in metres.

| Model | Size (width x depth x height) | Front and parts | Materials | Triangles |
|---|---|---|---|---|
| `sofa/sofa.obj` | 2.00 x 0.90 x 0.85 | Back at +Y, arms at both ends of X, four short feet | `Fabric`, `Legs` | 96 |
| `bed/bed.obj` | 1.60 x 2.10 x 1.00 | Headboard at +Y, frame, mattress | `Wood`, `Linen` | 36 |
| `table/table.obj` | 1.20 x 0.80 x 0.75 | Top and four legs | `Wood` | 60 |
| `tv/tv.obj` | 1.23 x 0.06 x 0.71 | Flat panel for a wall. The `Screen` box is a thin separate object on the -Y face | `Plastic`, `Screen` | 24 |

The thumbnail camera stands at +X, -Y (front right in Blender's views), so a thumbnail shows the front and the model's own left side. On the sofa that is the arm at +X.

## Finishing models

Built the same way, for catalog doors and windows and for set dressing. Import them with the category in the second column, and the tag in the third where one is given.

| Model | Category, tag | Size (width x depth x height) | Parts |
|---|---|---|---|
| `door/door.obj` | `door` | 0.90 x 0.05 x 2.10 | Frame, leaf and handle. The leaf hangs on its left jamb seen from the front (low X), the handle is on the right |
| `window/window.obj` | `window` | 1.20 x 0.08 x 1.20 | Frame round one pane, material `Glass` |
| `shelf/shelf.obj` | `shelf` | 0.80 x 0.30 x 1.80 | Two sides, a back, four boards and a top, so the cleanup measures five supports |
| `books/books.obj` | `decor`, `book` | 0.30 x 0.20 x 0.25 | A row of five books |
| `vase/vase.obj` | `decor`, `tabletop` | 0.15 x 0.15 x 0.30 | Body and neck |
| `picture/picture.obj` | `decor`, `wall_art` | 0.60 x 0.03 x 0.45 | Frame and canvas, flat at the back |

The files of these six folders are written by the script on the next run of it; until then only the script holds them.

## License

The models, their material files and pictures are dedicated to the public domain under CC0 1.0 (see `samples/LICENSE`). `make_sample_models.py` is code under GPL-3.0-or-later.
