# Native arc centre and endpoint contacts

The read-only `Projects/FORM-KOULE.prtz` investigation reproduced feasible
centre-to-axis, centre-to-Origin and end-to-axis contacts being rejected.
Tests retain both the initial `FORM_FLAT` Sketch in
`cpp/tests/fixtures/sketch/form-koule-shared-arcs.json` and the subsequently
saved Sketch in `form-koule-axis-ends.json`; the user's document is never
rewritten by the regression.

Sequential contact correction moved an arc centre/end without fitting its
neighbour's shared end. The solve rejected the inconsistent graph, or
collapsed free radii while trying to repair it. Contact creation now reuses
the existing bounded simultaneous circular fit for native arc defining points.
A circular tangent junction is fitted before sequential corrections; other
arc contacts retry only a failed ordinary solve. Temporary radius equations
prefer entered sizes and retire before normal validation and DOF calculation.
For an arc end, both real circle/line intersections are tried before relaxing
free sizes; the existing finite-arc domain check preserves the sweep branch.

R/D and three-point angle edits fit shared native arc ends before validation,
including shared ends without T. A point held by both Sketch axes rejects
dragging immediately. There is no OCCT calculation, new persisted constraint
kind, format change or precision reduction. Side and native point ownership
contracts remain unchanged.

The reused fit retains its existing 32-point support bound. Larger graphs
stay on the ordinary path; this change adds no new geometry-size restriction.
Large connected native Sketch fitting remains outside this regression.

## Expected contacts in the initial regression Sketch

| Point | Horizontal X axis | Vertical Y axis | Origin |
| --- | --- | --- | --- |
| First arc centre | Conflicts with existing endpoint C and arc/axis T at positive radius | Feasible | Same conflict as X-axis contact |
| Second arc centre | Existing C; duplicate is rejected | Feasible | Feasible |
| First arc axis end, after removing C | C can be reinserted | Feasible | Separate endpoint/Origin case unverified |
| Second arc perpendicular end | Feasible with a valid finite-arc branch | Feasible | Separate endpoint/Origin case unverified |

Impossible and duplicate relations preserve exact serialization. Origin entry
uses the existing point-reference relation; axis entry uses C. Shared native
endpoints remain the same points.

The newly saved Sketch already has pair T and an additional C fixing the
second arc's perpendicular end to Y. With that additional C, the first centre
cannot also reach Y at positive radii: external circle tangency would put the
second centre farther from Y than its radius, so its constrained end could
not reach Y. This is an incompatible contact, not a selection defect. The
second centre can still reach Y or Origin, and its endpoint C can be removed
and reinserted. Both exact snapshots are verified separately.

## Verification matrix

`zima_cpp_native_arc_axis_tests` covers **3280 cases**:

| Area | Coverage |
| --- | --- |
| Creation | 120 centre/end/support variants; no pair T or either T order; original/90-degree orientation and either reflection; feasible, duplicate and impossible contacts |
| R/D | Both arcs, radius/diameter, driving/reference, locked/unlocked; three successive edits/drags; atomic locked-reference rejection |
| Position and angle | Shared contact X/Y/distance to Origin and either arc's three-point angle; all four driving/locking states; successive edits/drags; negative/wrapped angle drivers |
| Fully fixed | Every point drag is rejected without changing serialization |
| External supports | 72 immutable external axis/point variants, all T orders and R/D states; repeated edits/drags; stable source identities and unchanged cached coordinates |
| Trim | Nine surviving centre/end contacts through native arc trimming; retained contact identity, Part save/reopen and DocumentSession Undo/Redo |
| Newly saved Sketch | Seven centre/end contacts, including the additional incompatible first-centre/Y case; 48 R/D driving/reference/locking variants with repeated edits/drags |

Independent equations check native endpoint incidence, line/Origin contact,
arc/axis direction, shared angular tangency and measured dimensions after each
successful action. Round-trips check point/constraint identities. Failed
edits/drags must preserve exact document serialization.

The Qt GUI contract has **48 scenarios**: seven feasible centre/end contacts
and two incompatible first-centre contacts, both point/support selection orders,
with and without pair T, plus twelve cases from the newly saved Sketch.
It uses the common hover/cycling/click list, creation
Undo/Redo, real inline R/D editing with three changes and Undo/Redo, repeated
mouse drags, native save and Sketch reopening. The initial 36-scenario run
passed. The complete 48-scenario local contract subsequently passed in 527.141
seconds. The final clean, signed Windows 2026101106 package passed the same
48 scenarios in 524.77 seconds, with immutable packaged source/runtime checksums.
See [published acceptance](releases/2026101106.md).

Existing native tangent-contact, C+K, Sketcher, dimension/reference/relation,
curve, serialization and FORM suites provide dependent checks. These finite
matrices do not establish exhaustive GUI coverage of symmetric/line-distance
dimensions, every dimension sector, imported curve packets, ellipse families
or all downstream modeling combinations. External-source and trim coverage
above is native; their complete GUI combinations remain unverified. Native
Linux execution remains pending in [the handoff](LINUX_RELEASE_HANDOFF.md).

Localization review: no new product text or symbols are introduced. Existing
contact messages and C/K labels are reused; all five catalogs are included in
final validation. English harness text is diagnostic output only.
