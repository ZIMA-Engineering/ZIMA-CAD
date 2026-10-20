# Sketch Slot centre alignment - 2026-10-20

## Traced cause and bounded change

The Slot already creates six shared native points, two semicircular arcs, two
straight sides, one equal-radius relation and four tangencies. Its arc centres
already accept ordinary manual point-pair H/V constraints in both selection
orders. The native solver and persisted Slot topology are unchanged.

The GUI reproducer confirms a separate automatic-inference defect. With centres
near `(10,20)` and `(42,20)` and an oblique construction segment `(6,0)-(46,40)`,
the shared segment inference offers midpoint-on-line before H/V. A Slot cannot
consume that segment-only relation, so it discards the offer without recovering
its available centre H/V. The second centre remains slightly displaced and its
perpendicular radius differs from the intended horizontal-slot radius.
`build/slot-gui-baseline.stdout.log` and `.stderr.log` retain the failed case 11;
cases 0-10 passed before the product change.

The Slot now falls back to the existing two-device-pixel H/V capture band when
a segment-only offer cannot be used. Existing usable point alignment, exact
snaps and centre symmetry keep their precedence. Automatic enable/disable policy
is respected. Preview and confirmation use the same exact-snap precedence; the
preview publishes H/V/S at the centre line and its cursor follows that result.
Only confirmation creates the Slot transaction. Preview remains arithmetic and
transient viewer geometry, without a solid-kernel calculation.

The command prompt describes H/V and symmetry in cs/en/de/fr/ru. No document
format, factory model, template or protected container-placement change is
required.

## Finite native matrix

`cpp/tests/sketch_slot_direction_studies.hpp`, run by the ordinary Sketcher
contract executable, passes 64 cases: H/V, both point-selection orders, both
initial contact directions, R/D, projected/Euclidean driving centre distance,
and free/native versus read-only external centre support. Each case performs
successive R/D and distance edits, adds the alternative reference dimension,
and drags both centres and the four shared endpoints with unlocked and locked
drivers. External anchors are not dragged as editable geometry.

The final run performs 384 numeric edits, 1,440 actual movements and 672 rejected
cursor requests whose complete serialized state remains unchanged. Locked,
externally anchored cases have no remaining motion; a centre constrained to H/V
cannot follow every requested two-coordinate cursor displacement. A rejection
is not counted as an accepted movement. Fully fixed geometry rejects movement
and impossible numeric changes. Duplicate H/V rejects without mutation.
Independent equations verify the centre alignment, every endpoint radius, all
four tangent dot products, both radii, signed dimension values and reference
immutability after each action. Native serialized reopening also passes.

Evidence: `build/slot-direction-complete.log` and its build log. The complete
pre-existing Sketcher suite also passes. This finite matrix does not establish
all possible downstream trimming or unrelated dimension combinations; the
changed production path is Slot creation inference, not the solver or trimmer.

## GUI matrix

`build/slot-fixed-gui-v2.stdout.log` passes 28 cases in a Unicode working
directory: Light/Dark, existing X/Y/oblique centre symmetry, positive/negative
free H/V creation, manual H/V in both selection orders (including automatic
H/V disabled), the construction-line reproducer, and fully disabled automatic
H/V. It checks actual preview labels, cursor-defined radius, persisted point
identities and H/V, repeated centre dragging, actual Undo/Redo, Escape and native
save/reopen. The first expanded run exposed a verifier error: calling `solve()`
on the comparison snapshot changed its floating-point coordinates. The
independent solve now uses a separate copy before exact Undo/Redo comparison.

The final real Unicode/CJK working-directory run is recorded in
`build/real-unicode-verifiers.log` and `build/ověření GUI 検査/results.json`.
All 28 cases pass with repeated R/D and centre-distance edits through actual
inline editors, exact Undo/Redo, five live prompt languages and native reopening.
Nine pointer movements per case publish zero base scenes (252 moves total).
Their elapsed times include event dispatch and painting and are observations,
not a hardware-independent latency promise. The cursor reuses the already
calculated preview endpoint, avoiding a second complete inference per movement.
The native matrix also independently checks the external anchor equation.

The five-language source/catalog contract passes. Packaged Windows acceptance
and native Linux execution remain separate gates. The diagnostic executable
does not replace the user's normal CAD launcher or EXE.
