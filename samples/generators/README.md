# Sample generators

Each file is one generator. Copy it into `Library/generators/` (the library setup does this for you) and place it from the picker or with `plynth place --generator <id>`.

- `column.py`: a round or square column from the floor up, with an optional plinth and cap.
- `railing.py`: a straight railing with posts, a top rail and a middle rail.

A generator starts with a docstring whose first line is `plynth-generator`, followed by one JSON object that declares its name, add-ons, parameters and size rule. It defines `generate(gen)`, which makes its mesh around the origin (bottom centre at 0, 0, 0, front toward -Y) and returns the objects. See `docs/GENERATORS.md`.

Only add generators from people you trust: a generator is Python code that Blender runs during the build.
