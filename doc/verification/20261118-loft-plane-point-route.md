# Loft plane entry, Point route diagnostics and route offer order

## Scope and causes

The 1812 source tests verified manual work-plane choices, but omitted the first
selected parent support and the owned Sketch's View-plane entry. The user
reproduced both failures. The Windows candidate was withheld from publication.

Loft now consumes the existing `prepare_profile_plane_reference` helper for its
first support, preserving the separate whole-Origin entry path. The owned
Sketch's View-plane action adapts its inherited parent attachment locally: a
confirmed valid plane replaces that attachment through the existing reference
entry and solver. Arming and cancellation do not remove stored references.
The shared placement solver, signed-side rules and native format are unchanged;
existing persisted Loft frames are not silently reoriented on load.

The copied user `16.prtz` has SHA-256
`12dd69c339565aa21ee31c5ed633f6f59d2a66ee3958a9771c4155c867603ec3`.
Its Point `Bod 01` had a valid `placement-route:3d` reference and calculated frame.
The Tree index contained individual Curve edges but omitted their whole route.
The baseline diagnostic reported this exact aggregate reference as missing.
The index now registers referenced routes through the same exact resolver used
by placement. Tree checking does not mutate the document or calculate a solid.
The original user document was not overwritten.

Whole-route offers were appended after all ordinary candidate hits. They are
now inserted immediately after their first segment in the common candidate
list. Existing command filters, occurrence identity and route deduplication
remain in effect; this does not add new path types or branch-selection semantics.

## Verification record

- Six native first-support cases passed: XY/XZ/YZ supports, both offset sides,
  inherited automatic XY Sketches, actual Loft volume and serialized reopen.
- The existing twelve manual-plane cases passed, including translated/rotated
  parents, independent origin/normal and volume/centroid equations, native
  save/reopen and Undo/Redo.
- Four focused CTest targets passed in 24.82 seconds: Loft geometry, Loft document,
  construction reference Tree and translation contracts.
- The copied `16.prtz` passes native diagnostics and the actual application's
  Tree check, including its Point feature. Missing owner, unknown route and wrong
  occurrence remain rejected; serialized document equality and repair after
  reopen are checked. The captured Tree shows `Bod 01` without a red error state.
- The expanded Loft application test passed: first-support TOP/XY orientation,
  manual XY/XZ/YZ preview and Sketcher alignment, View selection of all three
  parent datum planes and two distinct real planar solid faces. It checks
  inherited attachment retention on arming/cancellation, Default Origin ending
  entry, five languages, source identity, preview plane equations, rollback,
  unchanged OK, solid/Thin calculation, native reopening and Undo/Redo.
- The new route-order test reproduced the old ordering failure before the fix
  and passed afterward. It checks overlapping segments, repeated occurrences,
  one route offer per occurrence, actual Qt hover/RMB/LMB events, confirmation
  identity and rejection filters. It runs offscreen without desktop capture.
- The existing viewer contract and whole-Origin application check passed.

Local logs and isolated fixtures are under
`build/test-models/equal-segments-20261118/`. They are not package inputs.
The prior [Equal Length matrix](20261118-symmetric-equal-loft-launcher.md) remains
the evidence for that separate solver fix. Production UI text is unchanged;
the translation contract checks all five catalogs and source coverage.

## Limits

This is a finite Windows verification. Linux execution is not claimed.
The first-plane change does not migrate previously authored Loft placements.
The route diagnostic repair is checked for Part features; Assembly aggregate
route diagnostics are outside this change. Mechanism routing, branch switching
and a new 2D Curve subtype are design discussion only, not implemented here.
The broad General Surface GUI test stops at its toolbar grouping assertion
before exercising the owned editor. The identical failure was reproduced with
the unchanged 1812 packaged executable and a clean source configuration; this
follow-up does not claim that unrelated toolbar assertion passes. Nested
Assembly face picking and every possible face topology were not exhausted.
Packaged runtime and public release acceptance must be recorded separately.
