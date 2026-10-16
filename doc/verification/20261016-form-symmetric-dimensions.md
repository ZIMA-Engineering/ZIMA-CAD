# FORM-KOULE reference and symmetric-dimension repair

## Input and causes

The user-authored `Projects/FORM-KOULE.prtz` snapshot has SHA-256
`785FD14484B829ECC5297DC1C180D8277898B26E27B372C9516712B94CB6D267`.
It differs from the earlier arc-investigation snapshot. Its Revolution owns
two equal-radius tangent arcs, a finite auxiliary segment, a vertical native
construction axis, a symmetric width dimension and a vertical coordinate.

The red Revolution came from a falsely broken external point reference to
the preceding standalone Sketch. Independent reference refresh found that
point, but regeneration omitted authored standalone Sketch geometry from its
source packet. Regeneration now consumes the existing authored geometry path,
deduplicating persisted reference identities and retaining the existing
preceding-owner filter. Placement solving and source identities are unchanged.

The circular fallback fitter lacked the existing point-to-axis symmetric
dimension equation. It now fits half the displayed width for each participating
point, using the same absolute-distance equation as the ordinary solver.
Signed point-line equations retain their persisted side. Failed segment-pair
transactions may use the same simultaneous circular fit, followed by ordinary
solve and redundancy validation. Successful ordinary solves keep their existing
path. This allows the auxiliary segment to become perpendicular to the vertical
base axis or the native vertical construction axis without dropping dimensions.

The vertical dimension is a signed coordinate below the Sketch origin. Its
displayed value is negative. Entering a positive coordinate requests the other
side of the origin; it is not an unsigned edit of the existing height.

## Verification

The actual-file native matrix exercises the original graph and both valid
perpendicular supports, driving/reference dimensions and locked/unlocked
drivers: 12 combinations. Each performs three successive dimension edits and
center-drag attempts, checking ordinary residuals, independent arc endpoint
and dimension equations, immutable external packets, actual accepted motion,
atomic rejected motion and serialized reopen. Edits exercise both full dimension
application and the displayed-value setter. The actual-file diagnostic also
checks width values 0, 2, 5, 7, 10, 12 and height magnitudes 2, 5, 7, 10, 12.
Perpendicularity to the horizontal axis and height zero remain rejected for
the supplied graph; they are not presented as repaired valid operations.

Regeneration finds the preceding Sketch point on successive runs, reports no
Body calculation errors, retains authored native points, and saves/reopens the
repaired reference. Comparing the original and repaired native documents shows
only the external point's `broken` flag changing from true to false and the
corresponding definition fingerprint changing. Calculated Body data is identical.
The permanent reference-command regression also checks standalone Sketch point
availability and deduplication against calculated reference packets.

Ten targeted native suites passed: viewer, Sketch serialization, Sketcher,
keypoint contact, external-reference commands, dimension commands, relation
commands, mirror/tangent, curve commands and Sketch commands (final run:
18.82 seconds). The actual-file native matrix additionally verifies atomic
rejection of dragging after every authored point is fixed. Reference repair
also passes native session Undo/Redo before save/reopen.

The actual-document GUI check passed both vertical supports. Real mouse picks
create perpendicularity, and double-clicking each displayed dimension performs
three successive signed value edits, each followed by Undo/Redo. Feature OK and
native save/reopen retain perpendicularity and both final values; the saved
graph satisfies the ordinary equations. GUI Regenerate and save clear the
falsely broken point reference. Both cases passed. The test initially attempted
to enter an unsigned positive height, which correctly requested the opposite
coordinate side and was rejected; the corrected test uses the displayed signed
coordinate convention.

No runtime labels, translation keys, error text or document format changed.
Localization review therefore found no new strings to translate in cs/en/de/fr/ru.
The existing five-language translation/catalog and boundary-text GUI regression
was rerun successfully for all five supported languages.

The verified native save replaced the unchanged user file atomically after a
SHA-256 concurrency check. The exact original is retained at
`build/FORM-KOULE-before-reference-repair-20261016.prtz`. No new perpendicular
constraint or different dimension value was imposed on the authored document;
the repaired application supports those edits interactively.
The repaired file has SHA-256
`9B99E42250CCB11F449562DC86DB7F107F8FD0C391D30E0FF423F8CC55676BE7`.

Evidence logs are `build/form-current-final-ctest.log`,
`build/form-current-matrix.log`, `build/form-current-reference-final.log`,
`build/form-arcs-dimensions.out.log` and `build/form-current-translations.log`.
The actual-file GUI entry point is `ZIMA_VERIFY_FORM_DIMENSIONS_DOCUMENT`.
Native actual-file checks use `ZIMA_FORM_REFERENCE_DIAGNOSTIC`, or the extracted
Sketch JSON with `ZIMA_FORM_ARC_DIAGNOSTIC` and
`ZIMA_FORM_DIMENSION_DIAGNOSTIC=1`.

## Limits

This Windows investigation does not verify a Linux build. The actual-file
matrix retains its authored XZ orientation and contact sides; the wider native
regressions cover other geometries and orientations, but are not evidence of
every symmetric two-point, external-line, trimming or fully constrained variant.
The new actual-file dimension matrix exercises native center dragging; actual
mouse center dragging under these two dimensions is not included in the new GUI
check. The earlier arc-only GUI matrix exercises actual center dragging.
Earlier arc-only verification and its recorded limitations remain documented in
`20261016-form-arcs-and-auxiliary-geometry.md`.
