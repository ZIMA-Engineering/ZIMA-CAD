# Formed-nose washer library

Thirteen native Parts, M4-M27, are installed under
`06-WASHERS/05-LOCK/02-FORMED-NOSE`. They follow the original
[CSN 02 1753 (1962) preview](https://nahledy.normy.biz/n.php?i=283).
This is not verification of the 2003 edition. The original preview gives
outside diameters 14 mm for M4 and 46 mm for M22; the differing combined
archived table remains preserved at the parent directory.

The reviewed rows and primary-image hashes are recorded in
`tools/data/formed-nose-washer-reviewed-dimensions.json`. The saved Part uses
ordinary Flat/Bend/Flat history inside one Body. Its clipped, bored base has
a straight attachment edge of width `f` at distance `l`. A real native 90-degree
Bend leads to the attached straight nose leg. The nominal height `h` is measured
from the top washer face to the end of the nose. The selected inside radius
equals thickness and the selected unfolding factor is K = 0.5; these CAD choices
are not certified normative bend-radius or neutral-axis requirements.

Relations retain the leg width and calculate stock dimensions and mass. The
steel density is 7,850 kg/m3, with material CSN 11 321.21. Reported mass comes
from calculated geometry, not the indicative table mass. No native document
format, protected placement implementation or geometry tolerance was changed.

## Windows acceptance on 2026-10-20

- All thirteen Parts passed independent BRep validity, one-solid connectivity,
  cached and analytic section volumes, bounds, positive physical mass and native
  save/reopen. The analytic volume separately sums clipped annulus, curved Bend
  and straight leg. Source inspection creates no Undo history or dirty state.
- Each size passed 21 successive edits: five Sketch dimensions changed twice
  and restored, thickness changed twice and restored, then Bend angles
  80/85/90 degrees. All 273 steps check independent physical results, original
  face identity, Relations, inherited thickness/radius, fully constrained base
  and leg, Undo/Redo, explicit Regenerate and save/reopen. The angle Relations
  consume radians. Every size also passed native unfolding with equal volume.
- The native checker exercises repeated point dragging with unlocked and locked
  profile dimensions. Actual GUI checks on M4 and M27 edit the base outside
  radius three times, with Undo/Redo, regeneration and independently checked
  saved results. These finite cases are not an exhaustive dimension/constraint
  or mouse-interaction matrix for the general Sketcher.
- Thirteen native test Assemblies passed save/reopen, zero material interference
  and zero contact distance against authored steel test plates. Their clearance
  holes are selected fixtures. They do not certify the standard's mounting-hole
  dimensions or tolerance classes. The test fixtures are not factory models.
- The M8 reference has two associative first-angle views, real native axes,
  and seven manual dimensions: d, D, f, l, h, t and r. The base plus five
  localized Drawings passed regeneration and Save As/reopen with retained
  dimension identities and values. Each embeds the native A4 frame, localized
  factory title block and logo. All six verify scale 2:1, positive calculated
  mass, constructor ZIMA-Engineering and a valid date. The short reference
  filename fits the unchanged factory drawing-number cells.
- Five localized HTML/image pages, selectable-text A4 output and printable
  bounds passed. The complete fastener table suite passed 27 tests; generated
  HTML and metadata matched the reviewed dataset. Names and scope descriptions
  cover cs, en, de, fr and ru.

Evidence is recorded in `build/formed-nose-candidates-v5`,
`build/formed-nose-edits-v4`, `build/formed-nose-gui-v1`,
`build/formed-nose-seating-v2`, `build/formed-nose-thumbnails-v2`,
`build/formed-nose-reference-v7` and `build/formed-nose-drawing-check-v2`.
The installer checks source hashes, GUI results, seating receipts, native
projection conventions, title-field receipts and thumbnails before copying.
Different existing files are rejected; custom libraries remain preserved.

The reference work exposed two program defects, fixed separately: enlargement
ratios in `&sheet.scale`, and missing preceding Sketches while editing within
the active Body. See [title-block verification](NATIVE_TITLE_BLOCK_FAMILY.md).

Shared source and factory data are used on both platforms. Native Linux
execution requires its host. A new Windows release still requires a clean
committed-source build and packaged-runtime acceptance; these local checks
do not certify that pending release.
