# Sample models

`chair/`, `sofa/`, `bed/`, `table/` and `tv/` hold small models drawn for this project. They are made by `make_sample_models.py`, so they belong to the project and is not a downloaded model. To regenerate them, run `python samples/models/make_sample_models.py` from the repository root. It needs only the Python standard library, and the output is the same bytes every run.

## chair

Six boxes (`Back`, `Leg_BL`, `Leg_BR`, `Leg_FL`, `Leg_FR`, `Seat`) as `chair.obj` with `chair.mtl` and a 64 by 64 wood picture `chair_wood.png`. The legs and the back use the material `Wood` with the picture, the seat uses `Fabric` with a plain color.

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

## License

The models, their material files and pictures are dedicated to the public domain under CC0 1.0 (see `samples/LICENSE`). `make_sample_models.py` is code under GPL-3.0-or-later.
