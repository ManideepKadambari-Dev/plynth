# Samples

Everything here was drawn or written for Plynth. Nothing was downloaded from the web or a store.

| Folder | What it holds |
|---|---|
| `plans/` | Sample plan images and the walls, rooms and openings detection should find in them |
| `models/` | Small furniture models for the assets catalog |
| `materials/` | Texture sets for the materials catalog |
| `shapes/` | A custom opening outline |
| `generators/` | Generator scripts for the build |
| `projects/` | Two finished sample projects, `apartment` and `house` |
| `tools/` | The scripts that draw the plans and build the sample projects |

`samples.json` lists the sample models, materials and projects, so the app and `plynth samples` can find them.

## Sample projects

Each folder in `projects/` is a complete project: `project.json`, `plan.json`, `export-options.json` and the plan image in `plans/`. Both use the sample models and materials, so add those to your library first (Catalog > Add the sample items, or `plynth samples add`).

| Project | What it holds |
|---|---|
| `apartment` | One floor with five rooms (two bedrooms, a bathroom, a kitchen and a living room), 16 walls, 9 doors and windows, 15 furniture items, 5 lights, and materials on every surface |
| `house` | Two floors of four rooms each, with 12 openings on each floor, a stair between them, 17 furniture items, lights, a gable roof and a flat roof over the one storey strip |

To open a copy you can change, use either way:

- In the app, go to the home screen and click a project under **Sample projects**.
- On the command line, run `plynth samples open <id> --to <folder>`, for example `plynth samples open apartment --to D:\Work`.

The samples folder itself is never changed.

`tools/make_sample_projects.py` rebuilds both projects from the sample plans using `plynth` commands: scale, detection, room types, furniture, materials, a door from the catalog, lights, set dressing, and for the house the upper floor, the stair and the roofs. It needs Blender, because it adds the sample items to a scratch library first. Run it with the program and an empty scratch folder:

```
python samples/tools/make_sample_projects.py --plynth <plynth.com> --scratch <empty folder>
```

It copies only the project files into `projects/` and updates `samples.json`. Run it in a shell whose `PLYNTH_CONFIG_DIR` keeps it away from your own settings. Add `--library <folder>` to use a library that already holds the sample items.

## License

These files are dedicated to the public domain under CC0 1.0 Universal. The full text is in `LICENSE` in this folder:

- the plan images and their expected files in `plans/`
- the models, their material files and pictures in `models/`
- the textures and `material.json` files in `materials/`
- the shape files in `shapes/`
- the sample projects in `projects/`
- `samples.json`

These files are code under GPL-3.0-or-later, like the rest of Plynth (see `LICENSE` at the top of the repository):

- the generators in `generators/`
- the scripts `tools/make_sample_plans.py`, `tools/make_sample_projects.py`, `models/make_sample_models.py` and `materials/make_sample_materials.py`

The README files are documentation. Plynth's logo and program icon are not part of the samples and are not CC0.
