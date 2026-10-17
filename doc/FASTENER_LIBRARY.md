# Native fastener library

The hexagon-head library defines one representative length per supported diameter,
pitch and product-grade combination: 222 ordinary editable Parts under
`config/lib/03-ENGINEERING-TABLES/05-FASTENERS/04-SCREWS/01-HEX-HEAD`.
Models belong directly to their chapter. Reference documents, illustrations and
thumbnails belong in `0000-index`.

| Standard | Geometry | Product grade | Models |
| --- | --- | --- | ---: |
| ISO 4014 | Partial coarse thread | A/B | 34 |
| ISO 4016 | Partial coarse thread | C | 23 |
| ISO 4017 | Full coarse thread | A/B | 34 |
| ISO 4018 | Full coarse thread | C | 23 |
| ISO 8765 | Partial fine thread | A/B | 30 |
| ISO 8676 | Full fine thread | A/B | 30 |
| CSN 02 1201 | Small head, partial thread | Historical precision class | 12 |
| CSN 02 1205 | Small head, partial thread, locking hole | Historical precision class | 12 |
| CSN 02 1207 | Small head, full thread | Historical precision class | 12 |
| CSN 02 1208 | Small head, full thread, locking hole | Historical precision class | 12 |

Every model retains an ordinary Body, native Sketches and editable feature history.
Circle and polygon centers have persisted Origin contacts. Copy the model to a
project before editing it. Save As creates an independent copy and deliberately
leaves the original active; the generator closes it and opens its copy explicitly.
Dimension changes update stock metadata without renaming the file.

## Sizes, grades and representative lengths

ISO coarse A/B covers the table sizes M1.6–M64; grade C starts at M5. Fine-thread
families cover the listed M8–M64 diameter/pitch combinations. Grade A requires
`d <= 24` and `l <= min(10d, 150 mm)`. M16–M24 therefore also have longer B
representatives; M27 and larger use B. Product grade is dimensional accuracy,
separate from strength class. The original ISO 4017 M3x20, M4x20, M5x20 and M6x25
filenames remain unchanged, without an added grade suffix.

Partial lengths are selected from preferred lengths so the complete thread and
runout stop before the underhead fillet. ISO thread length b uses the reviewed
small-size values or `2d+6`, `2d+12`, `2d+25` for lengths up to 125, up to 200
and above 200 mm. The runout allowance is `5P`, using the coarse ISO pitch even
for fine-thread families. Historical small-head variants use their own reviewed
b/runout values and are not relabeled as modern ISO grades. Their M7 size remains
because it appears in the requested table; its historical sector restrictions
are not permission for unrestricted modern use. Sizes absent from the requested
table are not invented. The historical M7 restriction concerns automotive and
aerospace use.

## Geometry and sources

`tools/data/hex-fastener-geometry.json` records nominal dimensions and public
primary preview links. The manifest generator combines those facts with actual
table headers. Corrupt cells are not used without review. The M12 fine-pitch
header and three small-head cells have explicit corrections recorded in
`tools/data/fastener-tables.json`; the captured source dataset remains intact.

Models use nominal across-flats/head-height values and the reviewed minimum
underhead radius. A/B bearing-face dimensions are chosen inside reviewed limits.
Grade C omits the optional raised bearing face, as agreed. Historical small heads
also omit it, use a 15-degree head chamfer and 30-degree tip, and have real
transverse locking holes where specified. The hole center lies at half the head
height; the XZ frame's local positive Y points toward world negative Z.

ISO chamfered ends use a 45-degree slope and an axial/radial distance of `0.7P`.
The end diameter lies below the basic external-thread root diameter; the angle
is below 50 degrees and incomplete end length below `2P`. This is a nominal
modeling choice, not a standard-mandated exact chamfer distance.
[ISO 4753:2011, Figure 1](https://cdn.standards.iteh.ai/samples/55928/38ddde12863b4c2db35dc929fd502a4d/ISO-4753-2011.pdf)
provides the end-form requirements.

Threads use the existing native cosmetic feature and exact catalog designation,
not a new helical solid. Full threads use Up to the plane below the head with
runout disabled; partial threads use numerical length and reviewed runout.
Reference Drawings have independent native Parts and sibling-relative source
paths. No required external geometry/cache sidecar is introduced.

The public previews do not expose every page of every complete standard.
Large-size nominal geometry is shared with the reviewed ISO 4017 tables. This
is not a certified complete tolerance/manufacturing audit, a set of separate
tolerance-limit solids, or every available commercial length.

## Names, metadata and artwork

Filenames use lowercase ASCII x, for example `HHB-ISO4014-M12x60-A.prtz` and
`HHS-ISO8676-M10x1.25x40-A.prtz`. Product filenames are unique ignoring case.
Native BOM fields include five-language name, standard, dimension-derived stock,
material, product_grade, quantity=- and mass. Fine stock includes pitch. Relations
bind shaft diameter and extrusion length, rather than parsing filenames. Kilogram
masses use six decimal places so small screws do not round to zero; dimension
annotations retain their independent presentation precision.

A/B representatives use Steel-8.8; C uses Steel-4.6. Historical models use 8.8 as
a modern representative choice, not certification of obsolete mechanical classes.
Both records use representative steel density 7850 kg/m³ without invented alloy
composition or FEA properties. Coating is not substituted for steel.
Above M39, the grade C standards require agreement on mechanical properties;
the stored 4.6 material remains a representative choice rather than evidence
that such an agreement or manufacturing certification exists.

Every product has a native isometric thumbnail. Each standard also has a native
reference Drawing and rendered illustration with l, k, d and s attached to real
geometry. Enlarged explanatory schematics remain separate from actual-model views.

## Reproduction and acceptance

The Python tools orchestrate the shared native CLI and do not rewrite native
models as text:

1. `tools/prepare-fastener-manifest.py` prepares reviewed inputs.
2. `tools/generate-fastener-library.py` calculates, saves and reopens candidates.
3. `zima_cpp_fastener_library_contract_tests` checks solids, nominal geometry,
   bearing choices, fillets, locking holes and thread limits independently.
4. `tools/verify-expanded-fastener-library.py` checks three successive lengths,
   Undo/Redo, source identity, stock/mass, save/reopen and exact geometry per edit.
5. `tools/render-fastener-library.py` and `tools/render-fastener-reference-drawings.py`
   create native-derived artwork.
6. `tools/install-fastener-library.py` requires complete matching verification
   records and every required image before installing factory candidates.

Disposable candidates/evidence stay under build. The original four-model
`tools/verify-fastener-library.py` remains a focused regression. See the
[finite verification report](verification/20261016-fasteners-and-reference-layout.md)
for results and remaining gaps. Packaged Windows acceptance is separate. Linux
execution requires the Linux host and remains unverified for this update.
