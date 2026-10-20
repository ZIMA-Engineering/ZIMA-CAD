# Equal-circle dimension editing - 2026-10-20

## Report and correction

The user's `Projects/31.prtz` contains three equal circles. Two centres lie on
the horizontal axis and are symmetric about the vertical axis; construction
segments join the third centre to them. Editing either the 300.0481977075 mm
centre distance or the 398.3145744068 mm horizontal distance failed despite
two remaining degrees of freedom. Both edits reproduced on private copies.

The existing simultaneous circular equation fitter handled EqualRadius only
between arcs. It now also handles native circle/circle pairs. The existing
arc/arc branch remains intact; mixed circle/arc and external circular supports
are not broadened. An exploratory broader lookup broke an existing fixed-arc
endpoint rejection and was removed before acceptance. The locked-distance
drag projection fallback also uses the fitter when its ordinary solve fails.

No native document fields, reference identities, placement contracts or
user-visible strings were added. The original user file remained unchanged,
with SHA-256 `d70449ea953d2765240ad4280cfa036438182a24e6d74c6d47c95652c4d1c472`.

## Finite native matrix

`zima_cpp_equal_circle_dimension_tests` passes 256 combinations of horizontal
and vertical orientation, reversed dimension/symmetry/EqualRadius selection
order, the third centre on either side, native or read-only infinite external
baseline, underconstrained or fully constrained geometry, locked/unlocked and
driving/reference dimensions, and ordinary or axis-projected centre distance.
Each combination receives three successive dimension edits and three centre
drags, plus serialization/reopening. Independent distance, axis, symmetry and
equal-radius equations are checked before solving a copy. Expected degrees of
freedom and external source immutability are checked separately.

An additional 64 combinations exercise three successive drags of either base
centre: locked distance rejects atomically, while an available unlocked centre
must actually move. Radius and diameter each receive three successive edits.
Collapsed driving distance is rejected without changing serialized data.
DocumentSession Undo/Redo and native save/reopen are covered.

The external fixture uses an infinite unit axis through the origin. An earlier
finite cached axis spanning -500 to 500 failed fixture construction as
redundant; finite/displaced external supports are not accepted coverage.
External symmetry supports and trimming are not added or verified by this
matrix. The existing complete Sketcher contract suite and midpoint-profile
suite passed, including the fixed arc-endpoint regression.

## GUI and actual-file checks

The application verification hook was run against a private copy of the actual
user file. It opens the owned Sketch, performs six real dimension-label
double-click/value-editor interactions, Undo/Redo and three mouse centre drags,
then commits, saves and reopens. Published viewer point positions and dimension
values are checked during the transient owned-Sketch editing session; committed
native geometry is checked afterward. The screenshot was inspected. This is
not a claim of independent radius measurement at every GUI step.

A separate native CLI run edited the actual file's centre distance to 310,
350 and its original value, and width to 420, 500 and its original value. It
then performed three locked-distance centre drags, Undo/Redo, explicit
Regenerate and save/reopen, without calculation errors or source-file changes.

Evidence: `build/user-31-initial/inspection.json`,
`build/user-31-before.log`, `build/user-31-corrected.log`,
`build/user-31-preserved-contract-regressions.log`,
`build/user-31-final-matrix.log` and
`build/user-31-extended-v1/results.json`. The four preserved-contract tests
passed in 40.86 seconds; the final expanded native matrix passed in 1.22 seconds.
Localization coverage passed with the accompanying program corrections; this
solver change introduces no UI text. Native Linux and release-package
acceptance remain pending.
