# Profile centerlines

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
