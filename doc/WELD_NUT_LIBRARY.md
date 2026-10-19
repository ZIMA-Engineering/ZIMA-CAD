# Reviewed weld-nut library

The historical CSN 02 1455 offer has been retired from the visible factory
library by agreement with the user. Its original source evidence is preserved.
DIN 929 hexagon weld nuts and ISO 21670 flanged hexagon weld nuts are separate
families with separate dimensions, not interchangeable labels for that source.
The factory library now contains 16 DIN 929 and 14 ISO 21670 native models,
including the reviewed coarse- and fine-pitch combinations. Each family has
its own descriptive directory, metadata, size table and native artwork.

## Placement and native history

The three welding-projection contact faces lie on XY at Z = 0. The thread axis
passes through the Origin and the nut extends in positive Z. This is the
unwelded placement representation; it does not simulate projection collapse,
welding forces or the resulting weld.

Both families use ordinary native Sketches, Default extrusions, a circular
Pattern, edge treatments, and a finite threaded Opening. Projection profiles
reference actual preceding rim geometry rather than fixed disconnected points.
The Opening references the actual front pilot face (DIN) or flange face (ISO).
Relations connect feature profile offsets, projection depth, thread length,
stock designation and calculated mass. No persistence or shared placement
implementation changes are required.

DIN 929 has three alternating triangular corner projections and a centering
pilot. Its overall height is m. ISO 21670 uses three rounded annular-strip
projections on a flange; its m excludes projection height c, giving overall
height m + c. The ISO profile uses b and g, its specified projection and flange
radii, and a selected 0.5 mm hexagon root radius. The back hexagon pressing
contour is represented by a selected 30-degree bevel. These nominal CAD details
do not certify every manufacturing tolerance, material or weldability property.
Material is generic steel with a declared density, without an invented strength
or welding certificate.

## Sources

Reviewed dimensions and provenance are recorded in
`tools/data/weld-nut-reviewed-dimensions.json`.

- [DIN 929:2013-12](https://www.dinmedia.de/en/standard/din-929/194678780).
- [Bollhoff DIN 929 dimensions](https://eshop-sk.boellhoff.com/out/media/pdf/DIN_929_Stahl___en.pdf).
- [Aspen DIN 929 dimensions](https://www.aspenfasteners.com/content/pdf/Metric_DIN_929_spec.pdf).
- [ISO 21670:2014](https://www.iso.org/standard/63369.html), Table 1 and Figure 1.

Third-party PDF evidence stays outside distributed factory assets. Fine-pitch
availability and the limitation of the retrieved DIN preview are recorded in
the reviewed data; do not infer additional catalogue combinations.

## Verification recorded on 2026-10-19

All 16 DIN candidates passed independent BRep validity, single-solid,
three-projection, pilot, Origin seating, volume, mass and locked/unlocked Sketch
dragging checks. The edit matrix passed 288 successive driver edits and 24
catalogue pitch changes, including independent geometry equations, reference
identity, Undo/Redo and save/reopen. Evidence is under
`build/weld-nut-din-edit-matrix`.

All 14 ISO candidates were calculated, independently measured and reopened.
The native checker verified connected solids, three correctly spaced
projections, exact contact areas at Z = 0, finite thread placement, volume,
mass and constrained Sketch dragging. The repeated edit matrix passed 336
successive driver edits and 24 catalogue pitch changes under
`build/weld-nut-iso-edit-matrix`. The M8 pilot of each family also passed actual
mouse dimension editing: three DIN projection-depth edits and six ISO
width/radius edits, retaining signed contact sides, source identities,
Undo/Redo, regeneration and native save/reopen. Both saved GUI outputs passed
the independent BRep checker (`build/weld-nut-din-gui-independent` and
`build/weld-nut-iso-gui-independent`). M8 x 1 fine-pitch examples also passed the
same mouse editing, reference-side, Undo/Redo, regeneration and independent
saved-geometry checks in `build/weld-nut-{din,iso}-fine-gui-independent`.
Thirty source-preserving
320 x 240 thumbnails have been rendered; the ISO thumbnail bounds include its
flange diameter and complete m + c height. An earlier hexagon-only bound cropped
the flange and was rejected.

`tools/render-weld-nut-library.py` reuses the existing native reference-drawing
pipeline. DIN artwork has six associative dimensions; ISO artwork has eight.
Both contain actual thread axes and a first-angle projected right view.
All twelve base/localized native Drawings retain resolved dimensions, values
and identities through regeneration and save/reopen. Receipts are in
`build/weld-nut-{din,iso}-art-verification`; the approved rendered inputs are
`build/weld-nut-din-art-v3` and `build/weld-nut-iso-art-v2`.

Promotion uses `tools/install-verified-fastener-candidates.py --kind din-weld`
or `--kind iso-weld`. It requires unchanged source digests, all 624 successive
driver edits, 48 catalogue changes, the coarse/fine GUI receipts, native drawing
round trips, unique model filenames/identities, nonzero published mass and
320 x 240 thumbnails before copying. Each source was explicitly regenerated
and saved during authoring. No user-authored library file is replaced.

`tools/prepare-weld-nut-pages.py` publishes the two reviewed size tables and
five-language figures through the established reference-page generator. The
retired table remains in `retired_pages` and the original curated/source data;
the numeric-preservation test includes it. Only its generated visible landing
pages were removed. The new figures use black geometry on an opaque white sheet,
remaining readable in either application theme. Other unreviewed nut families,
including the historical blind-cap candidates, are not promoted by this change.

The volume oracle integrates the bevel and rounded projection sections.
At the ISO hexagon root, intersecting fillet cylinders have mitred hexagon
corners: an outward section offset t adds P*t + 6*tan(pi/6)*t^2. A rounded
Minkowski buffer's pi*t^2 term is not the geometry of this feature. The oracle
uses the former independently derived expression, without relaxing its
comparison tolerance.

These checks ran on Windows with the shared native implementation. Native
Linux execution remains unverified. No new user-visible application text was
introduced; the family names in the generator cover cs, en, de, fr and ru.
