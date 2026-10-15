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
| Threads | 32 | 160 |
| Shafts, splines and gears | 35 | 175 |
| Bolts, screws, nuts and washers | 73 | 365 |
| Remaining chapters | 54 | 270 |
| **Total** | **287** | **1,435** |

The tree has **337 directories / 1,685 localized labels**. This follow-up removes
418 redundant navigation directories from the 755-directory published tree.
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
