# Sample models

`chair/` holds a small chair drawn for this project. It is made by `make_sample_models.py`, so it belongs to the project and is not a downloaded model. To regenerate it, run `python samples/models/make_sample_models.py` from the repository root. It needs only the Python standard library, and the output is the same bytes every run.

## chair

Six boxes (`Back`, `Leg_BL`, `Leg_BR`, `Leg_FL`, `Leg_FR`, `Seat`) as `chair.obj` with `chair.mtl` and a 64 by 64 wood picture `chair_wood.png`. The legs and the back use the material `Wood` with the picture, the seat uses `Fabric` with a plain color.

- Size is 0.45 wide, 0.50 deep and 0.90 high, in metres, with 72 triangles once every face is split in two.
- The model is placed away from the origin on purpose. After Blender's default OBJ import its lowest corner is at (1.0, 2.0, 0.5), so the asset cleanup has to move it to the floor at the origin.
- The backrest sits at +Y after the default OBJ import, so the front of the chair faces -Y, as asset conventions require.

The Blender tests copy this folder to a temporary place before using it. No downloaded model is ever added here.
