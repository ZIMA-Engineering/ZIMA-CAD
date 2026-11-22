# Arc endpoint contacts and source-driven circles

## Scope and implementation

Two follow-up Sketcher reports concern separate contact identities and the
endpoint selected by a clockwise Polyline gesture.

- A native circle can copy the centre and radius of an external circular edge.
  An arc endpoint may already have C to that external source, while the user
  selects its native copy for T. The previous implementation did not recognize
  that contact and could reject the tangent as outside the arc domain.
  The repair requires the existing Equal Radius dependency and coincident
  current circle geometry. It adds the missing independent C to the selected
  native support, then uses the ordinary tangent solver. Both additions are
  atomic and retain the same real endpoint. Removing T retains both contacts.
- Native arcs have canonical counterclockwise endpoint ordering. The clicked
  finish of a clockwise Polyline arc is therefore its canonical start. Snapping
  now attaches C, M or alignment to the endpoint opposite the connected incoming
  point. IDs are copied before constraint operations replace Sketch storage.

This uses the shared Windows/Linux C++ implementation. It introduces no native
format fields, placement changes, source-geometry edits or user-visible text.
No user-authored original is overwritten.

## Native checks

`zima_cpp_copied_circle_contact_tests` covers 64 synthetic configurations:
both selection orders, mirrored orientations, two rotations, direct external
and native-copy targets, and internal/external tangency at both ends.
Two optional checks use an isolated saved Sketch supplied through
`ZIMA_COPIED_CIRCLE_SKETCH`; private geometry is not a repository fixture.
All 66 configurations passed on Windows in 52.90 seconds on this workstation.

| Operation | Verification |
| --- | --- |
| Add T with an existing C | Selected contact identity and contact side; no extra point; explicit native-C control; source centres/radii unchanged |
| Radius/diameter | Driving/reference and locked/unlocked states; three successive values; locked reference records rejected atomically |
| Dragging | Three successive centre/start/end drags for each radial state; actual movement when free; fully constrained geometry cannot escape |
| Position and angle | Centre-to-Origin distance/X/Y and three-point arc angle, both point orders; three feasible targets; driving/reference and locking states; repeated centre drags |
| Invalid edits | Fixed incompatible geometry, duplicate T and constant positional drivers leave the serialized Sketch unchanged |
| Persistence | Exact serialized reopen, preserved point/reference identities; removing T retains the independent native and external C records |

Radial incidence, tangent centre separation, contact side and arc equations are
checked independently after edits and drags. The initial smaller reproduction
matrix failed ten of eighteen cases before the repair; its eight direct-external
controls passed.

The existing 200-case double-circular-tangency matrix also passed (59.54 seconds),
including both Trim intervals, retained endpoints, dimensions and repeated drags.
The complete native Sketcher suite passed before the test matrix was expanded
from eighteen to sixty-six cases; the production patch did not change afterward.
Five-language translation coverage/catalog validation passed. This change adds
no runtime labels or error strings requiring new translations.

## GUI checks and limits

The copied-circle GUI check passed four synthetic configurations with a real
preceding source Sketch and four configurations on an isolated saved Part.
Each set covers native/external selection in both orders, 24 radius/diameter
value edits with Undo/Redo, 72 actual point drags and native save/reopen.
The reported Part is still being authored; this verifies Sketch editing, not
completion of its extrusion profile.

The Polyline GUI matrix passed eight configurations (25.34 seconds): native
or external circular support, an incoming segment or arc, and both gesture
orientations. Each creates the arc through View mouse events, checks C on the
clicked finish, performs six actual centre/finish drags, and checks save/reopen
and Undo/Redo. The preceding source Sketch remains byte-for-byte unchanged.
The fixture uses the ordinary source reference packet, including its display
sampling, so cache refresh is not mistaken for an edit to the exact geometry.

The existing nine-case Polyline and eight-case endpoint-priority GUI checks
passed before their broader chained intersection suite reached mode 5 and
failed with `External Geometry command offered an original edge`. The unchanged
published 2026112205 executable reproduces the same failure (60.19 seconds;
changed executable 56.04 seconds). This older edge-selection issue is outside
the contact repair and remains unresolved. A separate endpoint-contact CTest
entry passed the relevant seventeen cases in 41.25 seconds; the broader test is
retained unchanged.

The saved original Part and its investigation copy had matching SHA-256 values
after verification. No private model is included in the release source.

This is a finite regression matrix. Positional/angular combinations are checked
through native dimension operations; the dedicated GUI dimension edits exercise
radius and diameter. It does not establish every possible graph, spline/ellipse
contact, large-coordinate case or all dimension combinations in the GUI. Native
Linux execution remains unverified.
