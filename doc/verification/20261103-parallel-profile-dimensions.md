# Parallel profile dimension editing

## Scope

The reported native Part was inspected read-only at
`C:/Users/vladi/Prace/ZIMA-Engineering/03-CAD/01-ACTIVE_PROJECTS/01.prtz`.
Its SHA-256 remained
`E6081D64E7FA91B70842548A87F742517C1167A3AEEA734FFD69A330E8C9E596`.
The first Extrusion owns a line-only Sketch with 12 points, 11 segments,
13 constraints and 10 driving dimensions. Two pairs of oblique segments are
parallel. Before the correction only one of the ten displayed dimensions could
be changed by the native dimension transaction.

## Cause and correction

The ordinary sequential solve could stall after a dimension edit. Its first
fallback intentionally supports axis-aligned rectilinear equations and rejects
`Parallel` and `Perpendicular`. The existing simultaneous equation seed already
supports those relations, but the dimension path enabled its line-component
mode only for angle, midpoint and symmetry graphs.

After an ordinary solve has failed, a line-only graph containing an active
`Parallel` or `Perpendicular` relation now retries through that existing
simultaneous seed. The retry starts from the last valid Sketch branch, receives
only the pending dimension definitions and is published only after the common
solver validates every persisted equation. Successful ordinary solves are
unchanged. The bounded simultaneous solver still rejects unsupported curve
types and profiles over its existing size limit.

This changes no native format, user-visible text, placement contract, OCCT path
or platform-specific implementation. Windows and Linux continue to share the
same C++ solver.

## Regression matrix

The distilled reported profile is constructed in
`cpp/tests/rectilinear_solver_tests.cpp`. Every check independently evaluates
the geometric equations after each operation.

| Area | Coverage | Result |
| --- | --- | --- |
| Dimension kinds | Symmetric point distance, aligned distance, line-to-line distance and projected Y distance | Passed |
| Dimension edits | All 10 dimensions; three successive values per dimension | Passed |
| Orientation | Original, X-mirrored, Y-mirrored and XY-mirrored | Passed |
| Selection order | Both line participants for parallel and line-distance relations; both point participants where ordered | Passed |
| Editing policy | Driving/reference and locked/unlocked | Passed |
| Manipulation | Repeated unlocked point drag, fully constrained locked drag, and an underconstrained drag after removing one driver | Passed |
| Invalid input | Fully fixed conflicting edit and redundant driver insertion; both checked atomically | Passed |
| Persistence | Sketch serialization round trips, native Part save/reopen, Undo and Redo | Passed |
| GUI | Real dimension label hover, RMB offer, LMB double-click editor, Enter, Cancel/OK, save and Undo on a formerly failing dimension | Passed |

The native matrix completed 480 dimension edits. The pre-existing capsule and
large rectilinear equation matrices also passed. The dedicated GUI contract
`zima_cpp_parallel_profile_dimension_ui_contract` passed on Windows with Qt
6.11.0 and OCCT 8.0.1.

## Applicability and remaining limits

The reported profile has no external geometry, arc, spline, trimmed curve or
contact-side choice, so those variants are not applicable to this defect.
Dependent trim behavior is unchanged and was not claimed by this line-only
matrix. The existing capsule matrix retains circular, external-support,
orientation, ordering, lock, drag and trim coverage.

Native Linux execution was not performed on this Windows host. The correction
is in the shared C++ implementation and does not change the aligned Windows and
Linux dependency pins.

The implementation adds no user-visible text. Translation catalogs for Czech,
English, German, French and Russian therefore require no new keys; the existing
translation contract remains the localization gate.

## Follow-up corrections

### Direction-relation selection order

The Parallel and Perpendicular commands previously restricted the first GUI
pick to a native segment even though the Sketch model already supported a
stationary axis or straight external reference. The shared picker now accepts
the stationary reference first or second. In both orders the axis or external
reference remains read-only and the native segment is the driven participant.
Native-to-native selection retains the established first-picked-driven rule.

The focused Windows GUI contract covers Parallel and Perpendicular against a
base axis and a straight external reference in both temporal selection orders.
It verifies the equations, stationary/driven identities, immutable external
source data, native save/reopen, Undo and Redo. The existing native matrices
continue to cover orientations, driving/reference and locked/unlocked states,
successive value edits, actual point drags, atomic rejection and persistence.

