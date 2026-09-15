# Dimension editing audit (2026-09-10)

## Update 2026-09-14: enlarging an axis-constrained rectangle

In `part02.prtz`, the final Extrusion's internal-sketch dimensions could be reduced,
but enlarging them was rejected as a conflict. Both CLI and View editing exhibited
the error. Neither an upper dimensional bound nor body calculation caused it.

The point-on-line solver shortcut found the intersection between the line and a
circle of the requested distance. Enlargement moved a point along the axis and
broke its horizontal/vertical constraint to another point. The next iteration
moved it back, causing oscillation. Reduction had no such intersection and correctly
used movement of the complete group of points sharing a coordinate.

The intersection shortcut now rejects candidates that split a coordinate shared
through horizontal/vertical constraints, including transitive constraints. Existing
group-movement solving handles them. A truly free point can still slide along its
line; genuinely impossible changes are rejected without a commit.

Verification:

- Before the fix, regression failed on the first width enlargement. Afterward it
  passed in all four quadrants, both point orders, direct/transitive constraints,
  repeated enlargement/reduction, reopened sketches and atomic rejection of edits
  blocked by a fixed point. A separate case protects free sliding along a line
  without horizontal/vertical constraints.
- On a working copy of `part02.prtz`, both dimensions accepted 20, 32, 40 and
  50 mm through `sketch.dimension.set`, each followed by explicit body regeneration.
  Tests do not overwrite the original user file.
- `ZIMA_VERIFY_PROFILE_DIMENSION_FILE` runs the same GUI test on a saved Part:
  picker and View double-click, save, Undo, open Properties, enter the owned
  sketch, and OK/Cancel transactions. It passed on a copy of `part02.prtz`, as did
  the normal `ZIMA_VERIFY_PROPERTY_SKETCH_ONLY` matrix for Part and Assembly.
- `sketcher_contract_tests`, `sketch_dimension_command_tests`,
  `profile_sketch_command_tests`, `contract_tests`, and
  `dimension_layout_contract_tests` passed. GUI checks verify events and data;
  offscreen execution does not replace visual inspection of OpenGL rendering.

## Update 2026-09-11: Body-Origin offset and dimension plane

A user's 16 mm edit reverted after Enter. The View offered derived X because
dimension inputs lacked the Body Origin. Editing changed X, but the saved YZ-plane
reference still required a 16 mm offset; placement solving correctly restored it.

Display and direct editing now use saved references in owner coordinates,
including Body Origins. The dimension addresses the corresponding reference offset.
Displayed placement dimensions are transformed into the owning Body's position.
Shared placement calculations and rules are unchanged.

Profile-plane offset dimensions have extension lines along the profile's local
X axis and measure along its normal. They therefore lie in the local Origin plane,
rather than a plane derived from global diagonal vector `{5,5,0}`. The owned
sketch does not insert the same dimension again. Revolution no longer rotates an
already resolved profile direction a second time through container rotation.

Verification of this update:

- `ZIMA_VERIFY_BODY_REFERENCE_DIMENSION_ONLY=1`: edit 16 → 17 mm, save/reopen and
  compare the calculated-body fingerprint; also translate/rotate the owning Body
  and check its dimension's spatial placement.
- `ZIMA_VERIFY_PROFILE_OFFSET_PLANE_ONLY=1`: rotated XY/XZ/YZ profiles, length and
  Up To termination, measurement/extension-line direction, dimension-plane normal
  and exactly one offset instance. Display checks require neither body calculation
  nor a selected target for pending Up To.
- Both belong to `ZIMA_VERIFY_DIMENSION_EDITS_ONLY=1`. The complete editing,
  Properties, Cancel, OK and persistence matrix passed.
- The loaded user model passed reference editing 16 → 17 mm and geometric checking
  of the 20 mm profile-offset dimension plane.

Targeted runs used `QT_QPA_PLATFORM=offscreen` and `--verify-startup`. They verify
editor events and dimension geometry. This offscreen environment provides no
OpenGL context, so they do not inspect rendered pixels.

## Original audit scope

Input is a numeric change to an existing dimension. Output must be corresponding
geometry and saved state, or rejection without a commit. Merely rewriting a label
or reporting success does not prove recalculation.

## Corrected paths

- Direct editing of an Extrusion/Revolution internal-profile offset also updates
  `Sketch::plane_offset`. Existing placement solving runs before calculation to
  prevent the old sketch-Origin position from reaching the body.
- Two-sided Revolution's reverse angle supports direct View editing.
- Hole has direct handlers for depth, plain-hole diameter, thread length, chamfer
  depth/angle and drill-point angle. Catalog thread diameter remains measured;
  thread designation uses catalog selection.
- Shell sends edited thickness to open Properties. A dialog without an active
  placement-reference field is no longer bypassed by a direct document write.
  Cancel therefore discards the pending dimension change.

## Regression coverage

| Path | Verification |
| --- | --- |
| Boxes, cylinders, spheres, cones, pyramids, wedges; all offered dimensions | `zima_cpp_dimension_edits_ui_contract` |
| X/Y/Z translation, Shell, numeric hole/thread dimensions | Same matrix: View → Properties → Cancel → Properties → OK → save/reopen |
| Profile-plane offset, Origin reference, reverse Revolution angle | `zima_cpp_profile_frame_ui_contract`: double-click the actual label, Enter, check sketch placement and calculated-body bounds |
| Profile orientation and reopening Properties | XY/XZ/YZ, Front/Back and four rotations; nonzero dimension available after reopening |
| Internal-sketch dimensions | `ZIMA_VERIFY_PROPERTY_SKETCH_ONLY`: actual picker and double-click, repeated changes, enter Sketcher, OK/Cancel, Part and Assembly |
| Locks, numeric inputs and other dialogs | `zima_cpp_ui_contract_tests`, `numeric_value_locks`, `numeric_fields`, `sketcher_contract_tests` |
| Drawing/measured dimensions, presentation, Assemblies | Existing Drawing, Measurement, DimensionLayout, Viewer and Assembly contract tests |

Besides saved values, the matrix compares the saved result's input fingerprint
with current document operations. Profile tests independently compare spatial body
bounds with calculations from expected inputs.

Run graphical tests sequentially in one graphical session. Concurrent test windows
can steal focus. On Linux without working offscreen OpenGL, use `-platform xcb`.

This is a finite matrix of specific interaction paths, not proof of every geometry
and constraint combination. The disappearance of the exact 18 mm dimension in the
user's `06.png` cannot be established from a screenshot with Properties closed;
that combination requires the saved model. Test models check nonzero-reference
visibility in a rotated view. Intentional suppression when a dimension's measured
line projects to a point when viewed exactly along it remains part of general
dimension presentation.

## Run results

- The 26-parameter matrix passed: direct editing, pending changes without a commit,
  Cancel, subsequent OK and reopening the saved result.
- Profile tests passed actual label double-click, offset, Origin reference and
  two-sided Revolution reverse angle.
- The broader startup test also ran internal-sketch dimension transactions in
  Part/Assembly, plus model, Drawing, Measurement, Viewer and lock contract suites.
- The offscreen appearance test lacked an OpenGL context; its XCB rerun passed.
