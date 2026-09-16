# Placement on Sketch geometry and body curves

## Status — 2026-09-16

Implemented in the native shared placement path. The user explicitly approved
the extension for Sketch geometry and original solid edges, with existing Point,
Axis and Plane meanings preserved. This document records the behavior and its
verification requirements.

## Inputs, means and outputs

- Inputs: persisted source points, curves, faces and axes, the current container
  frame, reference order and editable placement values.
- Means: the shared ZIMA reference model, native curve definitions and placement
  solver. Picking and editing must not call OCCT.
- Output: an associative container frame with visible XYZ and RX/RY/RZ values,
  stable source references and correct remaining degrees of freedom.

## Agreed reference meanings

- A point fixes the origin position.
- A selected curve constrains the origin to the curve, including a circle's
  circumference. This applies to Sketch curves and original solid edges.
- A planar face provides its support plane; an axis provides its line and
  direction. These retain their existing placement meanings.
- A point on a curve followed by that curve can orient the container's FRONT
  plane perpendicular to the tangent at the selected point. The existing
  FRONT/TOP frame convention remains authoritative.
- Use an explicit center point to request a circle's center, and an explicit
  axis to request its axis. Do not infer either from picking the circumference.
- For drill-tip placement, a circular bottom face and the bore axis express the
  intended support and centering without overloading the circular edge.
- Keep XYZ and RX/RY/RZ visible. References determine which values are computed
  and which remain editable. A free curve parameter couples the Cartesian
  coordinates; editing one must not detach the origin from the curve.

## Implementation

- The common viewer candidate list admits native Sketch points and curves only
  when their stable identity is present in the allowed reference packet.
- Sketch packets use native point IDs and exact rational curves in their resolved
  frame. Body transforms and Assembly occurrence paths retain source ownership.
- Explicit OCCT calculation captures finite original edges as exact rational
  curves in the already persisted `exact_spline` field. Native Sketch curves need
  no kernel calculation. Older calculated packets obtain this data on explicit
  Regenerate; opening properties never reconstructs it through OCCT.
- Position solving combines exact point-on-curve equations with the existing
  point, axis and plane constraints. Curve parameters stay inside their trimmed
  domains. An actual Axis, including a Sketch centerline, remains infinite.
- Tangent evaluation uses the rational derivative rather than display chords.
  Ambiguous tangents at corners, cusps and self-intersections cannot orient a
  container. Closed-curve seams with a consistent tangent remain usable.
- XYZ controls use the constraint rank at the current position. A free Cartesian
  coordinate can move the origin along the curve; constrained coordinates are
  computed. The existing rotation and correction controls remain available.
- Native history order controls publication: resolve a source container and its
  Sketch frame before resolving consumers. A pending property preview is evaluated
  after its inputs. This uses the existing history and ownership rules.

## Independent geometric checks

For a circle of radius 10 in XY, its point `(10, 0, 0)` has tangent `(0, 1, 0)`.
The old center axis has direction `(0, 0, 1)`. These are perpendicular, so the old
axis interpretation cannot implement the requested tangent placement.

A regular point-on-curve constraint leaves one positional degree of freedom.
Fixing a particular point removes that freedom. Aligning FRONT to one tangent
leaves one rotation about that tangent; a further independent direction or an
angle can determine it.

## Verification boundaries

- Evaluate native curve definitions, not rendering chords. Preserve curve range,
  direction and exact source IDs, including occurrence paths in Assemblies.
- Respect model history and source ownership; reject self/later dependencies.
- Preserve the saved resolved coordinates as the local solve seed. On a closed
  curve, repeated solving retains the current solution. A specifically selected
  native point determines its own location and follows source edits.
- A missing source, incompatible references or undefined tangent must leave the
  last valid frame intact and mark the reference invalid. Do not move the origin
  to a different point merely to make an inconsistent reference succeed.
- Exercise lines, circles, arcs, ellipses and splines; endpoints and seams; curve
  reversal; source edits/removal; transformed Bodies and repeated occurrences;
  native save/reopen; Undo/Redo; and remaining XYZ/rotation controls.
- Preserve and run existing Point/Axis/Plane, multiple-plane, FRONT/TOP, flip,
  offset and drill-tip tests. Verify GUI and CLI through the same reference path.

The dedicated `zima_cpp_curve_placement_contract_tests` exercises the geometric
matrix and native transactions. `zima_cpp_ui_contract_tests` also verifies that
the common picker hovers and confirms the same native Sketch point/curve.