### Line-distance witness origins

For a finite line-to-line distance, each witness line now starts at the
geometric midpoint of its source segment and terminates at the established
dimension line. A base Sketch axis remains infinite and therefore continues to
use its projected witness point. The Sketcher contract verifies both segment
midpoints and both dimension-line endpoints in Sketch-local coordinates; the
complete Sketcher contract suite passed afterward.

### Feature-to-Sketch transition

Changing an existing Modeling Feature to Sketch could reuse the former feature
type's cached profile-frame validity. A stale false validity flag then marked a
valid Sketch red in the Tree. Cached frame reuse now additionally requires the
feature type to be unchanged. A type transition therefore runs the existing
reference resolver and does not add a second placement path.

The native contract covers Modeling to Sketch, Sketch to Modeling, calculated
state, Undo and Redo. The focused GUI contract performs the real sequence:
change Modeling to Sketch, enter and leave the owned Sketcher, confirm, inspect
the Tree unresolved-state role, save/reopen, Undo/Redo and Cancel. Both focused
contracts passed on Windows.

No native format, placement data, user-visible text or platform branch changed.
The five-language translation contract passed. Linux execution remains
unverified on this Windows host.

### Active placement-origin presentation

With the user's explicit approval for this protected presentation change, an
active placed-container Properties preview keeps the active Body Origin and
shows exactly one separate pending container Origin. Successive placement
changes must replace that pending frame instead of leaving its former position
in the scene. Closing with either OK or Cancel removes the pending frame while
the ordinary active Body Origin remains visible.

The focused Windows GUI contract verifies the continuously visible Body frame,
the single pending container frame with three axes, three plane borders and one
always-visible origin point, three successive pending coordinate edits,
replacement without an old or duplicate frame, frame movement, OK and Cancel
cleanup, unchanged calculated geometry for the display edit, rejected
persistence of the cancelled coordinate, save/reopen and Undo/Redo.
`zima_cpp_placement_origin_preview_ui_contract` passed. The broader
surface-placement contract completed its cylinder branches and repeated
second/third-reference offsets, then failed in its later unrelated body-property
measurement continuation because that continuation had no current calculated
body. This presentation correction does not change placement solving,
references, persistence or calculation.

No user-visible text was added. The five-language translation contract passed
again. Native Linux execution remains unverified on this Windows host.

### Mirrored tangent Trim boundaries

The reported upper reflected circle was not split at its two finite segment
endpoints because display chords lie inside an exact circle and can miss an
endpoint contact. Trim topology now uses the existing native analytical
circle/arc-to-line query for segment contacts and converts the exact hit back to
the curve parameters. Endpoint acceptance uses the Sketch solver's absolute
equation tolerance converted by segment length; display sampling and the global
intersection tolerance are unchanged.

The follow-up radius matrix also exposed that a reflected circle previously
persisted only centre-point symmetry. Unlike an arc, a circle has no reflected
endpoint from which its scalar radius can be derived. New circle Mirrors now
persist an `EqualRadius` relation between source and reflected circles. Later R
edits therefore keep the reflected circle and its Trim topology associative.

The native matrix reproduces the exact coordinates from the reported file in
both segment endpoint orders. It verifies two boundaries per circle initially,
four real crossings per circle after increasing R by 0.5 mm, two again after
restoring R, a free outer-endpoint drag, Sketch serialization/reopen and the
independent source/reflected counts after every action. The complete Sketcher
contract, trimmed-intersection GUI contract, rounded-Mirror native contract and
tangent-Mirror native contract passed.

The GUI Mirror matrix now includes a circle in all eight existing combinations:
four axis kinds, light/dark themes and ordinary/rounded source geometry. It
performs real View source and axis picking, edits the source R, applies
Undo/Redo, drags a source point, saves and reopens the native Part, independently
checks the reflection equations and confirms that both circle radii and their
persisted `EqualRadius` relation survived. The focused GUI contract passed.

