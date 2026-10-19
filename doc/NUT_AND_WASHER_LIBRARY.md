# Nut and washer library authoring

## Agreed placement and state

Nut and washer models use the thread/hole axis through the Origin, with the
seating face on XY at Z = 0 and material extending in positive Z. This is an
authoring convention for stable assembly references; it does not change shared
container placement or geometry-side rules.

The library default for split spring washers is the **compressed assembly
state**, confirmed by the user on 2026-10-18. Build the annulus from two
Origin-constrained circles, extrude by the section thickness on one side, and
subtract an Origin-based trapezoidal split profile. Do not use the free height
as the extrusion thickness. The model description and metadata must identify
the compressed state in all five library languages.

The split Sketch lies in a plane containing the washer axis. Its upper and
lower boundaries contact the two actual extrusion end planes through read-only
external face references. The oblique sides are parallel at 45 degrees; their
perpendicular gap is 1 mm, as agreed with the user. A construction diagonal
centers the opening across the thickness. Subtract through one radial side
only, starting inside the bore. Verify both diameter changes and thickness
changes, unchanged external source identity, save/reopen and Undo/Redo.

The engineering table illustration and its height dimensions describe the
**free state**. Retain that distinction when producing native model/drawing
artwork; a compressed illustration must not display the free-state height as
its measured thickness. A second Family state has not been requested.

Ordinary flat washers use solid profiles. Use sheet-metal modeling where bends
or unfolding actually belong to the part, rather than treating every washer as
sheet metal merely because it is thin.

## ISO-first dimensions

The user prefers ISO definitions wherever a corresponding ISO family exists.
An approximate cross-reference does not authorize simply renaming historical
CSN dimensions. The reviewed plain-washer dataset records the 2000 editions of
ISO 7089, ISO 7090, ISO 7091 and ISO 7092, including their actual nominal size
ranges and dimension limits. Author their native models from these values.
The previous CSN data differ at several sizes, including the M5 thickness,
large clearance bores and some outside diameters.

The current set contains 102 ISO sizes and 32 historical CSN sizes outside the
applicable ISO range. Keep those out-of-range sizes explicitly historical;
they must not inherit an ISO designation. Archived source cells remain intact
in the data and are excluded only from the primary ISO presentation. Native
artwork uses the upper face so that the ISO 7090 outside chamfer is visible.
Its chosen CAD chamfer is 45 degrees and depth h/3, within the published
30–45 degree and h/4–h/2 limits. The bottom seating face remains at Z = 0.

