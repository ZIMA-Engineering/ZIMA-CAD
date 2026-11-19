# Modeling completion — Windows 2026111905

This record closes the implementation work in
[the modeling follow-up](../MODELING_FOLLOWUP_20261119.md). Publication acceptance
is recorded separately after the committed-source package has been signed and
downloaded from GitHub. Shared C++ sources, Qt 6.11.0 and OCCT 8.0.1 are retained.
Linux execution is not established by these Windows checks.

## Implemented behavior

- Symmetric and unequal two-sided extrusions retain exact Start/End ancestry.
  A merged current face has one selectable presentation while both authored
  aliases remain available to downstream operations. Fillet route previews use
  cached complete guides and terminals. Extrusion and Sheet from Body can select
  the reported walls after successive Fillets without a preliminary Regenerate.
- Sheet conversion clips exact curved continuation partitions with the existing
  kernel geometry. Its private skin traversal recognizes both aliases of a
  merged face without changing ordinary face identity or picking contracts.
- The shared failed-calculation acknowledgement also covers Assembly-owned
  Extrusion and Revolution cuts. The unchanged second OK commits the unresolved
  definition once; changed draft or source generations require another attempt.
  Cancel discards it. The retained input remains distinguishable from a valid
  calculated result. Missing/invalid definition fields still require repair.
- Virtual corner rounding supports native line, circular arc, elliptical arc
  and open spline junctions. The separate **Curve fillet** command trims two
  editable finite curves exactly and inserts an ordinary native arc with two
  point-based tangent constraints. It does not insert a radius dimension.
  Each contact is a shared native point; removing T retains incidence.
- Generated Sheet Profile Flats expose two end corrections as ordinary Sketch
  dimensions, editable in the View. They retain stable IDs, signed-zero sides
  and the ordinary magnitude/sign input convention. Deleting a dimension leaves
  the endpoint available to other native dimensions or C supports; regeneration
  does not recreate the removed dimension. The following Bend uses the corrected
  joining edge. Explicit regeneration also refreshes the generated definitions.
- Up-to-plane Flat limits include both material skins, the target inclination
  and thickness direction. An outward correction on a targeted side is rejected
  atomically; a free opposite side remains editable. The first Flat respects
  the same material envelope instead of replacing it with a nominal centerline.
- Existing independent sheet branches, parallel calculation, history insertion,
  conversion release on source deletion, correction baselines, Corner Closure,
  embedded Sketch exit and intersection editing remain in their established paths.
  No shared container-placement solver or native format is changed in this batch.

## Verification matrix

Tests use independent endpoint, tangent, radius, plane-clearance, material and
reference equations. A successful initial solve alone is not acceptance.

The new curve matrix passes 400 native combinations (10.41 s) and 20 actual
Qt interaction cases (66.04 s). The general UI contract passes in 31.27 s;
the completed Sheet Profile native suite passes in 9.73 s. The final external
read-only fixture includes a complete source identity, as required by native
validation. Local logs are under `build/fillet24/`: `curve-final-test.log`,
`version1905-tests2.log`, `final-native-suite.log`, `final-translations.log`,
`gui-completion-tests5.log` and `gui-completion-tests3.log`.

| Area | Cases and actions |
| --- | --- |
| Native curve fillets | Every ordered pair of line, circular arc, elliptical arc, control-point spline and interpolating spline; four rotations, mirrored geometry and both selection orders. Virtual radii 0.5/1/0.75, unchanged source points and reopening. Real fillets retain exact curves, native contact identity and no automatic dimension. |
| Fillet manipulation | Driving R and diameter, locked/unlocked, three successive size edits and center drags; reference R/diameter remain non-driving. Locked center X/Y edits and fully fixed drag rejection. Equal Radius drives the new arc. Removing T retains points. Invalid radius and read-only external support leave the document unchanged. Separate input endpoints are also exercised. |
| Fillet GUI | Native and virtual creation against each support type, both click orders, Cancel, one-transaction Undo/Redo, manual R/diameter, three successive label edits and actual center drags, finish and native save/reopen. The tangent check differences two exact support positions, avoiding amplification of endpoint solver residual in a tiny derivative step. |
| Flat end propagation | Both material directions, master and intermediate Flat, four successive corrections; next Bend joins the actual corrected edge. Independent calculation, stable dimension IDs and native reopen. |
| Flat end freedom | Both endpoints, master/intermediate, native/external supports. Replacement X and length dimensions, locked/unlocked; dimension deletion and repeated free drags; replacement C and three support positions. Read-only sources, independent station equations and absence of resurrected dimensions. |
| Flat envelope | Both material directions and target sides, positive/negative plane slopes, both skins at every profile vertex, three inward changes, atomic rejection outside the target and extension of the untargeted opposite end. |
| Flat GUI | Four master/intermediate and first/last cases, actual double-click value edits through positive/negative inputs and zero-side semantics, Cancel, return to Properties, commit, save/reopen and Undo/Redo. |
| Parallel + C + Trim | Eight GUI cases include the slanted native/external support, both input orders, actual trim, persistent Parallel identity, post-trim drags, independent equations, read-only source and Undo/Redo/reopen. |
| Assembly unresolved cuts | Eight creation/edit, Feature/legacy-cut and Extrusion/Revolution combinations. First/second OK, changed draft, one transaction, unchanged source Part, saved unresolved state, regeneration and two Undo/Redo cycles. Existing Part Fillet/Chamfer/Shell coverage remains. |
| Existing modeling | Nineteen rebuilt native suites pass, including old Sketcher mechanisms, trimming, tangency, offset, symmetry, planar Fillet/Chamfer, route query/support, Bend, Sheet Profile, exchange and common confirmation. Local elapsed batch time: 301.99 s. |
| Layout and placement tests | The 660-case dialog layout matrix measures nested expanding Sweep3D tables. Placement fixtures now explicitly choose TOP after the first positional orientation and use the actual whole-Origin entry scope. Both material skins are asserted in Flat hover/click tests. Product placement behavior was not changed to satisfy obsolete test assumptions. |
| Localization | Source coverage, catalog keys/placeholders and affected controls pass in cs/en/de/fr/ru. |

The independent-branch release label must pass again in the committed candidate.
Its actual parallel-job counters and sequential comparison remain mandatory;
see [the branch contract](../SHEET_BRANCH_TEST_CONTRACT.md).

## Finite scope and compatibility limits

The new two-curve fillet uses finite open native supports. It does not extend
curves, trim externally owned geometry or accept closed splines as two-ended
supports. Exact trimming of an interpolating spline produces an exact native
control-point spline with persisted knots and weights. Independently referenced
old control points are retained rather than silently retargeted. These cases
do not prove every possible constrained spline, cusp, self-intersection or
arbitrary sharp 3D Fillet route can be solved. Every support-specific dimension
combination, including all ellipse axes and spline-control dimensions coupled
to the new arc, is not exhaustively covered by this finite matrix.

The new material-envelope matrix covers planar Up-to targets. It does not
establish exact full-thickness clearance against arbitrary curved targets.
The accepted source Solid-before-Fillet hover fallback remains metadata-only;
exact occlusion filtering of every original source face is not claimed.

Historical union-fragment references and files predating the current native
format are not migrated. Original user files, including 18/21/23/24.prtz and
the protected production project, are not overwritten. A future Sheet Transition
redesign still needs a concrete behavior specification; no such redesign was
included in the user's current list.
