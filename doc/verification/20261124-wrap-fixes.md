# Wrap placement, material side and interaction fixes

Development baseline: `c944c7336`. Windows MSVC Release, Qt 6.11.0,
OCCT 8.0.1. This supplements the original [Wrap matrix](20261124-wrap.md).

## Causes and changes

- Exact cylindrical p-curves previously assumed that the native surface used
  the same right-handed UV frame as the preview map. Native circle Extrusion
  can produce an indirect frame. Wrap now builds those p-curves on an equivalent
  direct cylindrical support; containment still uses the actual bounded face.
- An ancestry binding can retain the generating face orientation instead of
  the current solid's material side. Wrap resolves its target against the current
  Body faces before creating its surface or normal offset. The wrapped region
  also retains that side independently of its wire winding. Shared offset and
  Boolean algorithms are unchanged.
- Isolating a modeling curve for profile extraction removed other supports
  while retaining constraints that referred to them. Regular polygons with a
  construction circle and equal-length constraints consequently failed with
  `Sketch constraint is invalid`. The adapter now marks other curves as
  construction geometry, retaining the complete constraint graph. It does not
  change the authored Sketch or its solver.
- Wrapped display curves use the ordinary sketch/projection width and feature
  hover/selection path. Their reference points remain available but are not
  permanently displayed as black endpoint markers.
- Wrap sketch and Wrap relief are separate commands immediately below Twist.
  Relief Correction is signed: positive adds along the material normal, negative
  cuts against it. Redundant direction checkboxes and Add/Subtract buttons are
  removed. Existing positive depth, Boolean operation and reversal fields retain
  their persistence contract; this is not a native format change.
- With explicit user approval, subsequent position references no longer acquire
  automatic orientation when an existing cylinder/cone/sphere reference drives
  the normal. Manual Orientation and the existing planar behavior remain.

## Finite verification

| Check | Coverage |
| --- | --- |
| Native Wrap | Cylinders, cones, spheres, polar anchors, curves/surfaces/relief, holes, images, later cuts, cache reuse; asymmetric nonzero-azimuth native cylinder endpoints checked against independent equations |
| Document Wrap | Constrained regular hexagon, outside cylinder and inside bore, both material directions, all three outputs, surface normals, independent cylindrical volume integral, preview/endpoints, unchanged OK, Undo/Redo, save/reopen and cold regeneration |
| User model | Read-only copy of `52.prtz`, including its failed hexagonal relief, cold regeneration and preview/committed curve agreement; original untouched |
| Shared profile adapter | Existing Projection document matrix, including external/nested source frames, source changes, no-op, Undo/Redo, native reopen, cycles and reference loss |
| Properties and viewer | Both command presets, actual target picking/replacement/clear, whole-Origin placement, OK/Cancel, reopening, hover and click/Tree synchronization, inspection, signed correction edits, images and Undo/Redo |
| Curve width | Dark/light, DPR 1.5, ordinary/preview/selected/hover; wrapped curve width matches ordinary Sketch width (2 logical pixels) |
| Placement UI | Cylinder/cone/sphere normal driver followed by two position planes, manual Orientation on/off and reopening; existing planar/DOF/precision matrix |
| Localization | Catalog/source coverage and placeholders in cs/en/de/fr/ru; affected dialog checked in all five languages and both themes |

The read-only model copy SHA-256 is
`165bfbb7131dafd4088de39012acacf68b32f821e88355d76a7fb2a63f3680dd`.
Private logs and regenerated copies are under `build/wrap-20261124-fixes/`.
No user model or personal configuration is part of the release input.

The tests do not establish exhaustive Sketcher manipulation coverage: the
Sketcher solver, dimensions and mouse manipulation implementation are unchanged.
Dedicated interactive nested-Assembly/external-Skeleton Wrap coverage and native
Linux execution remain unverified. Existing cached results require explicit
Regenerate to apply corrected geometry; tab switching still does not calculate.

Portable publication is a separate gate, recorded in the release notes after
candidate, signed-runtime and public-download verification.
