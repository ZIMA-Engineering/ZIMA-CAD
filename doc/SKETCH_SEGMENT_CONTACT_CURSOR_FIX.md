# Segment contact cursor verification

Date: 2026-10-07. Shared C++ implementation; Windows Qt GUI and native tests.

## Change

Segment preview already resolves circle tangency to an exact contact and owns
the C/T/H/V contact marker. The cursor independently used the ordinary circle
projection and its C marker, creating two apparent states inside the tangent
capture band. Straight Segment and straight Polyline cursor painting now uses
the preview endpoint and leaves the contact annotation to the preview. Arc mode
keeps its existing cursor behavior. Constraint calculation, capture tolerances,
dimension solving and persistence are unchanged. No user-visible prose changed;
localization coverage remains required.

## Regression matrix

| Behavior | Evidence |
| --- | --- |
| Circle contact from both sides, multiple successive small pointer moves | New endpoint-priority GUI cases 5 and 6 verify a held exact tangent endpoint and independent circle/tangent equations after each move. |
| Release outside the capture band and return | The same cases check actual endpoint movement along the circle, then recapture. Framebuffer equality against the expected cursor rejects a displaced cursor or a second cursor label. |
| Commit and native serialization | The same cases commit the segment, require the persisted Tangent constraint, serialize/reopen and solve. |
| Polyline line/arc/line, axis/line/point/circle endpoint supports | Existing five GUI cases in the same verifier retain their start tangency and endpoint markers; the non-tangent case must not acquire Tangent. |
| Circle radius, segment length and angle edits; contact and free endpoint dragging | Existing native Sketcher contract independently checks radius, length, angle and tangent equations and actual permitted movement. Both contact sides and endpoint orders are covered there. |
| Driving/reference dimensions, locked/unlocked, constrained/free geometry, external read-only supports and trimming | Existing native Sketcher contract and dimension-entry, grips and external-constraints GUI suites cover these dependent behaviors. This is regression evidence, not a Cartesian product of all variants. |
| Undo/Redo and document reopening | Existing grips/dimension/external GUI and native contracts exercise these flows. The new cursor cases separately verify native Sketch serialization. |

The new cursor framebuffer cases directly cover native circles in the XY Sketch
orientation. Rotated Sketch planes, external-circle cursor capture/release,
fully constrained circle contact cursor capture, and every combination of GUI
dimension kind, lock state and selection order are not separately covered by
those new cases. Existing solver coverage does not substitute for those GUI
variants. Linux execution and manual inspection of the user's latest screenshot
remain unverified.

Test results are recorded in `build/form-diagnostic/contact-tests.log` and
`contact-native-tests.log`. These are disposable local verification logs.

The native contract passed (0.82 s); external-constraints GUI passed (58.21 s),
grips GUI passed (3.60 s), and dimension-entry GUI passed (30.66 s). The final
endpoint-priority GUI matrix passed (54.39 s), recorded in
`build/form-diagnostic/contact-final-tests.log`. Its initial equation assertion
incorrectly used the requested click coordinate instead of the actually created
start point. The corrected assertion uses both real preview endpoints and still
independently checks the circle and tangency equations after successive moves.
