# FORM-KOULE external point motion

## Reproduction

The inspected native file has SHA-256
`A504B4E4F01BC8D2E8D90766BF2901243483685F46FDA8D9F149D1A4FF6A1B99`.
The source circle diameter is now 35 mm and its referenced point is at
local (17.5, 0). The Revolution Sketch still caches (15, 0), and its native
K-constrained arc endpoint remains there. Its auxiliary segment is perpendicular
to the vertical axis, with width 10.5 mm and vertical coordinate -8 mm.
A third Sketch and its 5 mm diameter remain independent of this repair.
The saved calculated Revolution reports:
`Refreshed external reference conflicts with Sketch constraints`.

The preceding fix made the standalone source point available to regeneration.
It did not verify a changed source point deforming this complete constrained
arc graph. The sequential refresh solve fails even though simultaneous circular
fitting can satisfy the new read-only point and all existing equations.

## Change

On a conflicting refresh of circular geometry, retry the existing simultaneous
circular fitter from the refreshed projection before sequential corrections.
Verify the candidate with the ordinary solver before accepting it. External
packets remain immutable inputs to the fit. Keep successful ordinary refreshes,
reference identity, dimension values, side choices and atomic rejection.
No shared placement solving, format, translations or runtime messages change.

## Native verification

Sixteen actual-Sketch cases cover four successive source positions
(17.5, 16, 18, 15 mm), driving/reference dimensions and locked/unlocked drivers.
After each refresh they check both cached and native K point positions,
independent circle endpoint and dimension equations, ordinary solve residuals,
unchanged read-only source geometry, further dimension edits, drag attempts and
serialized reopen. A completely fixed native graph rejects the changed source
atomically instead of moving geometry or dropping constraints.

The full document test regenerates the actual failed snapshot, then changes the
source diameter to 32, 36 and 35 mm successively. Each regeneration verifies the
source/cache/native point relationship in the real Sketch frames, retained
driving dimensions, no calculated feature errors and session Undo/Redo.
The corrected initial 35 mm document is saved separately before these test edits.
Its external cache and native contact are both (17.5, 0), the equal arc radii
are approximately 10.489831254 mm, and calculated surface area is
1045.1102193012148 square millimetres. The width remains 10.5 mm and the
vertical coordinate remains -8 mm. Comparing native definitions shows only
the dependent Sketch's points, arcs and refreshed external cache changing;
all its constraint identities and dimensions remain identical. The source and
third Sketch, history, placement definitions and other authored sections remain
unchanged. Calculated Body packets and reference-error state are recalculated.

Native entry points are `ZIMA_FORM_REFERENCE_MOTION=1` with
`ZIMA_FORM_ARC_DIAGNOSTIC` pointing at the extracted Revolution Sketch JSON,
and `ZIMA_FORM_SOURCE_MOTION_DOCUMENT` pointing at the native snapshot.
Logs are `build/form-reference-motion-native.log` and
`build/form-reference-motion-document.log`.

## GUI and final checks

`ZIMA_VERIFY_FORM_SOURCE_MOTION_DOCUMENT` exercises the complete native file
through the Windows GUI. It regenerates the failed snapshot, then opens the
source Sketch, double-clicks its diameter with actual mouse events, enters
32, 36 and 35 successively, exercises Undo/Redo, finishes the Sketch, confirms
feature Properties, regenerates and saves. After every step native reopen
verifies the source/cache/contact relationship, the unchanged 10.5/-8 drivers
and absence of calculated feature errors. All three edits passed. The test
uses the existing generalized Feature Sketch editor as well as the standalone
Sketch editor where applicable; no product dialog behavior was modified.

All ten targeted native suites passed after rebuilding against the changed
solver (17.10 seconds): viewer, Sketch serialization, Sketcher, keypoint
contact, external-reference commands, dimension commands, relation commands,
mirror/tangent, curve commands and Sketch commands. Independent checks on the
saved native geometry verify both tangencies, equal radii, perpendicularity,
axis incidence, K incidence, width and vertical coordinate.

Localization review found no changed product-visible text in cs/en/de/fr/ru.
The existing five-language checks passed in the immediately preceding repair;
this follow-up changes neither their catalogs nor runtime labels/errors.

After a concurrency hash check and strict authored-definition comparison, the
verified native save replaced `Projects/FORM-KOULE.prtz` atomically. The original
snapshot is retained as
`build/FORM-KOULE-before-external-motion-fix-20261016.prtz`.
The repaired file has SHA-256
`BEC5BF7502E22EC1BFEA690025A1901AB3A69E00DBCEA2F411017DF570BFF2B4`.
GUI and regression evidence is in `build/form-arcs-source-motion.out.log` and
`build/form-reference-motion-ctest.log`. The root development BAT still launches
the rebuilt Windows C++ executable.

## Scope

The concrete failure is an external point driving native circular contacts.
The new actual-file matrix retains its authored plane and contact sides; it
does not claim every external curve/face projection, orientation, trim case or
dimension kind. Existing external-reference and Sketcher regressions remain
required. Actual mouse dragging of the source point, as distinct from editing
its driving diameter, is not covered by the new GUI test; the new native matrix
tests dragging of the dependent circular geometry. Linux compilation and GUI
behavior are unverified on this Windows host.