Sources: [ISO 7089](https://www.iso.org/standard/13666.html),
[ISO 7090](https://www.iso.org/standard/13669.html),
[ISO 7091](https://www.iso.org/standard/13672.html), and
[ISO 7092](https://www.iso.org/standard/13674.html). Public preview URLs used
to review the dimensions are recorded alongside the numeric dataset.

## Hexagonal nut candidates

Regular, thin, high, unchamfered and grade-C hexagonal candidates cover the eight
existing ISO table families, with one model for each distinct diameter/pitch
combination. Do not duplicate the same geometry as separate A and B models.
The default solid is a centered hexagonal prism extending from Z = 0 to m.
Optional outer chamfers are conical revolved cuts at beta = 30 degrees, with
their profiles attached to persisted original hexagon vertices. There is no
optional raised washer face. ISO 4036 retains its unchamfered outside form.

Use the current native threaded-opening operation through the complete nut,
with 90-degree entrance/exit countersinks. Relations keep bore/thread lengths
equal to the prism height and bevel widths tied to the hexagon size. Catalog
nominal diameter and pitch are read-only Relations inputs; they cannot be
assigned as independent scalar dimensions or exposed as editable Family fields.
The stock description follows the actual catalog selection.

Ten demonstrated transcription errors in the grade-C table are corrected
against ISO 4034:2012 Tables 1 and 2, while retaining the original archived
cells. The reviewed correction file records every before/after value. The
2012 table and its range must not be described as the current ISO 4032:2023
edition. Thread geometry is the application's native symbolic thread model;
the library is not a manufacturing gauge or a mechanical load certification.

## Verification before installation

Fastener directory display labels describe geometry and execution, without
standard numbers. The agreed scope includes screw directories, the cylindrical
head washer directory and both castle-nut directories. Standards remain in
reference tables, model metadata and native model filenames. This changes
localized metadata labels and page titles, not filesystem paths or document
identities. Product-grade distinctions in otherwise similar screw directories
remain visible. All five language labels are updated together.

All 17 nut dimension pages use the same row-per-thread screen presentation and
fixed header. Split size ranges are merged without modifying archived source
cells. Castle-nut cotter-pin diameter and length headings have presentation-only
aliases, so separate source ranges produce one column each. Their alternate
thread row remains a separate column. The source-cell reconstruction tests and
five-language HTML checks cover these transformations. Category landing pages
without dimension tables are not converted into artificial tables.

Multi-view library illustrations use one base view and native projected child
views, following the sheet's first-angle projection method. Do not position an
independent camera as an apparent orthographic companion. The projected view
inherits the source and scale; its position is defined by projection direction
and distance. Dimension attachments remain native references. Washer diameter
annotations are mirrored: d1 at the upper left (135 degrees), d2 at the upper
right (45 degrees), with space outside the circular outline. This applies to
plain, compressed spring and tab-washer illustrations in all five languages.
Both views show the actual circular-profile axis through shared Show/Erase.
Prefer the outside circular profile when it exists, avoiding duplicate coaxial
annotations; a tab washer uses its bore axis. The installation validator checks
visible resolved axes and the persisted view-plane label directions, rather
than assuming a major arc shares a full circle's radial parameter zero.
The remaining generic DIN washer schematic also uses mirrored diameter labels,
centerlines in both views and an aligned vertical side silhouette.

Flat tab-washer candidates retain exact tangent arcs, shared native junction
points and one equal-radius relation between the roots. A construction segment
between root centers has its midpoint on the sketch axis. Circular initialization
must support this midpoint equation and signed point-to-axis coordinate drivers;
otherwise a feasible radius edit leaves adjacent arc endpoints inconsistent
before validation. This adds existing equations to the bounded initializer;
the ordinary solver continues to validate the complete constraint graph.
Candidates remain separate until the repeated-edit, interaction and artwork
checks are complete. The 19 flat tab washers were installed after these checks,
including their native drawings, five localized illustrations and metadata.

The October 19 candidate verification covers 19 tab washers and 342 successive
edits: bore diameter, outside radius, root radius, tab width, tab-end coordinate
and extrusion thickness, each changed twice and restored. Every edit includes
Undo/Redo, regeneration, native save/reopen, face-reference identity and an
independent tangent-outline volume/mass check. The source candidates remain
unchanged. A separate Sketcher matrix covers four rotations, both lock states
and both constraint enumeration orders, with repeated dimension edits, dragging
and serialization. Unlocked drivers retain the established direct-manipulation
behavior; locked drivers must retain their solved coordinates. The complete
native Sketcher suite passes. A Windows GUI check additionally performs 15
successive mouse-double-click edits of all five profile dimensions, Undo/Redo,
regeneration and native save/reopen on the M3 tab washer. This does not establish
GUI coverage for other sizes, underconstrained/reference-dimension variants, external midpoint
supports, trimming of this outline or Linux; those remain verification gaps.

Persisted illustration checks cover 102 drawings (17 groups, six native drawing
files per group including the five localized copies): seven plain-washer groups,
two spring-washer groups and eight hex-nut groups. Each retains its parent view,
first-angle right projection, shared source and scale, aligned placement and
the corresponding orthogonal camera. The installation validator rejects an
independent second camera for these library kinds.
Both nut views explicitly show the actual model thread axis through shared
Show/Erase. End-on projection derives the center cross from that same axis;
the side projection derives its longitudinal centerline. These are persisted
model annotations, not raster strokes added after export. The installation
validator also requires a visible, resolved axis in each nut view.

Keep candidates separate from the factory library until independent geometry,
dimension, material/mass, editable history, reference identity, save/reopen and
Undo/Redo checks pass. Native Part/Drawing artwork and white-background previews
must come from those checked models. Check globally unique model filenames,
all five languages, and the packaged native runtime before release.

## Castle nuts and small hexagon nuts

The October 19 factory addition contains 53 historical castle nuts: 28 ordinary
and 25 thin executions, including the listed coarse and fine threads. They
retain the archived CSN dimensions and designations; no current ISO equivalence
has been established. Three native two-sided Through All cuts form six slots.
Each slot has a semicircular root with radius n/2, shared native junctions and
tangency to its straight flanks. An auxiliary native point at the slot floor
keeps floor height independent of slot width. Relations synchronize all three
cuts, crown height and total height. Crown turning references a stable bottom
vertex of the original hexagon through a read-only external reference.

All 53 variants passed 916 successive geometric/catalog edits, including
Undo/Redo, regeneration, save/reopen, stable face references, derived stock and
independent section volume and BOM mass. The checker examines actual retained
thread mesh vertices and edges against all six removed slots, including their
rounded roots. Polyline chord midpoints are checked in straight slot regions;
they are not mistaken for exact circular-boundary points at the rounded roots.
Receipts are under `build/castle-nut-complete-edit-matrix/`. Native projected
artwork includes axes and dimensions s, (e), d, h, p, n and D where a cylindrical
crown exists. The table defines h as total height and p as slot-floor height.

Fifteen small hexagon nuts retain CSN 02 1402 dimensions and the original
15-degree outer bevel. They passed 165 independent geometric/catalog edits,
reference, stock, mass, Undo/Redo and save/reopen checks. Their drawing uses m1
for height. Native illustrations use readable plain digits because the selected
CAD stroke font has no Unicode subscript glyphs. The companion small
castle execution uses m2 for total height and h for slot-floor height. For its
M12-and-larger crown, the archived table specifies axial relief c but no separate
crown diameter; the candidate CAD execution explicitly selects the inscribed
diameter s. This selected diameter must not be presented as an additional
tabulated normative dimension. All 15 companion small castle nuts passed 225
successive geometric/catalog edits, independent volume/mass, reference, stock,
Undo/Redo and native save/reopen checks before installation. Both small-nut
executions share their table page and have separate native projected figures.
All five localized images were regenerated with readable m1/m2 labels.

Rounded slot intersections exposed an inaccurate fixed-order volume integral
on non-isoparametrically trimmed cylindrical surfaces. Such surfaces now use
adaptive OCCT integration, as trimmed toroidal surfaces already did. Complete
rectangular-UV cylinders retain the established fast path. The native document
and OCCT suite passes. Its direct construction/Sketch fixtures now select their
intended local work planes explicitly instead of relying on factory defaults;
the production shared-placement implementation was not changed. A Windows GUI
check on the ordinary M12 castle nut passed six actual mouse edits of slot width
and floor height, with Undo/Redo, regeneration and native save/reopen; its source
candidate remained unchanged. This does not establish GUI coverage for every
size, the small-crown relief variant, or Linux. Factory cached-property auditing
and measured performance evidence are recorded separately.

The read-only factory audit checked 863 native Parts, including 116 solid Bodies
with trimmed cylindrical surfaces, and found no stale stored volumes or load
errors (`build/factory-trimmed-cylinder-properties-audit.json`). Open FORM role
surfaces are not treated as solid volumes. The audit neither saves assets nor
creates undo transactions.

Measured explicit regeneration used seven timed edits after two warm-up edits
on disposable source-identical copies. On the M12 castle nut, the median changed
from 1.0262 s to 1.0688 s while maximum independent volume error fell from
0.00247763 to 0.000001435 mm3. On an ordinary ISO 4762 M10x40 screw, medians were
0.2208 s and 0.2175 s; this difference does not establish a general speedup.
The measurement receipt is
`build/cylinder-properties-measured-comparison-v3/results.json`. These are Windows
measurements; Linux has not been verified for this change.

## Blind cap-nut candidates

`tools/generate-cap-nut-library.py` stages 21 historical CSN 02 1431 diameter/
pitch combinations from M6 to M30. It preserves s, overall m5, minimum threaded
depth b and the archived p column as separate dimensions. The current candidate
interprets p as cylindrical blind-bore depth; this interpretation and the end
geometry remain under review and must not be treated as an approved standard
execution. A native revolved arc
cuts the crown from an Origin-seated hexagonal prism. The arc is supported by
the actual original prism vertex, not an arbitrary point in space. A native
threaded opening has a finite bore, finite thread length and no drill point.

The archived figure does not dimension the crown radius or seating bevel.
These candidates explicitly choose crown R=2*d and a 30-degree seating bevel.
They remain outside the factory library pending final review, artwork and GUI
checks; do not relabel them DIN 1587 or claim complete manufacturing validation.
The user identified an excessively thin end wall in the M8 illustration on
2026-10-19: m5=16.5 mm and interpreted p=16 mm leave only 0.5 mm at the pole,
and approximately 0.15 mm axially at the bore rim with the chosen sphere.
`build/cap-nut-native-candidates-v2/end-wall-review.json` records the same check
for all 21 candidates (minimum approximately 0.122 mm). Valid solids and passing
edit tests do not establish manufacturability. Installation remains blocked
until the p definition, closed-end geometry and dimensions are corroborated.
Do not arbitrarily shorten the archived p value or substitute another standard.
The archived k column is retained in the manifest, but its meaning is not
established by the existing figure and it is not treated as another driver.

When R equals overall height, the independent arc centre happens to coincide
with the Origin. The rotation axis must not reuse that centre point: otherwise
changing R or height would introduce an unintended dependency. The axis owns
its separate Origin contact. Native repeated-edit checks exercise both drivers
and the original external support, rather than accepting initial coincidence
as proof of editability.

`--cap-nuts` in the native fastener checker verifies one valid solid, a genuinely
blind end wall, the crown's sphere equation, exact independent section volume,
stored mass, finite thread display and repeated locked/unlocked Sketch drags.
`tools/verify-cap-nut-library.py` exercises height, flats, crown radius, bore
depth and thread length with Undo/Redo and save/reopen. An interrupted run can
resume completed receipts while retrying the incomplete model in a new folder.
The checker is pinned to an immutable executable snapshot so another build
cannot interrupt a verification process.

The M16 GUI fixture passed three actual mouse radius edits, Undo/Redo and
save/reopen; its saved result passed an independent native geometry check in
`build/cap-nut-m16-gui-independent`. The R label overlaps the H glyph at its
lower end; the test selects an exposed part through the ordinary ordered picker.
`tools/render-cap-nut-library.py` produces native projected views and an axial
section with axes and six dimensions, localized in all five languages. All six
drawings in `build/cap-nut-native-section-art-v3` retain dimension values and
identities through regeneration and save/reopen. These are review candidates,
not installed library assets; their annotations explicitly identify the chosen
CAD crown radius.

The crown also exposed the drawing-only collapsed-triangle defect described
in [DRAWING_COLLAPSED_TRIANGLES.md](DRAWING_COLLAPSED_TRIANGLES.md).

## Knurled nut candidates

The staged historical CSN 02 1461/1462 set contains 22 models, covering M1,
M1.2, M1.6, M2, M2.5, M3, M4, M5, M6, M8 and M10 in both executions.
`tools/generate-knurled-nut-library.py` authors one constrained revolved
meridian, native threaded opening, mouth treatment and one circular Pattern
of a cut. The high execution includes the tabulated seating-side clearance.
The low execution has no zero-length hub segment: its head height follows
the driving overall height minus the shoulder radius.

The selected CAD straight-knurl detail has a 90-degree profile, depth t/4 and
an integer groove count rounded from pi*D/t. The archived nut dimensions do
not specify all these manufacturing details; these choices do not establish
an ISO equivalent or a manufacturing prescription. Seating remains at Z = 0.

The base geometry gate checks all 22 valid solids, actual Pattern flank counts,
shoulder tori, thread clearance, stored mass and independently integrated
section volume. The repeated-edit gate in
`tools/verify-knurled-nut-library.py` additionally checks native master edits,
Undo/Redo, original blank/thread face identities, derived stock and save/reopen.
Its results are recorded per completed variant; an unfinished run does not
qualify the complete set for installation. Representative GUI checks exercise
height, diameter and radius through the actual mouse dimension editor.
`tools/verify-knurled-nut-catalog.py` separately exercises feasible changes of
thread pitch and nominal diameter, retains the authored mouth treatment and
clearance, and verifies derived stock, independent properties, source references,
Undo/Redo and save/reopen. Infeasible larger bores are not manufactured merely
to obtain another test case. Installation also requires the final GUI outputs
to pass the independent native geometry checker through
`tools/verify-knurled-nut-gui-results.py`.

`tools/render-knurled-nut-library.py` creates an axial section using a native
projected child view. Both views have native axes. High-nut D1 is farther from
the part than d3; p and k share a dimension height. ASCII h1/h2 avoid unsupported
subscript glyphs. All twelve neutral/localized drawings passed regeneration
and save/reopen with resolved dimensions in
`build/knurled-nut-aligned-art-hash-reopen/results.json`; all 22 white-background
thumbnails were rendered without changing their model sources. Installation
requires the complete edit matrix and representative GUI receipts as well.

Remaining special families must retain their actual construction. In particular,
do not replace blind cap nuts, welding projections, two-part locking nuts,
nonmetallic locking inserts or wing/knurled profiles with ordinary hexagon nuts.
Original figures can be compared read-only in the user's ZIMA-Parts engineering
tables. Knurl geometry should consume the existing native circular Pattern of a
cut, rather than insert hundreds of separately authored history operations.
[ISO 13444:2012](https://www.iso.org/standard/53788.html) specifies a 90-degree
profile angle for its knurling series; that does not establish interchangeability
between a historical CSN nut and another nut standard. Any chosen CAD detail
not specified by the archived table must be identified explicitly and verified.

The archived wing-nut example incorrectly repeats CSN 02 1461, the knurled-nut
identifier. The official [UNMZ bulletin 5/2002, printed page 41](https://www.unmz.cz/wp-content/uploads/Vestnik_2002_05.pdf)
identifies CSN 02 1665 as wing nuts. This establishes the historical identifier,
not dimensional interchangeability with DIN 315. The archived wing dimensions
must not be silently relabeled DIN 315: for example, their M6 g1 is 4 mm,
whereas the [Bollhoff DIN 315 product sheet](https://eshop.boellhoff.de/out/media/pdf/DIN_315_Messing___en.pdf)
gives a 2.3 mm maximum for that dimension. Preserve the original source text as evidence
and use the corrected identifier when authoring the historical family.

Historical dimension sources require consistency checks. In particular, the
archived spring-washer table lists a 31.5 mm outside diameter for the M24
square-section washer together with a 24.5 mm bore and 5.5 mm radial width;
these values disagree. Resolve the source discrepancy before authoring or
publishing that size. The M10 square-section row also lists a 2 mm section but
a 10.2 mm bore and 15.2 mm outside diameter (2.5 mm radial width). Do not silently
copy either discrepancy or label an inferred correction as a reviewed normative
value. The reviewed correction file records the original cells, nominal
corrections, mathematical consistency checks and independent historical table
source. It corrects the M10 section to 2.5 mm and the M24 outside diameter to
35.5 mm. It does not import tolerances or other dimensions from another edition.
The generator rejects changed source cells and reports any remaining inconsistent
rows separately rather than authoring them silently.
The corroborating scan is PDF page 90, printed page 183, in the linked historical
table source. Its other sizes differ from the repository's later table and
must not be substituted wholesale.

The 45-degree axial cut and 1 mm gap are the agreed CAD representation, not a
claim of spring behavior under load or a normative manufacturing-gap dimension.
The earlier planar angular-sector prototypes were rejected and must not be
installed or used for library artwork. The rounded annular section uses the
tabulated corner radius. Verification independently integrates the straight
removed strip through the rounded annular sections and checks the actual two
parallel BRep planes, their slope, gap and seating bounds.
