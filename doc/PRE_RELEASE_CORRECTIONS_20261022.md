# Pre-release correction verification, 2026-10-22

This is a work-in-progress report. Publication remains blocked until the complete
requested correction batch passes and the user tests the normal Windows build.
The approved numeric Feature direction/Relations/negative-grip extension remains
pending. This report does not claim completion of that extension or a release.

## Completed focused checks

- Explicit Symmetry hover: six real mouse cases, X/Y/construction axes and both
  point selection orders. The second point remains offered after confirming the
  first. Creation, independent symmetry equations, repeated dragging, Undo/Redo
  and save/reopen pass (`pre-release-gui-and-reference.log`, 24.45 s).
- Feature dimension inspection: unchanged inline edit followed by selection,
  middle-button inspection completion, repeated inspection and Tree deselection
  pass without document changes (`pre-release-gui-and-reference.log`, 4.87 s).
  This reuses the common inspection retirement path; it does not regenerate.
- Feature Point/Text defaults: native parameter/default/persistence checks pass
  (`pre-release-gui-and-reference.log`, 0.08 s). The GUI shortcut matrix additionally
  checks both controls before any changes for all five types and the rotation
  shortcut. It passes with the existing surface-placement matrix
  (`pre-release-entry-ui.log`, 26.23 s).
- Feature/Sweep axis References: four actual View mouse cases cover a Feature
  axis and straight 3D Sweep axis, each projected as an infinite Sketch line and
  as a perpendicular point. Exact source identity, read-only source history,
  Undo/Redo and native save/reopen pass (`pre-release-slot-drill-axis.log`, 11.08 s).
  No production defect was reproduced in these paths; no workaround was added.
- Partial Drill Point: six native cases cover both material directions and full,
  half and partial circular bottoms whose bore axis lies outside the material.
  Four successive angles, independently integrated clipped-cone volumes and cone
  equations, conical source ancestry, Undo/Redo, save/reopen and cold regeneration
  pass (`pre-release-slot-drill-recheck.log`, 1.53 s).

The partial-bottom implementation searches existing persisted tessellation for
an interior probe when the circle centre is outside the face. Exact face and
solid classification decide the material side; the cone and Boolean clipping
remain exact. Repeated arcs on one circle are deduplicated by analytical support;
different circles remain ambiguous and are rejected. Hover and selection do not
run this calculation. A copy of the user's partial-bottom case also succeeds.

- Slot: 34 Light/Dark mouse cases cover first-centre markers, symmetry priority
  over point/curve/HV snaps, both manual H/V orders, disabled inference, repeated
  centre drags, radial/diametral and centre-distance edits, Escape, Undo/Redo and
  native persistence. Pointer previews must not republish the base scene.
  All 34 pass (`pre-release-entry-ui.log`, 189.77 s).
  Test marker identity is checked against the accepted native centre before
  subsequently adding a manual constraint, which may legitimately move it.
- Localization: production source/catalog coverage and the existing five-language
  UI checks pass (`pre-release-localization-recheck.log`, 16.11 s).

- Opening axis entry: all four mouse cases and eight axis-flip subcases pass
  (`pre-release-opening-axis-recheck.log`, 39.06 s). See
  [the diagnosis and authoring policy](OPENING_AXIS_ENTRY_DIRECTION.md).
- Existing Opening/reference/whole-Origin checks pass
  (`pre-release-opening-final-checks.log`): whole Origin 15.62 s, Opening targets
  1.11 s, component targets 0.88 s, Opening commands 5.87 s, drill references
  1.68 s, Feature selection 4.51 s and Opening copy 9.50 s. That run's new axis
  GUI probe failed because the verification retained a retired Tree item pointer;
  it was corrected and passed separately as recorded above.

## Remaining publication gate

The scalar Feature direction parameter, Relations binding, negative linear extent
entry and mouse crossing pass their native and GUI checks. The subsequent
Revolution signed-angle and opposite-end grip limit extension also passes native
and actual mouse/View dimension checks.
See [the direction parameter record](FEATURE_DIRECTION_PARAMETER_PLAN.md).
All 1,046 Part-based factory definitions pass isolated regeneration and are
installed with private working definitions preserved. Six start templates are
refreshed, and new-document activation, FORM/library preview, template editing
and Part/Assembly creation GUI checks pass. The user
tested the normal Windows build positively, then accepted automated verification
of the arithmetic follow-up and authorized release before further feature work.
All four arithmetic/localization/selection GUI contracts pass. Final immutable
portable package acceptance and publication remain; do not describe this
correction set as a completed release yet.

The broader native Sketcher matrix already passed in
`pre-release-corrections-native-local-temp.log` (15.29 s). The external-reference
command contract passed separately after the Windows test environment allowed
native hard-link/atomic-save operations. These results do not substitute for the
focused mouse regressions above.

All logs are ignored build artifacts under `build/`. Linux GUI/kernel execution
has not been performed on this Windows host. No native format field was added
by the completed fixes. The numeric direction validation adds one user-visible
error translated into all five languages; its label and lock controls reuse
existing translations. Localization and unit-entry GUI checks pass.
