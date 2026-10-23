# Curve-plane points and rounded Sketch mirror

Status: the recorded Windows native and GUI gates passed; this record does not
accept a portable Windows package or GitHub publication. Shared C++, Qt 6.11.0 and
OCCT 8.0.0; native Linux execution remains unverified.

## Inputs and implementation boundary

The curve-point persistence scope was explicitly approved in
[the curve-plane plan](../SKETCH_CURVE_PLANE_REFERENCE_PLAN.md). An isolated
intersection owns a native point binding containing the exact source curve,
ordered authored-point identity, native basis and an isolating branch domain.
Several bindings to one curve share one query during a Sketch refresh. Ordinary
hover and selection consume existing viewer geometry.

Intersection calculations use native rational B-spline Bezier spans, including
even-multiplicity tangencies. Parameters belong to the real bounded curve.
Coincident intervals do not invent a unique point. A changed parameterization,
contact orientation, root count or ambiguous branch freezes the last point as a
broken reference rather than silently selecting a neighbour. These conservative
checks can require explicit reselection after a substantial source edit.

Assembly regeneration borrows authoritative open source documents while using
the candidate Assembly's current occurrence frames. A regression reproduced an
unsaved source edit to 0.55 being replaced by the saved value 0.50. The corrected
explicit calculation retains 0.55 without writing the source file.

Rounded corners are native treatments on design vertices and their two support
legs. Mirror copies those records and their incidence, preserving the source
records and linking corresponding radii. Selecting a corner includes its two
support legs; selecting both legs also includes their corner treatment. Mirror
does not replace the treatment with a sampled curve. Source dimensions remain
on the source; copied corners do not duplicate intrinsic annotation placement.

An intrinsic corner-radius annotation is now represented in the Dimensions Tree
and the ordinary dimension selection/deletion path. Removing that annotation
preserves the corner treatment and its exact evaluated geometry. The shared
symmetry solver can propagate a dragged mirrored point back to a free source
point; two immutable points still reject an impossible move.

## Protected user inputs

All native tests use copies. The actual rounded skeleton input has SHA-256
`1de366313a98c93c2bfc7c3ce1ad95610e0ca81112f24432cfe27b660221f43c`;
the thin/reference review input has SHA-256
`931ac6c8d091ff5d0394f6a6736fd9f895d0a9643714f0acde39d291c42d8c75`.
Their originals, user Projects and private configuration are not bulk rewritten.

## Native point verification

| Matrix | Result |
| --- | --- |
| Exact bounded query: crossings, tangency, endpoints, no contact, coincident intervals, equal-XYZ distinct parameters, opposite normals and rigid frames | 32 passed |
| Rational closed circle seam/tangency and authored 3D path adapter | Passed |
| Part/Body frames: placed/unplaced, same/separate Body, three roots | Four passed |
| Source edits 0.55/0.45/0.52/0.50, disappearance/recovery, ambiguous domain and reversed point order | Passed; stable identities and frozen broken caches checked |
| Distance/X/Y, both selection orders and signs, driving/reference, locked/unlocked, fixed/free | 96 states, 144 successive edits and 108 permitted-DOF drags passed |
| Native exact interpolating 3D Curve segment | One associative point passed |
| Circle, ellipse, bounded arc endpoints and line; ellipse rotation swapping parameter branches | Passed; unsafe swap rejected |
| Coplanar source interval | Non-unique creation rejected atomically |
| Repeated and nested Assembly occurrences, unsaved source change, closed source and cold Assembly reopen | Passed |
| Native Part save/reopen and binding deletion Undo/Redo | Passed in four frames |
| Actual review-model Sketch spline against later Sketches | Two pairs, one isolated point each |
| Missing/inconsistent payload, invalid version/orientation/count/domain/key/owner | Eight malformed payloads rejected |
| Three contacts at identical XYZ but different parameters | Three distinct bindings/native points and exactly three contacts retained through native Sketch readback |

Each dimension/drag verifies its actual equation and read-only reference data.
Locked reference dimensions and redundant fixed-point drivers reject atomically;
fully fixed points cannot escape on dragging. Root/source identity is checked
separately from coordinate equality. Records are
`build/curve-plane-certified-query.log` and
`build/curve-plane-reference-native.log`.

One synthetic warm run of 100 explicit three-root source queries took 2.465 ms
total (0.02465 ms/query), without source/revision changes. This is a local
observation, not a universal timing promise. Three bindings with three linked
native points add 4,275–4,433 bytes to the four native Part fixtures. No external
geometry sidecar is required. Three complete native regression processes took
0.750–0.764 s with peak working sets of 169,779,200–170,487,808 bytes and peak
commit of 155,627,520–155,840,512 bytes. These whole-process figures include
OCCT, fixture body calculations, the dimension matrix, native I/O, Assembly
checks and actual-model source queries; they are not the incremental memory
cost of one point. There is no pre-feature performance comparison.
Record: `build/curve-plane-review/measurement/results.json`.

## Rounded mirror and GUI verification

