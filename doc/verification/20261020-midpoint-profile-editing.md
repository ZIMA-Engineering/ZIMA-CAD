# Editing midpoint-centred profiles

## Reproduction and bounded correction

A private external fan-washer study uses an ordinary native Sweep with three
rectangular sections. Each section has opposite parallel sides, perpendicular
adjacent sides, width, thickness and orientation. A real native point fixed to
the Sketch origin is the midpoint of its construction diagonal. The initial
solve reports zero remaining degrees of freedom, but successive width/thickness
edits were rejected. The same failure occurs without Sweep or Relations.

The initial native regression fails with `Cannot edit centred rectangle width`;
see `build/midpoint-profile-before.log`. Separate direct CLI profile edits
reproduce the rejection in `build/toothed-fan-profile-edit-diagnostic-v2`.

The existing linear and bounded simultaneous equation seeders did not include
the two equations for a native Midpoint constraint. The correction adds
`P = (A + B) / 2` in both coordinates. Failed value edits, drags and support
updates can reuse the existing bounded line-component fit when an active
Midpoint is present, as already done for coupled angular graphs. The ordinary
solver still checks the complete result before accepting it. Its geometric
tolerance, immutable external sources, segment direction safeguards, point IDs
and transactional rejection remain unchanged. Successful ordinary solves retain
their existing path. No kernel, native format or protected placement code changed.

Mouse input exposed another part of the reproduction. A cursor position can lie
off the remaining permitted path, while an unpinned minimum-change fit can share
the displacement between the four vertices. The midpoint fallback therefore fits
the actual handle first, before the existing generic distance projection. If
both cursor coordinates cannot be pinned, it refines two feasible states and
projects the cursor onto their displacement direction. Both direction states use
a stricter `1e-12` residual to avoid amplifying ordinary solve roundoff; ordinary
fits retain `1e-9` and final verification retains `1e-8`. The projected handle
must then pass a pinned fit and the complete ordinary equations. A curved path
is not assumed straight, and a failed fit leaves the existing fallback paths
available. A fully constrained graph cannot gain motion from roundoff.

The private washer uses the existing supported Relations dimensions and native
path references to earlier Sketch points. Allocated dimension identifiers for
owned path coordinates do not automatically make those coordinates assignable
Relations inputs; this correction does not expand that separate contract.

## Native matrix

`zima_cpp_midpoint_profile_tests` checks the following finite matrix:

| Cases | Variants and repeated actions |
| --- | --- |
| 128 centred rectangles | Angles 0, 4, 14, 45, 90, -14, -90 and 166 degrees; both contour sides and parallel/perpendicular participant orders; origin reference, cached external point, fixed native centre and free centre. Width and thickness each change three times. Oblique angular drivers also change three times. |
| 180 alternative width definitions | X/Y projections, point symmetry, line symmetry, parallel-line distance and point-line distance; three axial orientations, both contour sides and endpoint/support orders, three anchored centre kinds. Three successive edits, locked drag rejection, reference edit immobility and three free-width drags each with a reference or unlocked driving width. |
| 32 diagonal drivers | Eight orientations, both contour sides and endpoint orders; three successive diagonal-length edits with independently calculated rectangle width. |
| 20 angular drag cases | Five oblique orientations, both contour sides, reference or unlocked driving angular dimension, locked width/height. Three successive rotations must move the actual picked point to its requested position and preserve every independent rectangle equation. |

After each applicable action, independent equations check both pairs of lengths,
perpendicularity, orientation and the diagonal midpoint. All original native
vertices must remain identifiable. The read-only external point definition is
compared with its original value after edits and drags. Locked fully constrained
profiles cannot move. Both a reference height and an unlocked driving height
allow three successive drags with a deliberate transverse cursor error of
`1e-5` mm. The expected orthogonal projection, actual point movement, resulting
dimension and independent geometric equations are checked after each drag.
Reference-width cases also use off-path cursor targets. A free centre with all
driving dimensions locked must permit three rigid translations of every native
point without changing the profile shape. Impossible collapse is
rejected without changing the Sketch. Eight native Part cases also check
DocumentSession Undo/Redo, `.prtz` save/reopen and a dependent corner rounding.
The common point-midpoint representation remains intact.

Geometry is checked in the actual post-action Sketch, without another solve
repairing that state. A separate copy is used for solver status and residual
verification. The final native executable reports all 360 cases passing in
`build/midpoint-expanded-final.log`.

The native API keeps a read-only axis in the reference slot of AngleBetween;
GUI selection order is canonicalized before calling it. Swapping that slot with
a read-only driven axis is not an additional supported native representation.
Curved contact sides, radii and spline dimensions are not inputs of this
rectangle reproduction; their existing regression suites remain separate.
This matrix does not add reference/unlocked dragging for the diagonal driver,
mixed native/external line supports or every cross-product of angular dragging
with all four centre-anchor kinds. Native dimension selection permutations are
covered where specified above; actual GUI mouse coverage uses origin-anchored
profiles and Distance widths/heights, not every native matrix variant.

The original private M8 study now passes three successive outside-radius edits
through its actual Relations, native Sweep, circular Pattern, regeneration and
save/reopen (`build/toothed-fan-parametric-probe-v7`). This is not acceptance of
a completed toothed-washer family. No such study is installed in the library.

## GUI and dependent verification

The dedicated GUI contract exercises 0, +14 and -14 degree profiles, actual
double-click width/thickness edits, ordinary dimension state changes between
Sketch editing sessions, locked and free mouse dragging, Undo/Redo and native
save/reopen. Its drag explicitly exceeds the system's mouse drag threshold;
dimension labels are inside a deliberately fitted camera frame.

All seven final Windows checks passed in
`build/midpoint-reviewed-regressions.log` (189.40 seconds total): localization
catalogs, the full Sketcher contract, rounded tangent wing profiles, the native
360-case midpoint matrix, the midpoint GUI contract, coordinate/incidence GUI
and native title-block GUI. The midpoint GUI run took 85.70 seconds and passed
18 actual width/thickness edits, three locked drag rejections and nine free
drags across its three orientations. Undo/Redo compares actual point identities,
coordinates and dimension values with the states before/after each action.
Native save/reopen and all three final screenshots were checked. The earlier
off-path GUI rejection is closed by this run; its original failed traces remain
under `build/midpoint-profile-gui-drag-trace-v2.log` and
`build/midpoint-off-path-before.log`.

No new or changed user-visible application text is introduced; localization
catalog verification is included in the dependent run. Shared Windows/Linux
source is used, but native Linux execution requires its host. This document
does not certify exhaustive Sketcher combinations or the next Windows package.
