# Double circular tangency and trimmed contact editing

The reported case contains a moving circle already tangent to an immutable
projected circle. Adding another circular tangency was incorrectly rejected as
redundant or conflicting, depending on support identity and selection order.
The same contact must remain editable after trimming the circle into an arc.

## Changes

- Circular incidence uses its signed radial equation for numerical rank,
  including a projected circular edge recorded as `PointOnLine`. An unrelated
  tangency on that support must not change the incidence equation at another
  point. Equivalent projected incidence records do not remove an extra DOF.
- A complete circular contact contributes one angular tangency equation after
  its native endpoint incidence or independent C equations. Native and external
  circular supports use the same rank treatment in either selection order.
- Tangency creation uses the existing simultaneous fitter with immutable
  external inputs. It first preserves feasible entered radii; the first-picked
  native centre is a seeding preference, not an additional model constraint.
- Trim uses the persisted circular contact instead of sampled intersections
  near a tangent point. Both retained arc endpoints keep the existing point IDs.
- An arc endpoint at a circular contact follows an immutable support's cursor
  projection before its radial/sweep seed. This includes a native source-driven
  circle and an external circular edge. The ordinary equations still validate
  the complete candidate before committing a drag.

No native serialization contract, placement solver, external source geometry,
or user-authored original file is changed. There are no new user-visible strings.

## Native verification on Windows

`zima_cpp_double_circle_tangent_tests` runs 200 configurations:

| Coverage | Cases and checks |
| --- | --- |
| Reported sketch | Eight combinations of native/projected support, both selection orders and two second supports; expected DOF changes, repeated radius edits and point drags, read-only references, native Part save/reopen and Undo/Redo |
| Circular support combinations | 192 configurations: native/native, projected/projected and both mixed orders; internal/external contact combinations; both selection orders; three rotations and mirrored contact sides |
| Radius and diameter | Driving/reference and locked/unlocked states; three successive value changes where driving; locked reference records rejected atomically |
| Point manipulation | Three successive drags of the centre and both contacts; independently checked C/T/side equations after every action; actual movement required for free handles; fully constrained handles rejected without document changes |
| Positional dimensions | Centre-to-Origin distance, X and Y in both selection orders; independently feasible targets obtained from three radial poses; constant/redundant coordinates rejected atomically |
| Trim | Both intervals, preserved endpoint identities, R/diameter states and repeated edits, centre/start/end dragging, constraint checks and serialized reopen |
| Contact removal | Duplicate T rejected atomically; removing T retains its real point and independent C records |

All 200 configurations passed. The initial reproduction failed before the rank
and creation fixes. Adding trim coverage exposed near-contact extra cut points;
after that repair, the matrix exposed a nearly stationary start-point drag in a
rotated, reverse-order native pair. The final cursor projection repair passed
that case without weakening the movement assertion or geometric tolerance.

Dependent suites passed:

- The complete `zima_cpp_sketcher_contract_tests` suite. Its old rejection case
  fixed only the second circle centre; the first was free and the requested
  tangent was feasible. The updated case checks that feasible movement in
  reverse selection order, then fixes both centres and radii to verify genuine
  incompatibility and atomic rejection.
- 108 native contact creation/edit configurations.
- 3,280 native arc centre/endpoint axis/Origin configurations.
- 288 trimmed rectangle/circle configurations.
- Their shared prerequisites: 48 radial and 96 many-arc dimension cases,
  54 external angular and 60 point-pair/order/kind cases, 384 external point/line
  dimension configurations, 48 corner annotation cases and 16 reference-lock
  rejections.
- Translation and material-manager catalog checks for Czech, English, German,
  French and Russian.

## GUI and remaining limits

The dedicated GUI check passed four native/projected and forward/reverse cases
on an isolated copy of the reported Part, and four cases with repository-owned
fixture geometry referencing a real preceding Sketch. Each set includes 24
radius/diameter value edits with Undo/Redo, 72 actual point drags, Trim, contact
identity restoration and native save/reopen. Circular equations are checked
independently after numeric edits, Undo/Redo and dragging. The common picker also
cycles between two nearby native points before starting the same drag gesture.

An additional independent Python audit of all four saved reported-Part copies
checked radial incidence, centre separation, internal/external side and retained
arc endpoint identities. The preceding source Sketch was byte-for-byte unchanged.
The user original's SHA-256 remained unchanged. The final arc screenshots were
visually inspected.

The reported profile is still being authored: touching closed circles, or an
isolated retained arc, do not yet form a valid extrusion profile. The GUI test
explicitly retains that unresolved feature through the ordinary second-OK
workflow. This evidence verifies Sketch editing and persistence, not completion
of that solid's profile.

The matrix is finite. It does not establish arbitrary graph, ellipse/spline,
large-coordinate or all positional/angular dimension combinations on a doubly
tangent arc. Existing related regression suites cover their own listed cases,
not every combination with this new reproduction. Linux execution remains
unverified; the implementation is shared C++ without platform branches.
