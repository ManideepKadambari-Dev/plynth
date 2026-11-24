# Sample plans

The plans are drawn by `samples/tools/make_sample_plans.py`, so they belong to the project. To regenerate everything, run `python samples/tools/make_sample_plans.py` from the repository root. It needs only the Python standard library, and the output is the same bytes every run.

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

## house-01

Two 1800 x 1300 plans of one house at 100 pixels per metre, in the style of apartment-01 (outer walls 30 px, inner walls 12 px, doors with arcs, windows as double thin lines, one opening without a symbol, dimension lines). They are meant for the floor tabs: both are placed with the offset (0, 0).

- `house-01-ground.png`: the ground floor, 10 by 8 m (outer faces from x 500 to 1500 and y 300 to 1100). Four rooms: two on the left, a kitchen at the back right and, at the front right, the stair hall with the front door.
- `house-01-upper.png`: the upper floor, 9.05 by 9 m, over the left nine tenths of the ground floor (outer faces from x 500 to 1405) and reaching 1 m past it at the back (y 200 to 1100), so it leaves a strip 0.95 m wide of the ground floor open at the right. Its left and front outer corners are the same pixels as the ground floor's. Two bedrooms, a small bathroom and the landing above the stair hall.

## Expected results

Each plan has an `.expected.json` file next to it, for example `apartment-01.expected.json`. They hold what wall detection should find, written from the drawing code, all in image pixels:

- `points`: every corner and junction
- `walls`: centre lines split at each junction, with the thickness, and gaps of openings closed
- `rooms`: one point inside each closed area and its centre line area in px squared
- `openings`: type (`door`, `window` or `plain`), centre and width. Each door also has `hinge`, the hinge jamb on the wall centre line, and `swing_point`, a point a quarter of the width out from the gap centre on the side the leaf opens toward. Both are `[x, y]` in image pixels and come from the same drawing data as the leaf and arc.
- `not_walls`: boxes `[x0, y0, x1, y1]` around marks that must give no wall, with a kind (`dimension_line`, `door_arc`, `text`, `furniture`, `filled_block` or `curved_wall`). No box touches an expected wall band.

`pixels_per_metre` in the file turns pixels into metres.

## Sample shape

`samples/shapes/keyhole.shape.json` is a custom opening outline in the format of `schema/shape.schema.json`: an ellipse on top of a tapered base in the unit box, 62 points. The same script writes it (`keyhole_outline`), so `python samples/tools/make_sample_plans.py` regenerates it with the plans.

## License

The plan images, the expected files and the sample shape are dedicated to the public domain under CC0 1.0 (see `samples/LICENSE`). `make_sample_plans.py` is code under GPL-3.0-or-later.
