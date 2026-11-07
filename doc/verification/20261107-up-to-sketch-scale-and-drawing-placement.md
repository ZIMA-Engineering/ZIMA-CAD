# Up-to direction, first-Sketch scale and lightweight drawing placement

## Scope and lifecycle

This release correction covers four interaction paths that share no new native
schema and no duplicated platform implementation:

- a Modeling Feature with an Up-to target on the opposite side of its pending
  Sketch plane;
- the initial camera scale of the first ordinary Part Sketch;
- the geometry shown under the cursor while placing an ordinary or projected
  drawing view;
- a small Sketch corner-radius handle overlapping its native vertex at the
  approximate physical scale.

Up-to target selection resolves a temporary copy of the pending container and
its owned Sketch through the existing reference-resolution path. The pending
placement references and profile-plane offset therefore define the classified
plane. Side 1 owns the shared direction and may turn it toward the target. Side
2 never changes that direction. If both branches are Up-to, targets in the same
unambiguous half-space are rejected before the dialog mutates the document. The
document-core calculation path repeats this validation for CLI and other
non-dialog callers. A face crossing the profile plane and an infinite target
plane not parallel to the profile plane are directionally ambiguous and do not
cause an automatic reversal. This distinction preserves the perpendicular XY/XZ
limits authored by the factory `CornerGusset90` definition while rejecting two
parallel limits on the same side.

Entering the first non-drawing Sketch in a Part selects the existing DPI-based
metric camera scale and centers the Sketch. Existing geometry affects the center
only. The same classification is used after save/reopen and for a placed Body.
A later Sketch keeps the ordinary context fit, so the exception cannot hide its
preceding model. Camera commands do not modify the document.

Drawing placement consumes the already projected interactive packet prepared by
the ordinary/projected-view command. Its depth renderer performs the foreground
edge pass only, filtering hidden, thread, thread-leadin and hatch strokes. It
does not shade the view, run an occluded pass, reload the model or prepare exact
vector hidden-line output. A committed view and its later Properties preview
continue to honor the complete persisted display settings. PDF, printing and
vector export remain on their exact output route.

The corner-radius handle remains an ordinary Sketch candidate with its existing
semantic identity. It receives higher left-button priority than an overlapping
native vertex; right-button cycling still exposes both. Pointer unprojection
within 0.05 physical pixel of the original vertex is treated as the explicit
sharp-corner intent, producing an exact zero radius without reducing the
precision of non-zero values.

## Verification completed

- `zima_cpp_sketch_entry_view_ui_contract` passed the first-Sketch physical
  scale for newly entered and reopened empty Sketches, non-empty XY/XZ/YZ
  Sketches, placed and unplaced Bodies, camera flip/normal controls and unchanged
  document revision. Later and face-attached Sketches retained context fit.
- `zima_cpp_feature_type_ui_tests` passed Side 1 forward/reverse direction,
  Side 2 opposite-side acceptance without direction mutation and same-side
  rejection.
- `zima_cpp_feature_ordered_up_to_native_tests` passed the independent ordered
  limit matrix, same-side rejection without revision or serialization change,
  regeneration and native reopen.
- The native Sketch regression matrix passed symmetric junction, serialization,
  solver contract, mechanism, engineering profile, trimming, angular sector,
  mirror-center drag, external midpoint, keypoint contact and dimension-command
  contracts.
- Ten GUI Sketch contracts passed in 225.41 s: keypoint contact, dimension entry,
  coincident and angle-sector editing, Offset, Mirror, symmetric rectangle,
  mechanism, four equal corners and equal-corner dragging. The matrix includes
  repeated edits, real movement, exact zero radius, revival, save/reopen and
  Undo/Redo. `zima_cpp_sketch_hv_view_tests` passed 768 view-relative equation,
  dimension, drag, persistence and candidate-priority cases.
- `zima_cpp_drawing_contract_tests` passed normal tangent/hidden-edge behavior
  and placement-only rejection of hidden, thread and hatch strokes.
- All five localization JSON catalogs parse, have identical key sets and contain
  2,700 entries. The Up-to validation text is translated in `cs`, `en`, `de`,
  `fr` and `ru`; the other corrections add no user-visible text.
- The final isolated Windows gate passed all eight selected contracts. The
  seven geometry/UI contracts passed in 144.14 s; the separate five-language
  contract passed in 11.33 s. The test TEMP was inside the build directory so
  native save/replace checks did not depend on the host app-container TEMP.

## Publication boundary

The Windows runtime is produced from committed Git data and the one pinned Qt
6.11 kit. Every deployed `Qt6*.dll` is compared byte-for-byte with that kit
before launch. Native Linux compilation and execution remain a separate host
gate.

Deleting a solid that supplies an Up-to reference remains a manual repair case
for this release. The user replaces or removes the unavailable Up-to entry in
Properties; no automatic conversion to a length or cached snapshot is added.

There is no native document-format, start-template, factory-library, protected
container-placement, geometry-side, OCCT pin or dependency-version change.
