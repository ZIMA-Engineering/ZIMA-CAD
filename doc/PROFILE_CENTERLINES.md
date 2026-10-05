# Profile centerlines

## Axis endpoints and display overhangs (2026-10-05)

Automatic circular and elliptical Extrusion axes, including General Feature
sides, and the standalone Revolution axis expose two independent points at
the actual axial ends of the calculated geometry. The fitted axis line retains
its display margin; its extra length does not move these reference points.
An axis-aligned exact bounding calculation determines the endpoint positions,
including when the feature is rotated. Their keys are
`profile:path-point:start:from:<axis-key>` and
`profile:path-point:end:from:<axis-key>`. The owner and axis ancestry remain
unchanged. The calculated reference packet persists these points for native
reopening and downstream point references. Profile-origin and centroid points
retain their actual operation endpoint positions. Intermediate Body boundaries
publish preceding points without calculating geometry, duplicating markers or
reviving original datums replaced by Straighten/Restore Shape.

Open Sketch construction curves, ordinary construction axes and curved model
centerlines have a 1 mm presentation overhang at both ends in the View and
Drawing model annotations. Automatically fitted profile axes already have their
margin and receive no second overhang. Curved overhangs follow exact endpoint
tangents, including Sweep arcs and splines; closed paths and infinite Sketch
axes have no artificial end extensions. Native Sweep calculation captures exact
centerline curves once. Repaint and drawing projection consume native curves
without OCCT. Reference coordinates, constraint geometry and picking samples
remain unchanged.

Idle axis/path points use the same brown as their axes. Sketch points belonging
only to auxiliary curves also use brown, including the authored Revolution axis
in active and passive Sketch views. A point shared with ordinary profile
geometry keeps its profile presentation. Hover, confirmation and inspection
retain their established interaction colors. Existing axis labels apply;
the shared disconnected-centerline error is localized in all five languages.

Previously calculated documents require explicit **Regenerate** to acquire new
automatic-axis points and exact Sweep centerline curves. Opening alone never
calculates geometry. Native and framebuffer regressions cover endpoint identity,
tangents independent of tessellation, closed-path behavior, Body cursor changes,
state ancestry, drawing projection and native persistence.

Windows verification passed all 13 targeted CTest checks: profile centerlines,
axis highlighting, point-marker colors, five-language translations and feature
UI, Sketcher, profile commands, solid-state kernel/document behavior, Body
references, derived copies and drawing annotations/contracts. A disposable copy
of the user's `01.prtz` also passed Regenerate, Save and Reopen with four new
automatic-axis endpoints and unchanged volume/input fingerprint. The original
file remained byte-for-byte unchanged. The local Windows development launcher
continues to use the newly built native application.

The subsequent physical-endpoint correction passed independent 15 mm circular
Extrusion and Revolution endpoint checks, an oblique translated/rotated cylinder,
General Feature sides, downstream references and native persistence. The axis
line remains longer than the actual geometry. The user's `01.prtz` copy again
passed Regenerate, Save and Reopen with four automatic-axis points, unchanged
volume/input fingerprint and an unchanged original file. The profile-centerline,
axis-highlight and automatic-axis GUI regressions passed on Windows.

## Body history cursor display (2026-10-05)

Body history boundaries publish preceding profile, origin and centroid axes
from their persisted reference packet using the same presentation path as
document history boundaries. Moving **Insert Here**, or confirming axis options
while the cursor precedes downstream operations, must not hide those axes.
Downstream axes remain excluded. This display preparation invokes no OCCT,
does not modify cached geometry and preserves transformed solid-state axes.
Native tests cover coincident circle axes, a later union and a through-all cut,
native persistence and every cursor boundary; GUI tests exercise Properties,
Cancel, Undo/Redo and the actual Tree cursor callback.

The broader `zima_cpp_profile_command_tests` initially reported an independent
stale expectation for an **Up To** rotation. The selected +X target normal
requires 270 degrees on the End side, giving `2400*pi`, whereas the test
expected the 90-degree volume `800*pi`. Stretching the owned Sketch was correct.
The corrected regression distinguishes both target orientations, rejects
symmetric 270-degree limits without committing, and verifies the valid
90-degree symmetric result using a separate opposite-normal datum. No product
geometry or side conventions were changed. Axis regressions, Body references,
session transactions, solid-state document tests and five-language UI/catalog
checks pass.

## General Feature profile axes (2026-10-02)

General Feature Extrusion sides now collect circular and elliptical profile axes
before the child solids are fused. Previously the group retained origin and
centroid centerlines but omitted these profile axes. Each new axis key is
`axis:profile:from:` followed by the existing length-prefixed Feature-side
boundary ancestry. The source boundary, Feature and Start/End side remain
recoverable; two sides never share one primary-axis identity. Unscoped groups
and the existing standalone Extrusion axis keys are unchanged.

Part Tree entries consume the calculated axis references, including enabled
profile-origin and profile-centroid centerlines. They retain the exact reference
owner and key and use ordinary axis selection. Reading or selecting these rows
does not calculate geometry. Side labels distinguish the two directions.
New profile-axis labels are translated in Czech, English, German, French and
Russian; origin and centroid labels reuse the existing shared catalog keys.

