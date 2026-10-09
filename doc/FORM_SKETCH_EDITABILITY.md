# FORM Sketch editing and reference reprojection

## FORM-EDGE edit, hover and draft history regression

The second FORM-EDGE profile contains a connected equal-length line component
with native axis contacts and horizontal/vertical constraints. Dimension edits
and point dragging reuse the existing bounded component seed when the ordinary
solve fails, then verify the complete native equations. A coupled drag whose
mouse coordinates miss its free path can project onto that path; this does not
relax geometric tolerances or make external references editable.

`zima_cpp_equal_length_component_tests` checks 576 successive dimension edits
and 2,016 drags across four coordinate reflections, reversed selection order,
Distance/X/Y, driving/reference, locked/unlocked and native/external axis support.
Independent equations, actual permitted movement, atomic rejection, stable IDs,
read-only sources, native persistence and document Undo/Redo are checked. A
combined dimension-edit/drag sequence also exercises off-path mouse coordinates.
The line-component fixture has no circles, so circle contact-side variants belong
to the separate tangent regressions rather than this count.

Active Sketch picking excludes that exact owner's committed original-reference
packet while retaining its current displayed curves and other occurrences. This
prevents hovering the old position of an edited arc from highlighting both the
old and new curve. No persisted source packet is removed or recalculated.

Embedded profile Sketches share the existing transient Section draft history.
Geometry and dimension mutations remain undoable before their owning feature's
OK. Leaving the draft retires its private history; the parent feature still owns
the single document commit, and Cancel retains the original document.

The Windows FORM-EDGE GUI regression exercises mouse dimension editing, repeated
drags, old-position hover, Segment/Circle creation, selection and Delete,
Circle Trim, draft Undo/Redo for these actions and native reopening for both
owned profiles. The latest combined run passed in 19.24 seconds. This covers
these two embedded profile editors, not every Sketch command, other draft hosts
or cross-platform GUI behavior. Section-specific Cancel restoration remains a
separate verification requirement.

Exact external spline display now uses its persisted rational curve, with the
same 128 intervals as imported spline display. Coarse projection-cache chords
no longer overlay a visibly different outline. Native tests compare samples
with exact evaluations before and after source refresh, preserve source data,
and exercise both import orders and both curve directions in a closed
quarter-cylinder profile with independently checked volume. Shared endpoints
retain native identity; external import pole mappings keep their order through
endpoint merging so refresh remains active.

No new Sketch UI strings were introduced. Windows native and GUI verification
is recorded separately from Linux execution, which has not been performed here.

## Editable tangent profile

The FORM 2D Sweep profile combines a tangent arc, two axis contacts, a sloping
segment, a radius and a supplementary line angle. Its lower support is a
read-only original face reference. A bounded circular-equation fit now reads
cached external points and lines alongside native supports, then submits the
result to the ordinary Sketch solver. External geometry is never a variable.

The fit preserves the selected angular branch and explicitly solves the
incidence and orthogonality equations at native arc/circle tangent contacts.
Measuring an unlocked supplementary angle after a drag retains that branch.
A grip that cannot move under its constraints rejects the gesture without
committing numerical normalization as a new document state.

A segment can start at an external point and end tangentially on a native
circle. C+T capture has one endpoint and one contact marker. Leaving the capture
area offers C alone; confirmation persists the exact contact. Circle radius
and segment dimensions remain editable where their degrees of freedom permit
movement. An external anchor never moves to accommodate the edit.

## Deliberate destination-frame edits

Extrusion/Revolution Properties resolve their owned Sketch frame through the
existing document resolver. When that frame changes, the pending Sketch now
reprojects its original/body/context sources through the existing reference
snapshot path and returns the complete updated Sketch to the embedded editor.
Only persisted viewer data is read; this preview does not calculate bodies or
change the shared placement solver, reference solving, orientation or offsets.

If a projection becomes unavailable (for example an axis-point reference after
the Sketch plane tilts), the pending edit removes that reference and its
dependent relations through ordinary Sketch geometry removal. If changed
projections conflict with driving equations, single-reference removal trials
first preserve a solution that removes only one reference. Where multiple
removals are necessary, changed references are retired in persisted order until
the remaining equations solve. Unchanged references are never candidates for
this conflict fallback. An unrelated invalid Sketch still rejects the edit.

The existing External Geometry removal semantics retain native owned profile
geometry and detach its reference dependency. Ordinary regeneration/source
refresh continues to mark missing references broken for repair; the removal
policy is restricted to a deliberate destination-frame change. Cancel discards
all pending reprojections/removals. OK, Undo/Redo and native save/reopen preserve
the complete accepted Sketch state and immutable source definitions.

## Surfaces from solid

A zero-thickness Shell retires preceding solid features' automatic axes,
profile centrelines and path helper points from ordinary Part display within
its own Body. Authored construction axes, independent Bodies and original
reference packets remain available. Positive-thickness and suppressed Shells
retain their ordinary datums. Rollback before conversion presents the input
solid with its original axes. This is a display rule; native topology and
reference ancestry are unchanged.

## Verification scope

- Native FORM profile matrix: 128 variants and 1,920 successive edit/drag actions.
  Variants include native/external lower supports, mirrored and rotated frames,
  both angular selection orders, radius/diameter, driving/reference dimensions
  and four radius/angle lock combinations. Independent equations check contact,
  fixed supports, finite arc membership, dimensions and source identity after
  each action. Blocked/impossible edits reject atomically; serialization reopens.
- External-point/native-circle tangent segment: 64 native variants covering
  both contact sides, both tangent selection orders, two frame orientations,
  radius/diameter, fixed/free centres and locked/unlocked radii. Successive
  length, horizontal/vertical projection, absolute-angle and line-angle edits
  are independently checked. Redundant/impossible dimensions reject atomically.
- Actual FORM GUI: repeated radius and supplementary angle edits by mouse
  double-click, centre grip drag, profile equations, OK/Cancel, Undo/Redo and
  native save/reopen. The saved surface conversion includes its following
  surface Fillet; input rollback and restored final display are checked.
- External-point C+T GUI: native source-point picking, contact jitter/hold,
  C-only release, confirmation, repeated radius edits and complete Undo/Redo.
  The surrounding existing endpoint/intersection cases remain part of the test.
- Native reference frame study: 12 rotation/translation combinations with
  projected edges and linked profile points, plus free/plane/point-constrained
  container frames (36 combinations). Additional checks cover collapsed edges,
  unusable axis points, fixed/free referenced endpoints, unrelated valid
  projections and multiple incompatible fixed-point sources. Ordinary broken
  identity retention, source immutability and serialized reopen are verified.
- Bound-container GUI: rotation, tilt, actual reference/native-point positions
  after re-entering Sketcher, unusable axis-point removal, Cancel, OK,
  Undo/Redo and native save/reopen. Source definitions remain unchanged.
  Final affected-suite results are recorded in the release acceptance record.

The native equations matrix does not claim every variant was exercised by
mouse. Linux execution, passive Assembly/Drawing datum filtering and a complete
repository-wide green regression run remain outside this Windows acceptance.
No native file-format change or new user-visible text is introduced. All five
localization catalogs passed source coverage, key/placeholder validation and
actual translated dialog checks in Czech, English, German, French and Russian.
