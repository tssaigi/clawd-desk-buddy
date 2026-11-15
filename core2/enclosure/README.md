# Clawd body for the M5Stack Core2

A two-part printed case in the shape of Clawd. The Core2's screen is his face.

- `clawd_core2_fit_test.3mf`: the front 12 mm only. **Print this first** (about 20 minutes)
  and press the Core2 in, to check the window and side opening against your unit.
- `clawd_core2_plate.3mf`: body and back cover on one plate, already oriented, with per-part
  settings for Bambu Studio, Orca and PrusaSlicer (no supports, 4 walls, 15% gyroid, 0.2 mm).
- The same parts as plain STL files, already in print orientation: body front face down,
  cover outer face down.
- `make_clawd_core2.py` and `make_3mf.py`: the parametric source (Python with trimesh,
  shapely, manifold3d). Every dimension is a named number at the top.

Size: 72 x 59 x 21.5 mm body, 85 mm wide with arms, 73 mm tall with legs. About 45 g.

Assembly: slide the Core2 in from the back, screen first. Close the cover with four
M3 x 10 self-tapping screws. The USB-C cable goes in through the opening on the left side.

Port positions come from M5Stack's dimension drawing. The fit test exists because units vary.