The targeted native and actual GUI regressions pass for both circle and ellipse
profiles, both sides, ordinary View availability, Tree selection and native
save/reopen. The GUI test also checks the labels after switching among all five
supported languages. Native axis binding and side ancestry, the broader Feature
GUI contract, profile commands/references and Straightening geometry regressions
pass. The final five-language run used a separate verification executable linked
from the current CMake objects because the user's development executable was
running; the production axis changes had already been built into that executable.

Previously saved results lacking these new axes require explicit Regenerate.
Opening a document alone does not invoke OCCT or silently recalculate it.

### Whole-feature selection (2026-10-03)

Whole-container hover and confirmation now include the lines of its visible
profile, profile-origin and profile-centroid axes. Previously endpoint markers
followed the selected Feature but the automatic axis lines retained their idle
color. Matching requires both the exact owner and instance path. Selecting one
individual axis still highlights only that axis; unrelated features and repeated
Assembly occurrences retain their own presentation. Origin visibility rules,
axis geometry and persisted identities are unchanged.

The framebuffer regression inspects line interiors separately from endpoint
markers and exercises whole-container confirmation, View hover/click, clearing
selection and individual-axis selection. It fails before the renderer correction.
No user-visible strings are introduced; existing five-language axis labels apply.
The regression passes after the correction, together with the full shared UI
suite and actual five-language circle/ellipse Tree lifecycle (26.49 seconds).
Whole-feature selection does not add individual-axis inspection markers.

## Profile-origin and centroid options

Extrusion and Revolution properties offer two independent, disabled-by-default
choices: **Profile origin axis** and **Profile centroid axis**. They supplement
existing cylindrical axes; they do not change the calculated solid.

The origin option follows the source Sketch origin. The centroid option follows
the area centroid of the selected closed profile, subtracting holes and combining
selected profile regions. An open profile has no area centroid and is rejected
when this option is enabled. Native line, arc, ellipse and resolved spline
geometry supplies area and first moments; the body volume is not measured.

Extrusion creates a straight axis with a 1 mm display overhang at either end.
Its two reference points remain at the actual operation endpoints, including
reference-limited extents. Revolution creates a circular reference arc with
endpoints for a partial turn, or a closed circle without endpoint grips for a
full turn. A seed on the rotation axis has no circular path; a partial turn
retains distinct Start and End references at the same position, while a full
turn creates no point. Coincidence must not merge these two identities.

Reference identities contain the feature owner and source Origin or Sketch ID.
Changing extent, angle or profile dimensions preserves those identities.
Generated geometry is stored in the native calculated reference packet. Opening
properties, picking and downstream reference resolution consume that packet;
they do not calculate a body. Circular paths also carry an exact rational curve,
independent of display tessellation.

The options use the common create/edit properties dialog and OK/Cancel
transaction. Both axis options share one horizontal row with spacing in
Extrusion and Revolution properties. Native serialization and Undo/Redo preserve both options.

Verification is provided by `zima_cpp_profile_centerlines` and
`zima_cpp_translations_contract`: area moments (including translated arcs and
holes), extent changes, stable identities, exact rotation curves, full-turn point
suppression, endpoint/axis binding, persistence, Undo/Redo and all five UI languages.

Feature-group fusion also preserves the automatic references of every child,
regardless of child order. The regression combines Extrusion and Revolution and
checks both orders. The Extrusion identity matrix covers Solid/Thin/Surface,
one/two/symmetric extents and both directions, with endpoint position checks and
native round-trips. Planar and original curved-body limits are covered separately.
See [Unified Feature implementation](UNIFIED_FEATURE_IMPLEMENTATION.md) for the
remaining work on disabled-side references and the unified modeling command.

## Verification boundary (2026-09-25)

The profile centerline tests, existing profile command/reference tests and
localization tests pass in the Windows development build. The broader
`zima_cpp_profile_frame_ui_contract` completed its Extrusion/Revolution
create/edit matrix, then failed its separate `sweep2dAction xz` first-plane
assertion (`Another Sketch host does not use the FIRST plane`). This finding
remains unresolved; this change does not modify shared container placement or
claim that the complete cross-command frame audit passes.

Factory Part, Skeleton and Assembly templates were regenerated with the current
serializer. Template UI and new-document options GUI contracts passed.

Centroid-derived axes and rotation paths display a `T` at their starting location
in the 3D View. The label shares the axis/path presentation color, including hover
and selection. It is a display-only label, adds no selectable point to a full
revolution and does not alter persisted references or require recalculation.

## Unified type and owned-frame follow-up (2026-09-25)

The Feature editor now offers Point, Axis, Plane, Sketch and combined
Extrusion / Revolution types; see [Unified Feature types](UNIFIED_FEATURE_TYPES.md).
The complete Windows profile-frame GUI matrix and Sketcher-return matrix pass.
The earlier Sweep2D first-plane assertion incorrectly applied the standalone
profile rule to a parent-owned path frame. Its expected frame now follows the
existing Sweep/Helical Sweep definition. The shared placement solver was not
changed for this correction.
