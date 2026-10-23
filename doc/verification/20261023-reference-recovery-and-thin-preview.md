# Reference removal and Thin Feature preview corrections

**Thin preview evidence is incomplete and superseded by the subsequent detailed
Thin review.** The GUI checks below established a nonempty operation wire and
absence of the separately appended source Sketch. They did not establish all
longitudinal edges or detect stationary inactive-side reference contours. The
user subsequently reproduced both defects. Do not interpret the earlier passed
checks as complete Thin wire or rounded-profile acceptance.

## Reported defects

- A broken red external reference could be offered by the View but could not
  be selected for deletion. Tree selection also confirmed an unbroken semantic
  key which did not identify the displayed red wire.
- Adding another Up To face could leave the pending operation without a wire,
  although accepting and reopening the Feature displayed its stored wire.
- Thin Feature Properties added the original Sketch profile to the physical
  wall boundaries, producing an additional contour at the profile plane.
- The left cylindrical generator in `2000-kontrola` was unavailable as an
  individual reference. Earlier complete-cylinder tests did not cover this
  partial cylindrical face and its offset, rotated destination Sketch.

## Changes

Selection for removal strips the presentation-only `:broken` suffix before
using the existing identity parser. Constraint/support entry retains the strict
parser and its refusal of broken supports. View click, View context menu and
box selection use the selection parser; Tree confirmation retains the broken
presentation key. Removal uses the existing native mutation and Undo paths.

Exact Part draft preparation now supplies the edited Body's calculated input
boundary. The final result may have consumed a selected face and is therefore
not an appropriate input for its own pending operation. Other Bodies retain
their existing calculated data and placements. Source capture remains once per
document generation. Opening unchanged Properties does not calculate geometry.

Thin modeling Properties no longer append a separate source Sketch profile to
the physical operation wire. Sketch editing still displays that source profile.
This does not change the calculated solid, source Sketch or persisted format.

Individual cylindrical/conical generators are certified against persisted cap
and angular boundaries. Partial bands offer only their supported generators.
The picker receives the exact world-space source line; the accepted reference
cache alone is projected onto the destination Sketch. Displacing that Sketch
along its normal must not move the offered source line. Both side identities
and native Segment output are preserved. No OCCT traversal is used for picking.

## Verification matrix

The native removal matrix covers five broken reference kinds, both Segment
orders, Distance/X/Y/Angle dimensions, driving/reference and locked/unlocked
states, successive removals and drags, serialized Sketch readback and removal
of the dependent PointReference contact of a broken AxisPoint. Unrelated native
geometry and dimensions must remain identical. Impossible edits remain atomic.

The GUI removal matrix uses private Part and Assembly documents and real View
and Tree mouse events: red-wire click/Delete, the View context menu, Tree
selection including an intentionally hidden broken AxisPoint, exact Undo/Redo
and native save/reopen. Box selection shares the corrected parser but requires
an additional mouse regression before claiming that interaction was exercised.

The analytic matrix retains complete cylinder/cone cases in XY/XZ/YZ and adds
partial bands, both source metadata forms, supported/opposite side rejection,
exact source depth and offset-plane cache equations. Existing native dimensions,
locked/unlocked dragging, source radius changes and serialization checks remain.
The actual-model GUI adds ordinary Reference and Reference-to-contour selection
of the left partial-cylinder generator in the rotated, offset Feature 005 Sketch.

The actual-model preview GUI checks correction changes, all three Thin side
choices, absence of an extra source-profile contour, and removal/reinsertion of
the second target through the real reference table and View picker before OK.
It also checks that numeric changes do not republish the base scene. Existing
ordered-limit GUI and native suites cover Cancel, unchanged OK, repeated edits,
save/reopen and Undo/Redo.

## Execution status

Rebuilt Windows results:

- Translation contract: passed, 14.46 s.
- Native reference-command matrix: passed, 0.83 s.
- Native dimension-command and analytic projection matrices: passed, 1.35 s.
- Part/Assembly removal GUI: passed, 12.212 s, including all five reference
  kinds, real context-menu interaction, exact Undo/Redo and native save/reopen.
- Actual-model and synthetic ordered-limit GUI: passed, 32.553 s. This includes
  correction changes, all three Thin side choices, no extra source-profile
  contour and real removal/reinsertion of the second target before OK.
- Actual left partial-cylinder generator GUI: passed, 15.696 s, for ordinary
  Reference and Reference-to-contour, including native owner acceptance,
  save/reopen, exact projected endpoint equations and native Segment output.
- Ordered kernel suite: passed, 14.49 s.
- Ordered native Feature suite using the corrected Part source preparation:
  passed, 70.80 s.

Records are under `build/reference-recovery/` and
`build/reference-recovery-native-final.log`. Before the source-boundary fix,
the actual-model failure was reproduced as
`Intersection source face is missing at its input boundary.`

The tested immutable copy and the untouched user review file both have SHA-256
`2886f659e2ca0f37afade5d30fb6c09dd9f444e45b0412d833a5adb0cd1df594`.
The separate `2000.prtz` and the user's configuration also retained their
before/after hashes. The GUI evidence includes
`build/reference-recovery/gui/ordered/single-face-correction-review.png` and
`build/reference-recovery/gui/generator/partial-generator-reference.png`.

No native format or shared container-placement change is introduced by these
corrections. Localization review: selection/removal and preview fixes reuse
existing translated text; no new UI labels or messages are required. User
projects and settings are protected by before/after SHA-256 checks. Native Linux
execution, wider Assembly occurrence interaction and release packaging remain
separate verification gates.
