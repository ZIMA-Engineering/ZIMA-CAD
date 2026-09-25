# Unified Feature

Implementation checkpoint: 2026-09-25. The Part command now uses the approved
compact editor and commits a native `Feature` history container. The targeted
native and GUI verification passes. Existing Sketch, Extrusion, Revolution
and sheet commands remain available; this change does not remove them.

## Inputs, means and output

Inputs are one owned Sketch, the existing container placement, two independent
side definitions, a common Add/Subtract operation, result type, thickness and
automatic-path options. The implementation reuses the native profile builders,
original-reference resolver, atomic Part profile transaction and OCCT primitive
builders. The output is one editable history container, not two independent
history operations. Opening properties and drawing its analytical preview do
not calculate a body.

The shared placement solver and its reference contract are unchanged. Feature
opts into the same capability predicate used by the Properties dialog and View.
Assembly commands retain their existing subtraction-only ownership contract;
the new unified action is enabled for Part editing, including an activated Part.

## Side and ancestry contract

Side 1 owns End; Side 2 owns Start. Each independently selects None, Extrusion or
Revolution. Symmetry uses Side 1 on both sides while retaining the independent
Side 2 definition for later use. A common Boolean operation applies to the
combined operands. Surfaces remain independent surfaces and cannot subtract.

**Swap sides**, placed after the axis checkboxes, exchanges the complete authored
side definitions, including inactive mode values, end references and numeric
locks. It does not exchange the Start/End identity namespaces or the Sketch
parents. The command is disabled during symmetry, where only one effective
definition controls both sides. Swapping twice restores the exact original
values, including precision beyond the displayed decimal places.

Generated topology is a child of authored Sketch geometry. Its source-parent
key includes the feature ID and authored side, but never the operation type,
length, angle, OCCT traversal order or coincident coordinates. Lateral faces
refer to source curves, longitudinal edges to source points, endpoint rims to
source curves, endpoint vertices to source points, and caps to profile regions.
The length-prefixed parent key can be decoded without kernel traversal.

Original child topology is captured before union. A union may consume a visible
edge without deleting its original-reference owner. Automatic origin/centroid
paths from every child are appended to the reference packet, including their
independently identified endpoints.

Disabling a side returns its endpoint caps, rims, vertices and automatic paths
to the source profile. References retain their owner and side. A dependent Point
therefore follows the disabled end back to the profile. Degenerate longitudinal
edges and lateral faces are not fabricated. A complete rotation omits its path
endpoint grips; a partial path on the rotation axis retains two coincident but
separately identified endpoint points.

## Extents and sketch-only mode

Extrusion supports length, original planar/curved Up To references and Through
All subtraction. Symmetric Up To reflects the target across the profile plane,
including exact kernel target geometry, while retaining separate side ancestry.
The analytical preview uses the existing symmetric extrusion preview rule.

Revolution supports an angle, a full turn, and Up To an oriented plane containing
the rotation axis. The angle is calculated analytically from the profile and
target normals about the directed axis. Opposite target normals remain distinct;
a same-oriented starting plane means a complete turn. A plane missing the axis,
a plane normal parallel to the axis, or a curved target is rejected atomically.
This is a uniform-angle operation, not a clipped arbitrary-surface revolution.
The first construction segment is the default axis; an explicit saved axis takes
precedence.

With both sides None, Feature can hold an empty, open or unfinished Sketch.
Closed profiles retain their reference-only endpoint geometry; open profiles
retain their source-curve endpoints without requesting a solid. Empty profiles
can still expose the origin path, but do not manufacture a centroid. This uses
an explicit empty-group permission for authored sketch-only Features. Ordinary
empty kernel groups remain invalid. No epsilon-sized solid is created.

## Editing and persistence

`FeatureParameterPanel` lives inside `PrimitivePropertiesDialog`; there is no
separate prototype creation dialog. Creation, Sketcher entry/return, editing,
rollback, reference inspection, OK and Cancel use the established profile
lifecycle. Pending changes are committed atomically through `commit_profile`.
Calculation failure leaves the document, Sketch and undo history unchanged.

The panel preserves inactive settings and original numerical precision when
fields are merely displayed. Length and angle locks are separate, including
when switching an operation on the same side. Shared parameter cues supply
length/angle dimensions and handles. Direct Feature dimension edits use the
same profile transaction, including owned-Sketch offset synchronization.

The native `feature` definition has an explicit validated codec. It preserves
both sides, target snapshots and signed zero. Existing document extensions and
native original-reference packets remain the storage boundary. No sidecars or
legacy migration paths are added. Factory templates must be regenerated with
the final serializer and checked through the real New Document GUI workflow.

## Verification checkpoint

Completed native coverage includes:

- parameter combinations, malformed definitions and oriented rotation limits;
- 18 automatic-path extrusion combinations and inclined/curved target limits;
- original topology consumed by union, both child orders and native packet
  round-trips;
- inactive endpoint references, dependent Points and Axes, closed/open/empty
  sketch-only Features, actual PRTZ save/reopen and restored operations;
- Part transactions, mixed side operations, symmetry, owned Sketch edits,
  failure atomicity, target refresh on regeneration and Undo/Redo;
- rotation Up To and symmetric extrusion Up To with independent volume checks.

The GUI contract exercises the public command, drawing a rectangle in the
embedded Sketcher, native OK, reopening, Cancel, Undo/Redo and unrounded values.
The expanded whole-Body-Origin placement and standalone-dimension checks pass.
Factory templates were regenerated and the real New Document GUI check passes.
Localization coverage checks all five catalogs; the Feature editor additionally
has per-language label, numeric-lock and complete side-swap checks.

A regression caught active-child path points being overwritten by the new
sketch-only point list. Appending the list instead preserves all child points;
the complete profile-reference suite passes after that correction.

## Final verification record

The Windows development executable and affected test executables were rebuilt.
The final targeted run passed 16 tests: core contracts, translations, Sketcher,
file rename, construction commands, symbol placement, Axis extents, profile
commands, Feature GUI, symbol GUI, New Document options, Drawing View controls,
sheet-state GUI, Drawing symbols, Feature parameters and profile centerlines.

Drawing View Properties now expands its scroll area to the confirmation row;
the GUI regression and visual screenshot check pass without changing calculation.
Drawing symbol Properties supports arrow-contact dragging without committing
until OK, and curved references provide the local tangent for perpendicularity.

This is targeted verification, not a claim that the entire repository test suite
passes. The previously identified Sweep2D XZ first-plane GUI failure remains
recorded in `PROFILE_CENTERLINES.md`; this work does not alter the protected
shared placement solver. Rotation Up To deliberately accepts only an oriented
plane containing the rotation axis, as described above.
