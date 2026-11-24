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

## Follow-up interaction and partition acceptance

The initial GUI selection check preselected the Tree feature and checked the
candidate identity, not the resulting pixels. It missed a reproducible cold-open
defect on the saved `52.prtz`: the candidate is correct, but the intermediate
input mesh is absent and the authored Wrap preview cannot find its analytic
support. The follow-up uses the persisted original support metadata from the
current calculated packet with the same preview generator. No solid calculation
is added to selection. Acceptance now probes actual wrapped curve locations
before any Tree selection and checks visible azure pixels. All four sampled
curves pass after cold opening, followed by actual single-click/Tree selection
and double-click source/result inspection without a calculation or document edit.

Double-click inspection also displays the persisted planar source Sketch and its
images together with the wrapped result and dimensions. Ordinary hover and
single-click retain their existing presentation contract.

The user approved adding the persisted `split` Wrap output. Its UI uses the same
Sketch / Split / Surface terminology as Projection, with Relief remaining a
separate command. Split feeds native wrapped edges into the existing exact face
partition stage; it must preserve material volume and reject read-only external
targets or dangling partition tools. The pre-change asset audit found 1,074
tracked Part/Assembly/library documents and no active Wrap definitions in factory
assets. All six start templates were regenerated and reopened with the changed
serializer; their bytes are unchanged because they contain neither new output.
Actual GUI creation from those templates confirms the editable context and
command availability. All 1,190 tracked native factory assets pass the reader
audit, with unchanged source hashes.
The native 32-case Wrap matrix and 16-case document matrix pass, including
polar Split, independent volume and stable side identities. Nonmaterial output
uses adaptive native integration of closed shells with the unchanged absolute
volume budget; open Surface output contributes no material volume. Pure 3D Wrap
tools retain the splitter's native parameter-curve construction. All four outputs
also pass the actual GUI creation/edit/cancel/save and Undo/Redo matrix.

The additional saved Projection failure is reproducible on the private copy
with SHA-256 `afb6a4a75c765e3ac76cf6c25cf1d003208bb5a7d401e06209254ae4ed31d0af`.
Its tangent cylindrical source plane creates adjacent zero-depth section pieces
inside an authored segment. Endpoint naming currently rejects that artificial
internal endpoint. The repair rejoins adjacent native domains before naming,
without fitting a replacement display curve. All 33 native directional cases,
the existing document matrix, independent equations and both endpoint orders
pass. The saved model passes cold regeneration and saving on a private copy.

## Project relief acceptance

Project relief uses the existing exact Projection partition and native surface
thickening paths. Target parameter curves are certified before partitioning;
parameter-seam regions are joined while authored boundaries remain protected.
Nearest surface parameters classify spherical seeds without selecting the
antipodal region. A deep-copied relief skin isolates native tolerance changes
from source geometry. Refined parameter curves and measured endpoint tolerances
retain the native 3D curves and requested accuracy; solid and self-interference
validation are unchanged.

The six native cases cover cones and equatorial/polar spheres, positive and
negative relief, independent volume equations/integration, seam-side seeds and
cold recalculation. Eight document cases cover planar profiles with/without
holes, outer cylinders and inner bores, both signs, repeated height edits,
unchanged OK, Undo/Redo, save/reopen and stable reference identities. Repeated
planar target edits confirm the region follows current geometry rather than a
stale reference mesh. The existing Projection dimension, dragging, external and
nested-source matrix also passes. The Sketch solver itself is unchanged.

Actual GUI creation/editing passes for all five Projection presets, including
both relief signs, target replacement/inspection, whole-Origin entry, no-op,
Cancel, saving, Undo/Redo and three successive radius edits in the View.
Evidence: `projection-acceptance.out` and `projection-acceptance-gui.out` under
`build/wrap-20261124-fixes/`.

## Related UI acceptance

- H Curve and H Sweep share the ordinary Plane/Sketch controls. The initial
  circle/point button is beside its plane, before the later profile buttons.
  Actual XY/XZ/YZ and offset changes update the cyan plane without calculation;
  the existing complete helical GUI matrix passes.
- The FEM player exposes all settings immediately and keeps its window geometry
  while duration, scale, position, playback and export status change. The FEM
  GUI matrix passes in five languages and two themes, including writing and
  decoding a three-second MP4 with changing frames and safe failed export.
- Section context creation uses the shared Create section text and section
  icon. The existing Section GUI, Drawing, PDF and native persistence checks pass.
- Updated catalog/source coverage, placeholders and actual translated dialogs
  pass for Czech, English, German, French and Russian.

Private follow-up logs: `hover-followup1`, `wrap-acceptance-gui`,
`helical-followup2`, `fem-stable-followup1`, `sections-followup1` and
`translations-followup1` under `build/wrap-20261124-fixes/`.
