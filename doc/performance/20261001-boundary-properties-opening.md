# Boundary Surface Properties opening - 2026-10-01

## Scope and implementation

Inputs are the already calculated Part, its four boundary references and the
Tree Properties request. Outputs must retain the same pre-feature geometry,
reference entry/inspection, camera, final display and document transactions.
The available means are the existing command-owned rollback refresh and the
Tree dispatcher. No new cache or kernel calculation is needed.

Baseline: `e910eec4`. Tree dispatch first refreshed the complete result scene.
Boundary Surface Properties then installed rollback and refreshed the input
scene before configuring reference inspection and picking. The first scene was
not consumed. Boundary Surface now joins the existing guarded dispatch path
that omits this preliminary refresh. The command-owned refresh remains intact.

The guard still requires a Part container, no active properties window or
Sketch edit, a valid history index and a resolved active occurrence. Creation,
reference controls, command-local picking, geometry calculation, persistence,
shared placement and close/commit callbacks are unchanged.

## Measurement and verification

Serial Windows Release/Fusion runs use the existing GUI fixture: four separate
Sketch Features defining a 100 x 80 planar patch. This is a small synthetic
fixture, not a performance claim for a large user model. Each run opens the
same committed surface six times, accepts unchanged values three times and
cancels three times. Timing includes event processing; framebuffer hashing is
outside the opening timer. Warm means exclude the first opening.

| Metric | Before | After |
| --- | ---: | ---: |
| Warm Properties opening | 37.895 ms | 34.470 ms |
| Scene publications per opening | 2 | 1 |

The measured opening reduction is about 9.0% on this fixture.
The deterministic improvement is removing one unused publication; wall-clock
timing varies with system load. No claim is made for regeneration, closing,
overall application speed or larger/nested documents.

All six corresponding preview framebuffer hashes and six restored-frame hashes
match before/after. Every cycle checks exact serialized viewer-packet restoration,
unchanged camera, document status and Undo/Redo availability. The test now
requires exactly one scene publication. Existing GUI coverage also verifies
creation, Tree and View boundary selection, reference clearing/replacement,
independent inspection, short middle-click entry termination, middle double-click
confirmation, native save/load and Cancel restoring the original sources.

Localization review: no product UI text changes; added test diagnostics and
this documentation are English. Five-language catalog validation is included
in the regression checks. The local Windows application is rebuilt and the
root `zima-cad.bat` launcher is unchanged. Linux and portable packaging are
outside this change.

Evidence: [before/after and regression logs](20261001-boundary-properties-opening.txt).

Final validation: all seven selected contracts passed in 33.18 seconds: Boundary
Surface geometry/workspace, dialog and GUI; refresh scope; selection filters;
surface profiles; and translation catalogs.
