# Sketch

## Before you start

Activate an editable Body. Open **Sketch** from the containing feature's
Properties, after choosing its placement and working plane.

## Steps

1. Choose the geometry tool: for example **Rectangle**, **Circle** or **Polyline**.
   Confirm defining points with the left mouse button.
2. Use existing points and offered constraints to establish exact connections.
   Mark helper geometry as construction so it does not become part of the profile.
3. Add dimensions. After placing a new driving dimension, enter its value and
   press Enter. Reference and locked dimensions remain read-only.
4. Select and drag a point to check the remaining freedom. Edit a dimension
   again to confirm that the profile behaves as intended.
5. Choose **Finish Sketch** to return to the feature Properties, then **OK** to
   commit the feature. **Cancel edits** discards the current Sketch editing session.

## What to check

The profile must suit the consuming command: a solid usually needs a closed
region, while supported Thin operations can use an open chain. External
references remain read-only. Conflicting constraints or dimensions are rejected;
fully constrained geometry cannot move freely.

See [Sketch mode](../USER_MANUAL.md#sketch-mode) and
[dimensions and selection](../USER_MANUAL.md#dimensions-and-selection).
