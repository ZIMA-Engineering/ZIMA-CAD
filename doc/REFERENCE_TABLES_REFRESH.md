# Engineering reference library refresh

## Presentation contract

On 2026-10-15 the user expanded this work to the complete library and withdrew
the one-sheet A4 limit. Group by technical content, with vertical scrolling and
paginated portrait A4 output. Wide matrices scroll horizontally on the **same HTML page**, retaining their
identifying column. Printing splits them into readable column panels with that
column repeated in every panel. Do not create additional
tree folders merely to fit a page or shrink text to illegible sizes.

Original explanatory schematics precede applicable dimension/geometry tables.
They identify the illustrated type, not every parameter or a complete normative
manufacturing profile. Material/property references need no decorative image.
Group folders use File Manager's automatic overview, without authored HTML menus.

All authored pages support CS, EN, DE, FR and RU, together at the upper right.
All active reference pages use the full available window width and left-aligned
content and figures. The shared header keeps the language links and original
ZIMA-Engineering logo on the right. `tools/data/reference-ui.json` owns the
optional `branding` record: its `logo` is a repository-relative local asset;
`url` is the standalone HTML click target. An empty URL retains the logo without
a hyperlink; `branding: null` omits it. In the native File Manager, the existing
Settings > File Manager > Company website field (`FileManager/CompanyWebsite`)
controls the logo's actual link without regenerating HTML. Clearing that setting
retains the logo without a link. Only HTTP/HTTPS company addresses are used;
settings are serialized safely into the existing page-presentation script.
The default target is the existing company website.
No network asset is loaded while displaying a table. Cell padding provides more
horizontal space, and narrow windows retain their existing table scroll regions.
Printing hides this navigation header and retains the independent A4 layout.
Reference images keep their aspect ratio and occupy at most one quarter of the
window height, with a 720-pixel width cap. Print figures are capped at 60 mm high.
The screw illustration retains only the main side view and explanatory detail
profiles; final dimension presentation will follow the user's edited native Drawing.

Language changes prose and decimal punctuation, not values or the standards
system. Prefer ISO where its scope actually fits. A national adoption or
interchangeable replacement requires evidence; language does not select one.

HTML follows the live native palette without reload; standalone pages follow
system light/dark mode. Printing uses white paper, 12 mm margins, repeated
table headings and unsplit rows. A reference may continue over multiple sheets.
`01.jpg` and `01_{cs,en,de,fr,ru}.jpg` show a bounded actual preview; long references
remain complete in HTML. Groups use real descendants, never unrelated previews.

## Current inventory

| Dataset | Logical references | Localized HTML files |
| --- | ---: | ---: |
| Quantities and units | 20 | 100 |
| Other General standards | 19 | 95 |
| Tolerances | 54 | 270 |
| Threads | 26 | 130 |
| Shafts, splines and gears | 35 | 175 |
| Bolts, screws, nuts and washers | 73 | 365 |
| Remaining chapters | 54 | 270 |
| **Total** | **281** | **1,405** |

The tree has **333 directories / 1,665 localized labels**, including the native
ISO 4017 model directory. The earlier consolidation removed 423 redundant
navigation directories from the 755-directory published tree.
Related dimensional ranges share a document; useful technical topic groups
remain. All numerical source values, evidence references and review limits are
retained. Category logo-text SVGs follow the existing Symbols visual style.

From shafts through the remaining chapters, 235 source row/column slices are
reassembled while conserving 44,033 distinct source cells. Original extra
HTML, CSV, SVG, TIFF, TXT and stylesheet source evidence from retired branches
is preserved byte-for-byte under each affected chapter's
`0000-index/retained-source/`. A manifest maps original paths and SHA-256 hashes
to short content-addressed names, keeping portable archive paths within budget.
These are provenance files, not extra tree nodes.
Table headers and identifying columns remain visible while scrolling. Shared
thread schematics are included in each relevant topic rather than requiring a
separate schematic-only folder. Standards acquisition and unresolved numerical
issues remain in the review ledger.

## Technical content and its limits

`tools/data/unit-tables.json` uses the fixed SI defining constants and all 24
prefixes, including the 2022 additions. It corrects obsolete kilogram/ampere
definitions, the litre exponent, viscosity distinctions, specific heat capacity,
unit products and exact electronvolt/astronomical-unit relations. Exact
British/US liquid/US dry measures remain distinct; historical and approximate
quantities are identified. Independent checks cover 49 derived expressions and
exact imperial conversions.

