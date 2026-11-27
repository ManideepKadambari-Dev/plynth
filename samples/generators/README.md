# Sample generators

Each file is one generator. Copy it into `Library/generators/` (the library setup does this for you) and place it from the picker or with `plynth place --generator <id>`.

- `column.py`: a round or square column from the floor up, with an optional plinth and cap.
- `column_round.py`: a round column with a set number of segments and an optional base and capital, each with its own height.
- `column_square.py`: a square column with an optional base and capital, each with its own height.
- `column_tapered.py`: a round column whose shaft narrows toward the top by a taper ratio, with an optional base and capital.
- `railing.py`: a straight railing with posts, a top rail and a middle rail.

A generator starts with a docstring whose first line is `plynth-generator`, followed by one JSON object that declares its name, add-ons, parameters and size rule. It defines `generate(gen)`, which makes its mesh around the origin (bottom centre at 0, 0, 0, front toward -Y) and returns the objects. See `docs/GENERATORS.md`.

Only add generators from people you trust: a generator is Python code that Blender runs during the build.

## License

The generators are Python code under GPL-3.0-or-later, like the rest of Plynth. They are not part of the CC0 samples.