The current drill-tip feature already selects circular bottom faces through its
own operation-specific list. Its selection and radius derivation must remain
distinct from the generic placement reference rule.

## Verification result — 2026-09-16

The Windows GUI was rebuilt. All 28 selected regression suites passed, including
the final reruns after the reference-packet and GUI-test corrections:

- Exact curves: six curve types, three locations, two coordinate frames and both
  orientation flips; finite endpoints, closed seams and ambiguous tangents.
- Source changes and deletion, translated Bodies, conflicting existing planes,
  earlier/later history ownership, native save/reopen and Undo/Redo.
- Existing placement, datum, offset, primitive/profile, drill, section, sweep,
  multibody and external-reference contracts.
- Common View hover/confirmation, auxiliary/centerline styles and owned-profile
  reference editing, including actual DXF import into its pending Sketch.

The owned-profile GUI test now stops its file-picker timeout on acceptance and
checks that the imported circle exists; the timeout previously kept running
through later import work after the picker had closed.

Separately, the general historical `zima_cpp_contract_tests` cannot complete with
the repository's `tests/fixtures/cross_language/part.prtz`: that fixture declares
format 26, while current native documents use format 28. No legacy loader or
migration path was added to bypass that unrelated fixture failure.

## Sketch endpoint placement correction — 2026-09-16

The three-Sketch bend study in `Projects/01.prtz` exposed two gaps in Sketch
Properties. The dialog stripped orientation flags from every later placement
row, including a curve deliberately selected after an anchor point. It also
left the automatic work plane at XY when FRONT came from a tangent. Since
FRONT is local Y, that plane contains the tangent rather than being normal to it.

Sketch Properties now preserves the shared direction-reference contract.
Existing planar-row normalization still protects the first placement plane.
Automatic Sketch planes use local XZ for a FRONT direction, in both the dialog
and native Part/Assembly commit and regeneration paths. Explicit manual plane
choices remain authoritative.

Regression checks cover the real dialog, both arc endpoints, creation and later
reference replacement, line/arc/spline endpoints in two frames, free roll,
source movement, Undo/Redo and native save/reopen. A read-only diagnostic can
also load the bend study and verify that the final Sketch normal aligns with
the source arc tangent; it never saves over the input document. References
already saved with their orientation disabled must be selected again to express
the intended tangent constraint.

The GUI and CLI were rebuilt after this correction. Sixteen targeted suites
passed, including the final dialog test with endpoint roundoff, curve placement,
Holes UI and owned-profile reference UI checks. The saved bend-study diagnostic
reported a unit normal/tangent alignment of 1; SHA-256 of the input file was
unchanged.

The additional broad `zima_cpp_workspace_startup_contract` did not pass: after
its Sketch/placement checks it failed the Drawing assertion `inserting an
Assembly view did not seed the BOM from its two Part occurrences` (the assertion
currently expects quantity 3). This result is recorded separately; BOM behavior
was not changed by the endpoint-placement correction.

## Third direction and Tree diagnostics — 2026-09-16

A point followed by its curve fixes position and the FRONT tangent, leaving
one rotation about that tangent. A third straight Sketch segment or original
solid edge can determine that remaining rotation even when the line is remote
from the anchor. The line contributes its constant direction; it does not
move the anchor. Exact native line packets and sampled straight edges now
follow the same rule in placement, datum construction and remaining-DOF feedback.
Curved references still require a unique tangent at the anchored point.

The issue reproduced on freshly created documents, independently of the saved
bend study. The regression matrix includes exact/sampled lines, reversed and
translated lines, native Sketch assignment and all original edges of a newly
calculated box, with transformed sources, Undo/Redo and native save/reopen.

Part Tree diagnostics now include persisted native Sketch points and curves
in their reference index. Previously a correctly solved point/curve placement
could still be marked red because the Tree indexed solid and datum geometry
but omitted Sketch geometry. The shared index is tested for valid, explicitly
invalid, repaired and deleted-source states; building it performs no OCCT work.

The rebuilt Windows GUI/CLI passed 15 selected regression suites. These include
the third-direction matrix (including parallel, degenerate and remote curved
rejections with last-good-frame retention), Tree diagnostics, existing datum
and placement commands, Assembly Sketch properties, Holes and Bend. The real
Sketcher mouse-input test also verifies endpoint C precedence over new-segment
midpoint M in both rendered previews and committed constraints. It reproduced
the competing M label before the correction and passes with line/circle contact
and with M alone. The read-only `01.prtz` endpoint diagnostic reports normal/
tangent alignment 1 and leaves the file SHA-256 unchanged.
