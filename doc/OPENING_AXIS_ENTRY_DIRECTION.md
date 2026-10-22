# Initial Opening direction with an axis and entry face

## Reproduction and cause

The last Opening (`01a162f9d3f97923ba706bf86e145a24`) in the user's
`Projects/POKUS.prtz` enters at Z = -50. Its axis reference points along +Z,
as does the preceding Opening's axis at the opposite entry face, Z = 100.
Both saved placements rotate local +Y into world +Z. The referenced Opening's
Forward direction is local -Y, so both bores point along world -Z. That enters
the upper face and leaves the lower face. The lower entry face correctly
retains its outward normal -Z; its identity and normal are not the defect.

The investigation uses private copies under `build/partial-drill-investigation`.
The captured source SHA-256 is
`bcb42308e0e0ee7b196078921caae8e44bebeadd54a0b5ceb9b0c86fc9d3f375`.
No user-authored document is rewritten by this change.

## Narrow authoring policy

A newly created Opening waits until an axis and a planar entry reference are
available. It uses their existing resolved placement and the entry surface's
oriented material normal to suggest Forward or Reverse so the bore enters
the material. The existing direction parameter remains authoritative.

This initialization happens once. An explicit direction choice, settings pasted
from another Opening, and an existing Opening opened for editing retain their
authored direction. Whole-Origin entry and placement without an axis retain
their established behavior. An axis alone cannot define a material-entry side.

The common placement solver, reference roles, offsets, flips, Origin axes,
calculation directions and native format are unchanged. No historical model
is silently reinterpreted on regeneration. An existing outward Opening can
be corrected with its existing Reverse control.

The helper reads already calculated reference geometry. It does not call OCCT,
publish a base scene, create history or create an Undo transaction. The existing
preview and explicit OK calculation consume the chosen direction together.

## Verification

`zima_cpp_opening_axis_direction_ui_contract` exercises real reference-field
and View mouse picking at both cylinder ends, in axis/face and face/axis order.
It checks the inward preview ray, explicit reverse/restore, both axis flip
states, exact independently calculated bore/chamfer volume, unchanged document
during entry, creation, unchanged OK, Cancel, Undo/Redo and native save/reopen.
It also checks that an explicit direction entered before the references wins.
All four mouse cases and eight axis-flip subcases pass in
`build/pre-release-opening-axis-recheck.log` (39.06 s).
The first probe incorrectly retained a Tree item pointer across a Tree rebuild;
the verification now reacquires the row for each edit, as the ordinary GUI does.
This correction is confined to the verification code.

The user case additionally verifies that changing only the last Opening's
direction in a private copy changes the previously empty outward cut into an
inward cut. Existing opposite-end and whole-Origin contracts remain separate
regression checks. Linux execution remains unverified on this Windows host.

Localization review: no new or changed user-visible strings. The existing
localized Forward/Reverse controls are reused.