The rebuilt CLI opened the user's `01.prtz` read-only and queried Sketch
`01a1a35018637508b5d15ea0ee2f07dd`. It returned 15 Trim pieces. The formerly
missing reflected circle `01a1a35018637508b5d15ea0ee2f0807` returned exactly
two pieces, and the document revision remained zero and clean. Its SHA-256 was
unchanged before and after the query:
`E6081D64E7FA91B70842548A87F742517C1167A3AEEA734FFD69A330E8C9E596`.

Spline, ellipse and three-dimensional contact variants are not applicable to
this exact circle/finite-segment defect and were not claimed by this matrix.
Existing general Sketcher suites retain their coverage. No native format,
user-visible text, placement behavior, OCCT path or platform branch changed.
Native Linux execution remains unverified on this Windows host.

### Automatic rotation-axis visibility

Committing a new internal revolution, changing a Feature side to Revolution or
selecting a different internal construction axis now enables the selected
Sketch segment's established 3D visibility flag. When no explicit axis is
configured, the existing resolver still selects the first construction segment;
other construction segments remain hidden. An ordinary later edit with the same
axis does not force a manually hidden axis visible again. External profiles are
not modified.

The shared helper is used by both Part and Assembly profile commits and adds no
OCCT calculation or additional scene publication. The Part contract verifies
two initially hidden construction axes, automatic visibility of only the first
selected axis and Feature extrusion-to-revolution conversion. The Assembly
contract verifies the same hidden-before/visible-after transition, native
persistence, subsequent angle edit, Undo/Redo and cold reopen. Both profile
contract suites passed. The Assembly fixture was also corrected to request a
visible shared Axis Feature when it asserts display; production handling of a
deliberately hidden Axis Feature remains unchanged.

No user-visible text or native-format field was added. The implementation is
shared by Windows and Linux; native Linux execution remains unverified on this
Windows host.

### Point-based tangent Trim junctions

Trimming a circle between two tangent Segment contacts could reconstruct the
resulting Arc with new endpoint points while retaining the original Segment
contact points and their C+T relations. The visible coordinates were nearly
identical, but the persisted topology contained two nodes at each junction.
Removing or editing a relation could therefore separate the Arc from its leg.

The immediate numerical cause was an exact tangent line/circle quadratic whose
discriminant retained a small positive floating-point remainder. Taking the
square root produced two false intersections around the one contact. The exact
line/curve query now classifies only a scale-aware round-off remainder as zero.
In addition, when a native Segment endpoint and Circle/Arc are explicitly joined
by the persisted C+T contract, Trim topology uses that stored shared point as
the authoritative single intersection. Real secant intersections and unrelated
nearby points are unchanged; no global snap tolerance was enlarged.

Trim reconstruction retains each authoritative contact identity, remaps both T
relations to the surviving Arc, and removes only the now-redundant C equations
after the Arc owns those exact endpoint nodes. Removing T subsequently leaves
the shared point and its Arc/Segment incidence intact.

The native matrix covers both Segment endpoint orders, source and reflected
circles, base axes enabled, radius increase and restore, a free endpoint drag,
independent tangent equations, removing T, and serialized reopen. The GUI
contract performs the real Trim action and View click, verifies the transient
preview, confirms through the shared middle-button double-click path, then
checks Undo, Redo, Save and native reopen. The full Sketcher contract, trimmed
rectangle/Arc contracts, native tangent-contact contracts and the focused GUI
contract passed on Windows.

No native format, user-visible text, placement behavior, OCCT path or
platform-specific implementation changed. The five-language localization
contract passed. Native Linux execution remains unverified on this Windows host.

### Unresolved-feature continuation contract

The profile command regression was aligned with the established recoverable
history behavior requested for failed Fillet and other modeling features. A
failed definition remains persisted and inspectable with its calculation error.
A later Feature is allowed to calculate against the last valid body instead of
being rejected or inheriting a false error. Repairing the failed definition
regenerates the continuation. A later invalid edit likewise remains as an
unresolved definition rather than disappearing.

The regression verifies the stored failing parameters, exact last-valid input,
cold reopen, a valid downstream additive Feature, repair, a second failed edit,
volume and error ownership, and Undo/Redo across repaired and unresolved states.
The complete profile command suite and its focused Feature-to-Sketch transition
contract passed.
