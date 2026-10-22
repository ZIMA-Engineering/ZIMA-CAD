# Profile whole-Origin authoring correction

## Demonstrated cause and approved scope

The user explicitly approved this correction on 2026-10-22: Default Origin
and whole-Origin Tree entry inherit aligned axes; individual face entry keeps
the local-XY policy. Persisted models, the format and common solver equations
remain unchanged.

The 2026-10-22 user test found that **Default Origin** rotates a Feature away
from the owning Origin, while Mirror does not. Whole-Origin entry expands the
selected frame into positional planes, beginning with XZ. The new profile-only
first-face authoring policy changes that first plane from FRONT to TOP. This
policy belongs to an individual support pick, not a complete frame attachment.
Switching the work-plane selector to XY after the final row cannot repair the
already authored orientation equations.

The correction gives the existing bulk Origin-entry loop a scoped,
transient entry context. Ordinary Sketch and profile dialogs bypass only their
first-face TOP authoring policy while that context is active. They consume the
existing complete-Origin FRONT/TOP assignments and shared placement solver.
Individual face/plane entry retains the accepted local-XY policy. The context
ends on every return and is never persisted. Mirror and other dialogs have no
profile-specific policy and retain their existing behavior.

The same four-case GUI matrix also exposed an incorrect default-parent lookup
for an ordinary Sketch. Placement is stored on its owning history container;
starting the parent lookup at the Sketch row chose that container's own Origin.
Use the owner row for all Sketch placement dialogs, extending the already
correct Flat rule instead of retaining a feature-specific exception. This
implements the approved parent-Origin intent; it changes neither frame-solving
equations nor stored documents.

No solver equations, reference side/offset meanings, manual rotation controls,
serialized members or factory assets change. Existing stored placements are
not rewritten or reinterpreted. The common reference-entry orchestration is
protected by AGENTS.md, so approval of this specific correction precedes editing
that loop. Approval was received before the product correction was applied.
The regression test was prepared and reproduced the defect independently.

Actual GUI reproduction on the normal Windows build fails as expected:
`Origin candidate Feature button rotation 90 -0 -180`, followed by
`Whole Origin rotated the profile axes away from the owning Origin`.
Evidence: `build/profile-origin-baseline-regression.log` (2.40 seconds).
This baseline uses a private native document and a real button click, rather
than inferring the error from the camera or an isolated solver fixture.

## Acceptance

The actual GUI matrix compares Default Origin and explicit whole-Origin Tree
selection for Feature and Sketch. Verify local XY and positive X/Y directions,
unchanged position, transient preview-only entry, Cancel, OK, native Save and
reopen, and Undo/Redo. Also run the existing individual first-face/native
work-plane, manual-plane/live-preview, container-placement, selection and
localization contracts. Record failures and unverified variants explicitly.

Pre-correction checks:

- Existing whole-Origin and nested Default Origin GUI contracts pass (11.57 s
  and 7.01 s). Their previous checks did not expose this profile-axis defect.
- Native work-plane and Origin/camera contracts pass. The curve-placement
  contract initially failed because its ordered-frame oracle still expected
  normalization to exchange explicitly authored FRONT and TOP roles. Its oracle
  now verifies that those roles survive both input orders and that the automatic
  work plane follows the primary reference, while manual XY remains explicit.
  No product change was needed for that baseline discrepancy. All three native
  contracts pass together in 2.57 s (`build/profile-origin-native-contracts.log`).
- The new four-case GUI matrix failed on its first case before the correction.
  Post-change acceptance is recorded below when complete.
- No UI text has been added or changed. Linux execution remains unverified.

Release 2026102201 remains unpublished until this correction is verified. The
first candidate completed compilation but packaging exhausted C: during its
private source copy. Only that incomplete private package copy was removed;
the build/source stage, user data and previously published archives remain.

## Accepted Windows result

The normal `build/cpp-windows-release` GUI now reports `(0, -0, 0)` for both
Feature and Sketch through both Default Origin and whole-Origin Tree entry.
The four-case matrix passes (9.05 s), including repeated draft entry, Cancel,
OK, native Save/reopen and Undo/Redo. Saved Sketch X/Y directions are checked
numerically against the owning Origin, rather than inferred from the camera.
The existing picker publishes five reference scenes per Feature entry and eight
per Sketch entry. These are observed packet publications, not kernel-call
counts; the authoritative document is unchanged throughout draft entry. This
correction adds no geometry-calculation path or scene-publication callback.

Additional accepted contracts:

- The complete live work-plane GUI contract passes (20.49 s), including the new
  matrix and existing XY/XZ/YZ/AUTO offsets, Cancel, OK and owned-Sketch return.
- Nested Default Origin (7.12 s), ordinary authored-wire selection (3.68 s),
  side reversal (2.98 s) and ten actual default/placed/solid/thin Sketch-camera
  entry cases (42.48 s) pass in `build/profile-origin-final-gui.log`.
- The initial combined run exposed an obsolete Sketch icon oracle: it expected
  the edited container's own Origin icon. The corrected oracle checks the actual
  parent Body Origin icon **and its confirmed reference owners**, not merely an
  unchanged preview. Its final rerun passes in all five languages and all tested
  palette/icon states, including repeated Flat Default and return to sheet-edge
  attachment (16.25 s, `build/profile-origin-parent-gui.log`). This supersedes the
  initial icon failure; it is not reported as a passing combined run.
- All four factory FORM previews, the formed-nose washer and actual template
  GUI creation pass: 6/6 in 31.27 s (`build/profile-origin-factory-gui.log`).
- All five localization catalogs pass in 13.75 s
  (`build/profile-origin-localization.log`); no user-visible text changed.

The finite matrix does not claim every possible nested Assembly occurrence,
manually corrected parent frame or existing user document. Native work-plane
tests separately cover persisted explicit planes, flips, offsets and rotations.
Existing stored placements are not automatically corrected or rewritten.
No factory conversion is needed for this UI-only correction. Linux execution
and final signed-package acceptance remain separate outstanding gates.
