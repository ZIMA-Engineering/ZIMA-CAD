# Profile centerlines

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