The strengthened rounded-mirror native matrix passes 576 R/D and segment
Distance/X/Y/Angle cases: four native axis kinds, both selection orders,
driving/reference and locked/unlocked dimensions, repeated edits and drags.
Independent reflected-point, radius, tangent and dimension equations are checked
after each action. Fixed-point rejection and dragging both source/copy points
are included. The actual four-corner skeleton passes both built-in axes,
explicit/implicit corner selection, successive radius edits, intrinsic annotation
deletion, unchanged evaluated geometry, Undo/Redo and native Sketch readback.
Record: `build/sketch-mirror-review/native.log`.

Final GUI checks pass:

| Check | Result |
| --- | --- |
| Mirror: four axes, rounded/unrounded profiles, both themes, actual source/axis picking, Cancel, Undo/Redo, drag and reopening | Eight cases, 22.267 s |
| Intrinsic radius View/Tree selection, synchronization, Delete and Undo/Redo | Passed within all four rounded cases |
| Curve-point source/branch picking, short middle return, source isolation, native readback and Undo/Redo | 40 cases: five languages, both themes, Sketch/3D Curve and reference/contour; 118.060 s |
| Actual Feature 005 centered rectangle: four exact point picks, three real drags each, transient Undo/Redo, owning Feature OK and native reopening | Both themes, 24 drags, 16.319 s |
| Actual new Part/Skeleton/Assembly creation from the refreshed millimetre templates, editable context and normal commands | All five languages, 44.087 s |
| Equivalent creation from all three inch templates | All five languages, 43.521 s |

The first expanded curve GUI run exposed a fixture whose third root lay outside
the viewport after a wider localized toolbar settled. The final test uses the
ordinary Fit View action after entry and then selects every root through the
common picker. No production picking tolerance or priority changed. The source,
user Project and private settings hashes remain unchanged in every GUI run.
Records are under `build/curve-plane-review/gui/`.

All six start templates were saved and reopened with the changed native
implementation. Their existing authored content and bytes were unchanged by
this final refresh; the earlier Assembly-preview format update remains included.
The read-only inventory of
1,185 tracked factory native assets contains no curve-point bindings; repeat
against the final committed resource inventory. All 19 affected existing
Sketcher, Assembly, translation and preview regressions pass (320.69 s),
including the strengthened mirror/rectangle matrix and bounded-source checks.
Record: `build/curve-plane-final-regression.log`. Committed-source packaging
and Windows publication have separate acceptance gates in the release record.

Finite limitations: the GUI matrix is not an exhaustive test of every curve,
occluded-source picking, derived-copy occurrence or nested Assembly editing
combination. Root Assembly-owned authored curves and arbitrary large
parameterization-changing edits are not established by the listed cases.
External Sketch mirror axes are not added by this fix. Point sources cannot
themselves be trimmed as curves; downstream contour operations continue to use
their real native point incidence.

## Bounded source and centered-rectangle follow-up

Final review reproduced a trimmed-spline defect: querying its full supporting
curve offered contacts on removed intervals. The source provider now consumes
the stored native bounded geometry. Rounded support legs and corner arcs use
the same exact native profile evaluation as their displayed geometry, without
recalculating an offset or reconstructing a curve from viewer samples. The
regression retains only the middle of three spline roots, rejects a contact on
the removed design-vertex extension of a rounded leg, and verifies the actual
rounded arc's point independently. All earlier native point cases still pass.
Evidence: `build/curve-plane-bounded-before.log` and
`build/curve-plane-bounded-after.log`.

The user then reported blocked lower rectangle vertices in Feature 005 of
`2000-kontrola-20261023.prtz`. A fresh protected copy has SHA-256
`48fc65570314bb7e7dc84cc706ba5ff774ec67108256335f5980697d4500768f`.
It contains four free native points, four H/V constraints and two connected
symmetries, with no dimensions and no point binding to its external references.
The lower-right drag failed before the correction.

Reflection about a native Sketch axis is linear. The existing simultaneous
point-equation seed now includes those equations alongside H/V, and can seed an
undimensioned preferred-point drag with axis symmetry. The complete ordinary
solver still verifies the result. This removes the source-to-driven projection
cycle without a rectangle-specific movement implementation. Moving or arbitrary
construction-line axes retain their established path. No native format,
container placement, side choice or reference cache changes here.

On the actual protected input, all four vertices pass three successive drags,
independent centered-rectangle equations, immutable external references, native
Sketch readback and document Undo/Redo. A further 832 states cover all vertices,
quarter-turn orientations, both support and constraint orders, Distance/X/Y,
driving/reference, locked/unlocked and fixed/free points: 1,152 value changes
and 960 permitted-DOF drags pass. Impossible/redundant edits reject atomically;
fully fixed geometry cannot escape. Records are under
`build/feature-five-drag-review/`. Actual GUI mouse verification passes in both
themes: all four exact point picks, three successive drags each, independent
equations and transient Undo/Redo. The test follows the owned Sketch entry path
and verifies exact viewer point coordinates while its Feature draft is pending;
native Save/reopen is verified after the owning Feature's OK. User source and
settings hashes are unchanged. Final regression/package gates are separate.
