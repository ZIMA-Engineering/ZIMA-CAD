# Capsule line-distance dimension editing

## Reproduction and change

The Sketch in `Projects/KAPSA.prtz` contains two equal-radius semicircular arcs,
two straight tangent connections, centres supported on the native X axis, a
100 mm centre-distance driver and a 57.946189880354524 mm line-distance driver.
Changing the latter to 60 mm was rejected as conflicting geometry; changing
the centre distance to 120 mm succeeded. The original file was not rewritten.
Its SHA-256 is
`1D32CF276432159953A597E84A430E6E999C28F5A87CB02CD40673DDF237663A`.

The circular simultaneous fit already supported endpoint, tangent, equal-radius
and centre-distance equations, but rejected a graph containing `DistanceLine`.
It now includes the same signed line-distance residual as the ordinary solver.
The existing transaction and ordinary solver still validate the fitted candidate.
No calculation tolerance, reference identity, serialization, container placement
or OCCT implementation was changed.

A zero-width audit exposed a separate singular collapse: a rotated capsule with
an external axis could accept zero while retaining almost collapsed arcs.
Dimension transactions now reject zero separation of parallel lines contacting
opposite endpoints of the same circular arc. The check preserves the existing
contact branch and uses the existing conflict error. Ordinary zero line-distance
dimensions without this impossible arc contact remain supported.

## Regression matrix

The original Sketch data is captured in
`cpp/tests/fixtures/sketch/capsule-line-distance.json`. Native checks run in
`zima_cpp_rectilinear_solver_tests`; GUI checks run in
`zima_cpp_capsule_dimension_ui_contract`.

| Area | Coverage | Result |
| --- | --- | --- |
| Original drivers | Successive width/centre-distance pairs 60/120, 30/80, 90/150, original width/100 | Passed |
| Coupled line-distance drivers | 128 cases: four rotations (0, 90, oblique, 180 degrees), both selection orders, both persisted normal sides, locked/unlocked value edits, free/fixed first centre, fixed native/external axis supports | Passed |
| Successive width edits | 60, 30, 90, original width in every matrix case | Passed |
| Locked driving dimensions and dragging | Each of six points; repeated forward/backward drags where translation is free; every coordinate checked for fully constrained cases | Passed |
| Unlocked dimension dragging | Each of six points, two successive horizontal drags on the original native-axis graph; resulting dimension equations checked | Passed |
| Alternate width drivers | Endpoint distance, endpoint Y distance, arc radius, arc diameter; three successive values each | Passed |
| Reference dimensions | Conversion of width to reference and attempted numeric edit leaves serialized geometry unchanged | Passed |
| Impossible/redundant edits | Fixed-point incompatible width and duplicate line-distance driver rejected without changing serialized state | Passed |
| Zero width | Atomic rejection throughout the capsule matrix; ordinary line-distance zero remains accepted | Passed |
| Identity and external ownership | Point IDs and persisted side choice stable after edits; external support records unchanged | Passed |
| Persistence | Sketch serialization/reopening after each native value edit; native Part saving/reloading in GUI | Passed |
| Dependent trim | Trim a resized top line; circular identities retained and remaining equations solve | Passed |
| GUI | Actual common-picker hover/click/double-click, numeric edits in View, Properties and owned Sketch, centre mouse drag, Undo/Redo, Cancel/OK, save/reopen | Passed |
| Independent geometric checks | Equal radii, endpoint-circle membership, tangent dot products, centre distance, support-line distance after native actions; radius, endpoint and tangent equations after GUI drag/edit/save | Passed |
| Localization | No new product UI text; reused conflict message. Five-language translation contract | Passed |

Fully constrained drags may return `true` from the existing projection API while
coordinates change only at floating-point roundoff (observed approximately
1.4e-14 mm). Tests check actual coordinates and equations rather than interpreting
that boolean as motion. This change does not redesign drag return semantics.

The Windows build and the focused Sketch serialization, Sketcher contract,
dimension command, mirror/tangent, translation, slot GUI and capsule GUI suites
passed. The latter seven suites completed in 30.65 seconds. The native matrix
is an additional suite. The normal root `zima-cad.bat` launches the updated local
GUI build. No new GitHub release was published for this change.

## Explicit limits and separate findings

This is finite regression coverage, not an exhaustive solver certification.
Rotated/external/reference variants were checked natively; GUI mouse checks use
the ordinary horizontal capsule. Alternate width drivers were checked on that
horizontal native graph, not across every permutation of the 128-case matrix.
External circular contacts, arbitrary non-capsule networks, more than 32 points,
trimmed circular fitting and Linux execution are outside this verification.

An additional audit attempted to add an arc-radius driver to the original Sketch
while keeping its existing width driver. The solver accepted the mathematically
redundant driver instead of rejecting it. This is a separate outstanding rank/
redundancy-detection issue. It is explicitly excluded from the passing duplicate
line-distance result above; changing the general rank calculation is outside the
minimal fix for editing the user's existing width dimension.