`general-tables.json` covers preferred numbers/sizes, slopes, tapers, inch
conversion and sheets/scales. It corrects transcription, ordering and decimal
errors. Slope/taper equations preserve full/half angles; all 330 integer-inch
and 64 fractional-inch conversions use the exact 25.4 mm definition. Long number
and conversion windows are now continuous tables.

`tolerance-tables.json` retains explicit historical/unverified numerical status.
Old H/K/L selections are not relabelled ISO 22081 values. ISO 286 matrices retain
source contradictions pending complete ISO 286-2 evidence.

`thread-tables.json` covers metric, Whitworth, pipe, trapezoidal and specialist
threads. Independent equations verify 706 basic metric-profile rows; this does
not certify manufacturing limits or classes. The correction ledger preserves
source discrepancies. Whitworth/metric/trapezoidal SVGs are original analytic
figures, with independent angle/rounding checks. Identified copied figures are
removed. Duplicate historical engagement tables await semantic review.

The general-purpose metric reference is now one continuous 392-row table, from
M1 through M300. Its header and identifying column remain fixed while its body
scrolls. The five former range directories and the intermediate metric HTML
landing page are retired. The metric folder uses native directory navigation
for General use, Precision and Tolerances. A fully visible profile diagram and
designation notes retain the explanatory material on the General use page.
Twenty-five rows retain the original first-choice emphasis through color and
weight, with a localized legend; the separate first/second-choice overview is
retained as source evidence in the dataset. Every data row and all 73 retired
files are retained and hashed under the General use `0000-index/retained-source`
folder. Source row/column identities remain available for independent equation
and preservation checks. Printing removes the scrolling limit and repeats the
header on paginated A4 output. All five languages and Light/Dark presentation
use the same values.

Precision metric and Whitworth tables use the same frozen-header scrolling
regions. Profile diagrams remain expanded. The Whitworth radius leader is
radial to its analytic root circle, with the arrow tip on that circle and the
R label aligned with the leader; its generator and independent geometric checks
retain this relationship when the schematic is regenerated.

An additional audit enabled the same frozen-header viewport in 46 existing long
references: three general references, 36 tolerance references and seven thread
references. This includes the inch overview and dimensions, trapezoidal dimensions
and engagement, pipe dimensions, runouts and reliefs. A block with at least 16
data rows is included in this audit; the viewport's existing 65vh maximum only
scrolls when its actual rendered content exceeds that height. Existing shaft,
fastener and remaining-reference frozen viewports are retained. Short topics
remain unchanged. Screen scrolling never limits the printed table.

`shaft-tables.json` preserves historical dimension/accuracy matrices and their
source identities. Independent input is in `shaft-retained-source-tables.json`.
Issued ISO 14/4156/21771/1328 contexts distinguish corrected editions and drafts.
ISO/R 775 is withdrawn; no direct current shaft-end replacement is asserted.
Nominal torsion, DP reciprocal, chord and span calculations are independently
checked, without claiming inspection allowances, fatigue or material design.
Original shaft-end/spline/involute diagrams accompany appropriate tables.

`fastener-tables.json` separates product grades A/B/C from property classes such
as 8.8. These identifiers retain full stops in all five languages, regardless
of ordinary decimal punctuation. Labels remove false grade C from ISO 4762 and identify full fine-thread
hexagon screws with ISO 8676 rather than ISO 8765. Published 2026 references are
distinguished from drafts; ISO 4034/4036/8839 withdrawal is explicit. Numerical
source tables remain unverified against complete current standards. [B] and [F]
retain their meanings. Repeated obsolete requirements become current contextual
references; original evidence remains in `fastener-source-tables.json`.

New nominal tensile stress areas use explicit reviewed thread pitches. The
erroneous old M4/1.75 entry is not propagated: ordinary M4 uses P=0.7 mm and
As approximately 8.779 mm². These areas are not permissible/proof loads. Supplier
button-head numbers remain unverified supplier values, not ISO 7380 certification;
the copied artwork is excluded. Original diagrams precede common product tables.

`remaining-tables.json` presents 54 retained matrices in 25 logical topics, with
independent originals in `remaining-source-tables.json`. One fragmented printed
taper-pin equation is not reconstructed by guessing. Pins have issued ISO
context; national keys/retainers are not silently declared interchangeable.
Historical steel/EN/AISI comparisons are not certified material substitutions.

