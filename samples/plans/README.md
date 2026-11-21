# Sample plans

Both plans are drawn by `samples/tools/make_sample_plans.py`, so they belong to the project. To regenerate everything, run `python samples/tools/make_sample_plans.py` from the repository root. It needs only the Python standard library, and the output is the same bytes every run.

## apartment-01

`apartment-01.png` is a 2400 x 1600 grayscale floor plan of a small apartment (two bedrooms, a bathroom, a kitchen and a living area). Scale is 100 pixels per metre. Outer walls are 30 px (0.30 m) and inner walls 12 px. It has doors with arcs, windows as double thin lines, a 2.4 m opening without a symbol, dimension lines and one curved wall that detection must leave out.

## apartment-02

`apartment-02.png` is a noisier 2000 x 1400 plan at 80 pixels per metre: dark grey walls (about 50) on off white paper (about 240) with seeded pixel noise and a light 3 x 3 blur. It has:

- outer walls 24 px (0.30 m) and inner walls 10 px (0.125 m), horizontal and vertical only
- five rooms (two bedrooms, a bath, a kitchen and a living room) with T junctions, and a 1.2 m partition that ends free inside the living room
- six doors with swing arcs, six windows as double thin lines and one opening without a symbol
- room name labels and dimension numbers drawn with a stroke font, strokes 3 px, letters about 40 px high
- dimension lines with ticks along the top and left
- furniture outlines in 2 px lines: two beds, a bath tub and toilet, a kitchen counter, a dining table with chairs, a sofa and two tables
- one solid 100 x 100 px legend block, thicker than the default maximum wall thickness

## Expected results

Each plan has an `.expected.json` file next to it: `apartment-01.expected.json` and `apartment-02.expected.json`. They hold what wall detection should find, written from the drawing code, all in image pixels:

- `points`: every corner and junction
- `walls`: centre lines split at each junction, with the thickness, and gaps of openings closed
- `rooms`: one point inside each closed area and its centre line area in px squared
- `openings`: type (`door`, `window` or `plain`), centre and width
- `not_walls`: boxes `[x0, y0, x1, y1]` around marks that must give no wall, with a kind (`dimension_line`, `door_arc`, `text`, `furniture`, `filled_block` or `curved_wall`). No box touches an expected wall band.

`pixels_per_metre` in the file turns pixels into metres.
