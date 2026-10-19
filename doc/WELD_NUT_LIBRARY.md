# Reviewed weld-nut candidates

The historical CSN 02 1455 offer is to be retired from the visible factory
library by agreement with the user. Preserve its original source evidence.
DIN 929 hexagon weld nuts and ISO 21670 flanged hexagon weld nuts are separate
families with separate dimensions, not interchangeable labels for that source.
Neither new family has been installed yet; promotion requires the remaining
editing, GUI, artwork, metadata and table checks.

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
`build/weld-nut-iso-gui-independent`). Fine-pitch GUI mouse editing and the
published dimensional artwork remain unverified. Thirty source-preserving
320 x 240 thumbnails have been rendered; the ISO thumbnail bounds include its
flange diameter and complete m + c height. An earlier hexagon-only bound cropped
the flange and was rejected.

The volume oracle integrates the bevel and rounded projection sections.
At the ISO hexagon root, intersecting fillet cylinders have mitred hexagon
corners: an outward section offset t adds P*t + 6*tan(pi/6)*t^2. A rounded
Minkowski buffer's pi*t^2 term is not the geometry of this feature. The oracle
uses the former independently derived expression, without relaxing its
comparison tolerance.

These checks ran on Windows with the shared native implementation. Native
Linux execution remains unverified. No new user-visible application text was
introduced; the family names in the generator cover cs, en, de, fr and ru.