New fundamentals cover fastener requirements, rolling-bearing life/dimensions,
radial plain-bearing pressure/speed, O-rings, metric ports, DN/PN meaning,
ideal fillet-weld geometry, machining conversions, chamfers, section/mass
relations, elasticity, roughness definitions, chain-wheel pitch geometry and
drawing notation. No catalogue-dependent load factors, cutting conditions,
seal grooves, allowable stresses or material equivalences are invented.
Sharp-corner section assumptions and actual catalogue-area requirements are
explicit. L10 states 90% reliability; dynamic capacity is not allowable load.
DN is a designation, not a measured bore. The external-only redirect and broken
image-only placeholders are replaced by local authored explanations.

Seven empty source categories remain listed in the review ledger: three
retaining-pin categories, three centre-hole categories and the old chamfer-PDF
placeholder. Complete numerical standards and delivery/usage conditions remain
open in other chapters even where useful explanations now exist.

## Ownership and verification

These are informative, unofficial references, not official standards or scans.
The five-language notice excludes accuracy/suitability warranty and limits
liability only where law permits. GPL does not authorize third-party standards
text or artwork. Use independently authored figures and preserve provenance. A legacy 727-page
František Drastík/MONTANEX handbook scan and unreferenced legacy raster/PDF media
are removed from the distributed factory library. Private originals are retained
under ignored build/reference-source-evidence/retired-legacy-media/.
See [the acquisition and unresolved-evidence ledger](REFERENCE_STANDARDS_REVIEW.md).

`generate-reference-tables.py --data tools/data/<name>-tables.json --check`
validates HTML/metadata. Logical sections retain source row/column identities.
Only reviewed hash-matched neutral pages are removed. Unrelated metadata settings
survive; unchanged files are not rewritten. No required runtime sidecar is added.

`render-reference-tables.cjs` blocks remote assets, checks every localized row
count, width and dark contrast, and creates previews/paginated A4 PDFs.
`verify-reference-table-print.py` checks every PDF page for A4 dimensions,
selectable text and printable bounds. Inspect representative continuation pages
and figures visually. PDFs below `build/` are evidence, not product dependencies.

The independent Python checks cover equations, source-cell preservation,
logical sections, localization, generated output and review status. Windows
File Manager checks cover ordinary behavior, directory properties, live
five-language Light/Dark/Light and all 1,685 native Qt labels. Rendering and
printing checks cover all 1,435 localized references, including sticky headers,
first-column retention and horizontal print panels. The dated release record
states the final checks, physical page count and Windows package acceptance.

Shared C++, native formats, OCCT 8.0.0 and protected modeling contracts are
unchanged by this library work. Linux execution requires its own host and is
not claimed here. User configuration, models and custom artwork remain outside
the change and committed-source package.

## Continuous shaft reference tables (2026-10-16)

Solid-shaft torsion now combines its two source blocks into one 30-row table. Fine-spline accuracy combines its two blocks into one 26-row table. Both use fixed headers and a scrolling body in all five languages. Columns are aligned by their original header identity; an em dash marks an absent source field. No engineering values are invented or discarded. Independent cell-by-cell tests, browser scrolling, full print output and native File Manager checks passed.

## Fastener follow-up (2026-10-16)

Bolt load capacity joins all three source blocks into one 61-row, 13-column
table. Distinct historical pitch/diameter combinations remain separate. The two
inconsistent historical 5S columns retain their values and explicit source labels;
they are not certified permissible loads. Counterbore/hole dimensions join three
blocks and transpose to 32 bolt-size rows and 20 parameter columns. The original
project figure is restored from ZIMA-Parts.

The six main hexagon-head bolt/screw families combine their dimensional ranges
with thread sizes down the left and parameter symbols across the top. Length
availability tables remain independently readable on the same page. Differing
historical rows are retained rather than silently overwritten. The ISO 4017 M4
head-height correction, 2.5 to 2.8 mm, is explicitly recorded with its catalog
source in `reviewed_numeric_corrections`; original source evidence is unchanged.

Product-grade A/B/C labels are distinct from property classes such as 8.8.
[The native fastener library](FASTENER_LIBRARY.md) documents the four verified
editable models, material, Relations, source Drawing and native thumbnails.
