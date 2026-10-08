# Form, sheet properties and drawing review

Review date: 2026-10-08. The implementation remains shared C++ with OCCT 8.0.0.
No native format fields or general container-placement contracts change.

## Causes and corrections

- Form's required populated support keeps replacement and inspection, without
  displaying an entry/clear indicator. Role descriptions move to translated
  reference tooltips; the table retains compact control and inspection columns.
  Preview and confirmed axes use a 10 mm nominal length without endpoint grips.
- Unfolded Form symbols are published once at each final calculated boundary,
  using successful native history and its material frames. They remain display
  overlays. Drawing Show/Erase owns its separate symbol annotations; the Drawing
  source adapter removes Part-only symbol overlays to prevent duplicate strokes.
- Pattern/Mirror retain Form's cut, flat and formed operands together. Each
  instance carries its own material region, native ancestry and reflected or
  patterned frame, including copies of copies and copying after Unbend.
- Rectangle inference now considers the two intermediate corners. Starting at
  the first Bend endpoint and moving the opposite corner can offer C at the
  second endpoint while retaining a free rectangle height. Confirmation uses
  the existing native coincidence/external-point constraint implementation.
- In `11.prtz`, Sheet Cut encountered an invalid analytic cylinder face after
  converting a calculated spline skin. The private converted face lacked valid
  edge curves in its surface parameter space. Build those curves on copied edges,
  repair the converted face and map its edges back to their native ancestry.
  Source geometry, the authored Sketch and its right angles remain unchanged.
  The shared kernel now links OCCT's TKShHealing; Windows uses `/bigobj` for the
  enlarged existing kernel translation unit.
- The rational-surface body-property path requested volume alone from GK, leaving
  centroid/inertia uncalculated. Adaptive Gauss now calculates the complete
  centroid and tensor at the existing precision. The established span-aware GK
  volume remains authoritative. This avoids the measured several-second cost
  of requesting every integral from GK for Form's offset spline surfaces.
  Existing saved Parts require explicit Regenerate to replace old cached
  integrals; opening Measurement does not calculate geometry.
- Centroid paths/axes show T at both actual ends. Presentation overhangs do not
  move native reference endpoints or create extra selectable geometry.
- Drawing opening axes use the opening's persisted coaxial cylinder envelope.
  Unrelated stock bounds no longer determine their axial length or cross radius.
  The 2 mm paper overhang follows view scale; curves retain model coordinates.

## Verification matrix

Native Form copies passed opposite mirror normals, circular and linear patterns,
a two-direction grid, a mirror of a pattern copy and copying after Unbend.
Checks include one connected valid BRep, independently integrated GK volume,
material-instance counts, flat symbol stroke counts, absence from original
placement-reference packets, Bend Back and exact native edge-packet roundtrip.
The independent additive-volume comparison is bounded by actual BRep surface
area times edge tolerance: rational Boolean trimming changes integration domains.
It does not relax the kernel calculation precision or the direct persisted-mass
comparison. Changed symbol geometry invalidates its native history fingerprint.

Native Pattern/Mirror commands offer Form as a source and preserve their source
chain through Undo/Redo and native reopen. Drawing symbols retain distinct copied
identities. Existing Form checks cover repeated thickness edits, replacement,
dependent-reference failure/restoration, suppression and repeated Assembly
occurrences. GUI Properties checks cover exact support picking, planar rotation,
rollback, reference replacement, unchanged OK, Cancel, definition replacement,
Undo/Redo and save/reopen.

Rectangle GUI covers twelve corner C, edge C/M and axis S cases. The four new
external-corner cases cover X/Y orientations and both free-height directions.
For each, the dimension matrix covers segment/point-pair selection, both point
orders, aligned/directional distance and driving/reference, locked/unlocked
states (128 combinations). A locked reference dimension is invalid and must be
rejected without a change. Valid drivers receive three successive value edits;
reference/unlocked dimensions allow dragging and locked drivers prevent escape.
Independent checks verify dimension equations, orthogonality, exact contact and
read-only external sources after every action and native roundtrip. Creation
also verifies actual hover/click, save/reopen and Undo/Redo. The attached-Flat GUI
case verifies both real Bend endpoints through mouse creation and Properties OK.
The full existing native Sketcher suite also passes. Trimming this particular
new intermediate-corner contact is not a separately established GUI variant.

Sheet body properties cover 30/90/180-degree Bends, Unbend and Bend Back, all nine
tensor entries and three centroid coordinates against independent GK with its
centroid/inertia flags enabled, plus an analytical rational-extrusion box.
Existing density, units, transformations, history and native persistence checks
pass. Existing Sheet State and complete Bend/Sheet Cut suites pass, including
rotated inputs, cylinder/cone supports, normal/clearance methods, bounded
projection and attachment cases.

The actual `11.prtz` recalculates as a valid connected solid (43922 mm3), retains
the final cut's native material-space ancestry and passes native roundtrip.
The personal file was explicitly regenerated and saved with a byte-verified
`11.prtz.before-0803-fix` backup. `01.drwz` likewise has a
`01.drwz.before-0803-fix` backup. Its reopened side-axis stroke is 27 mm; all four
head-on rays are 8.1881 mm. Source `01.prtz` is read-only during Drawing repair.
Synthetic plain/threaded openings independently verify both projections at
scales 0.5, 1 and 2, separate owners and native annotation roundtrip. Existing
Show/Erase, local annotation layouts, exact occurrences and Undo tests pass.
The actual Drawing comparison retains sheet content, view placement/settings,
annotation identities, visibility and layouts; only calculated source packets
and the two opening-axis envelopes change.
GUI pixel checks verify both T ends, selection colors and curved paths through
repeated state changes, display modes and native reopen.

All five translation catalogs and affected Form labels/tooltips passed the
localization contract. No new untranslated user-visible messages were added.
An accidentally selected broader startup test reproduced its separate Save Copy
fixture failure; this change does not claim that unrelated workflow is repaired.
Timing acceptance and signed Windows packaging are recorded in the release
record after clean-build verification. Linux build/GUI acceptance remains due on
the Linux host. These are focused regression matrices, not universal solver or
arbitrary Form-definition coverage.
