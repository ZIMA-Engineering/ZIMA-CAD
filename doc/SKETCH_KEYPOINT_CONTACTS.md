# Characteristic-point contact: C+K

## Shared behavior

A new native point attached to a characteristic point of a circle, arc, ellipse
or elliptical arc owns two independent persisted relations: C keeps contact with
the curve, and K specifies its characteristic position. Removing K releases
motion along the curve while retaining C and its reference identity. Removing C
alone retains K. A pre-existing active C is reused. Both new rows are committed
atomically using one solve and the existing Workspace transaction path.

Own defining points, native point identity merges, Origin bindings and isolated
projected point references retain their existing semantics. No document schema,
kernel pin or modeling precision changes. Older contacts are not migrated. The
same C++ implementation is used by Windows and Linux.

Coincident selection accepts a generated characteristic point in either order
with the native point. Hover, right-click cycling and confirmation consume the
common viewer list. Confirmed C and K retain separate markers and removal actions.
Curve creation/snapping uses the same native insertion path.

The DOF Jacobian includes the source curve variables of generated K coordinates.
An exact K implies its same-point C, so the latter is excluded only from the
numerical rank estimate to avoid finite-difference roundoff creating false rank.
Convergence and the final independent geometry residual still validate both
persisted relations. After K removal, C participates in the rank normally.

Three-point angle correction moves the free ray when the other ray is anchored
by K, independently of selection order. Participant identity and the unsigned
angle remain unchanged. An unlocked symmetric dimension retains its structural
equations after a drag: K-supported point motion is projected along the axis,
and restored symmetric drivers are checked before committing the transaction.

Trimming remaps the independently persisted C to its surviving curve and retains
its ID. It discards the obsolete trimmed K instead of converting it into a
second C. Existing K-only trim input retains its prior conversion behavior.
Loft/profile correspondence retains the point when K is suppressed and C remains;
suppressing both removes its contact from correspondence.

## Regression matrix

The focused native test `zima_cpp_sketch_keypoint_contact_tests` records a finite
matrix of 860 cases with independent conic/contact and dimension equations after
successive edits and drags. Another 32 unsupported two-point angle requests are
rejected atomically; the supported point-line, three-point and four-point angular
forms are exercised separately.

| Area | Variants/actions |
| --- | --- |
| Native supports | Circle, arc, ellipse, elliptical arc; available quarters; ordinary/reversed axis definitions; 0, 0.37 and pi/2 orientations; fixed/free supports; existing/new C |
| Removal and rank | Separate C and K removal; exactly one released DOF after K removal; duplicate K rejected without document changes; exact C identity retained |
| Driving/reference dimensions | Radius/diameter for circle/arc; ellipse major/minor radius and rotation; segment distance/X/Y/angle; ordered point-pair distance/X/Y; both axis coordinates; point-line distance/angle; three/four-point angle; symmetric point distance; parallel-line distance; symmetric line distance/angle |
| Editing policies | Driving/reference and locked/unlocked driving states; locked reference dimensions rejected; three value changes and three successive endpoint/contact drags; fixed support unchanged |
| Full constraint | Fixed support plus locked segment length/angle; zero DOF; drag cannot change exact serialized state; impossible dimension rejected atomically |
| External supports | Projected closed circles/ellipses; C-only contact drag; source points and exact external packet remain read-only. Open projected conics currently become splines, so a forged analytic-conic K is rejected without changes |
| Dependencies | Opposite-side trim with K present/removed on all four conics; no duplicate C; reference survives Sketch round-trip; native Loft correspondence regression |
| GUI | Four native conics; both input orders; actual common-picker mouse confirmation; independent marker selection/Delete K; exact-ID Undo/Redo; three C-only point drags; native save and Sketch reopen |

The general Sketcher and command-level relation, dimension, curve, external
reference, serialization and Mirror/tangent suites are run alongside the focused
matrix. The native Origin K case verifies that no curve C is introduced.

The matrix also records 144 atomic value-edit rejections across 48 fixtures,
each checked against the same fixture with C removed (K-only). Fixed-support
symmetric point dimensions cannot change the anchored distance. Free K-supported
parallel and symmetric line-distance drivers also retain their existing
nonconvergence limitation; adding C does not introduce those rejections.
They are not reported as successful value edits. Subsequent drags, K removal,
independent equations and persistence are still verified for every fixture.
Free-support symmetric point value edits are not covered by this fixed-support
fixture. Exact analytic external arc K entry is not currently offered through
the sampled open-profile projection path.

This is not exhaustive GUI coverage of every dimension sector, symmetric
line-pair construction, imported external support, trim fragment or dependent
modeling operation. External conic contact is checked natively; the eight focused
GUI fixtures use native supports. Detailed GUI dimension entry, pending own-arc K,
and C+T regression coverage remains in the existing dedicated suites. Native
Linux execution remains pending in [the handoff](LINUX_RELEASE_HANDOFF.md).

## Fillet, Chamfer and Shell selection

These commands filter against the already calculated operation-input topology
at the active Body cursor or rollback boundary. Construction 3D curves and Sketch
geometry cannot become body edges/faces merely by sharing a generic viewer kind.
Input membership is read from the persisted viewer packet without OCCT. The
filter owns immutable shared lookup data so copying the callback does not copy
all reference rows. Exact occurrence paths and treatment preview dimensions are
retained; parameter seams remain unavailable.

Part scene/toolbar refresh restores the command contract, as the Assembly path
already does. Click confirmation applies the same predicate as hover. Closing
the command restores the established ordinary selection through scene refresh.
Shell surface extraction retains its solid-face-only restriction.

The focused GUI fixture keeps a standalone Sketch and 3D Curve visible alongside
a Box, verifies ordinary visibility, rejects both during all three commands,
confirms actual body topology through mouse input, checks selection refresh and
Cancel persistence. Existing treatment and surface-shell fixtures verify supported
body treatments. Native Linux GUI execution and representative large-STEP timing
remain separate acceptance work.
