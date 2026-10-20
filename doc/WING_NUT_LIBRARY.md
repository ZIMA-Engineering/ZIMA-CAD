# Historical wing-nut studies

## Publication status

The four cast M12/M16/M20/M24 models are **unfinished studies**, stored under
`build/wing-nut-cast-studies-v1/`. None is installed in the factory library.
Their five localized model names explicitly say "study". The generator refuses
factory destinations and collisions with existing factory model filenames.
No factory data, native schema or user library has been converted or overwritten.

Publication is blocked by the missing accepted perimeter R2 rounding,
the M16 source-radius discrepancy and
remaining shape/mass review. The six smaller pressed variants are now installed
and separately verified; see [Pressed wing nuts](PRESSED_WING_NUT_LIBRARY.md).
The native BRep/constraint checks below do not establish manufacturing conformity.

## Source identification and radius mapping

The archived table is `05-NUTS/09-WING` in
`tools/data/fastener-source-tables.json`; its source SHA-256 is
`43eb7b7f1705f6d3762c6a96ae51bb99f07b3f9e49a5768ff701f046e7d4da27`.
Its example incorrectly repeats the knurled-nut identifier CSN 02 1461.
The [official UNMZ bulletin, printed page 41](https://www.unmz.cz/wp-content/uploads/Vestnik_2002_05.pdf)
identifies wing nuts as CSN 02 1665. This is not evidence of interchangeability
with DIN 315; retain the archived numeric data rather than substitute another
standard's dimensions.

The original local figure was inspected in the retired reference archive under
`build/reference-source-evidence/retired-legacy-media/05-FASTENERS/05-NUTS/09-WING/0000-index/01.pdf`.
The historical [STN excerpt, PDF/printed page 16](https://cloud6.edupage.org/cloud/Vyber_noriem_PDF.pdf?z:LUPeiSrXT3T/KEbijWTB1cDo8rhSxS69cRvC%2Band2TKNbHFceQEhSDQH2eAjAm4J=)
helps distinguish the radius roles. The downloaded file is
`build/wing-nut-sources/stn-excerpt.pdf`, SHA-256
`2cb9bedf561767703019d9db6df7793aaf387ca74c1becde211a63ccd3d5beee`.
It is a historical facsimile reference, not a complete current tolerance source.

| Archived column | Cast drawing label | Meaning in the study |
| --- | --- | --- |
| R1 | R6 | Crown circle in the front outline |
| R2 | R7 | Small perimeter edge rounding; unresolved |
| R3 | R9 | Large hub/wing root rounding in plan |
| R4 | R8 | Small front-outline root corner |

For M16, the archived R4 value is **1.6 mm**, whereas the STN excerpt's R8 is
**1.2 mm**. The study retains the archived 1.6 mm and records the disagreement;
no normative correction has been approved. R3 must not be substituted for R4.

## Authored history and physical properties

`tools/generate-wing-nut-library.py` uses ordinary native Part/Body commands:
a constrained front wing outline with a native corner radius, continuous
extrusion, circular Pattern, constrained thickness cutter and its Pattern,
revolved tapered hub, metric threaded opening and accepted root Fillet.
All source profiles retain native geometry and constraints. The taper cutter
extends beyond the crown rather than ending exactly on its tangent point.
Relations connect dependent dimensions, publish `mass = model.mass` and derive
stock from the actual thread designation/pitch.

Each model is explicitly regenerated, saved, reopened, given its Relations,
regenerated again, saved and reopened. Assumed steel density is
7.85e-6 kg/mm³; no steel grade or manufacturing tolerance is certified.

| Study | Saved mass (kg) | Archived indicative mass (g) |
| --- | ---: | ---: |
| M12 | 0.065015 | 60 |
| M16 | 0.094349 | 90 |
| M20 | 0.189000 | 180 |
| M24 | 0.377912 | 260 |

These are calculated masses of the current study solids, not copied catalog
weights. In particular, the M24 difference requires further contour/material
review; it has not been attributed to the omitted R2 alone or forced to match
the table by changing density.

The independent `--wing-studies` gate checks one connected valid BRep, actual
bounds, adaptive volume against the current physical cache, saved mass against
volume times density, the bore/crown/front-root cylinders, and repeated
locked/unlocked Sketch dragging on private copies. All four baseline studies
passed. Inspection must leave source documents clean and without Undo entries.
This gate does not independently certify the 3D R3 blend's curvature or the
entire casting contour.

## Edit matrix and rejected experiments

`tools/verify-wing-nut-studies.py` works on private copies and records failures
instead of silently skipping a driver. The final matrix is
`build/wing-nut-cast-edit-matrix-v7/results.json`:

- All forty driver cases passed: base/top hub diameters, hub height, span,
  overall height, crown radius, front root R4, root blend R3, root thickness
  and tip thickness for all four sizes. Each performs 102%, 104%, 100%,
  Undo/Redo, Regenerate, native save/reopen and independent geometry/mass
  checks: **120 successive successful edits**.

The thickness cases exposed and now verify the bounded
[coordinate-incidence correction](verification/20261019-sketch-coordinate-incidence.md).
The subsequent [rounded tangent and Relations correction](verification/20261020-rounded-tangent-wing.md)
resolves the retained tangent-chain and M24 target-order failures. The final
native matrix covers 1,792 rounded outlines, 512 finite sloping-line controls
and 16 Part/Assembly Relations cases; GUI controls exercise repeated real edits
and dragging. This does not resolve the manufacturing-contour questions above.

Constant perimeter Fillets were tested alone, in reversed feature order and
jointly with root radii using OCCT's existing API. Root-only succeeds; the
perimeter/joint operations do not produce an accepted valid solid. Equal-end
linear-radius probes did not establish another accepted path. No tolerance,
requested radius or OCCT version was relaxed to make a test pass.
`fillet.create` accepting a history definition is not proof of calculated
geometry: check `history.list.calculation_errors` and explicit Regenerate.
The generator's explicit `--edge-rounding complete` mode remains a failed probe.

A native profile-loft root-pad experiment regenerated but changed the casting
footprint. It is rejected as a substitute for the intended spatial root blend
and must not become factory geometry or artwork.

## Review artwork and remaining gates

`tools/render-wing-nut-studies.py` renders four thumbnails and an M12 native
Drawing. It uses three associative views, axes and eight measured dimensions
(span, overall/hub height, crown/front-root radii, both hub diameters and nominal
thread diameter). Dimensions must retain their values and identities through
Regenerate and save/reopen. The review tool uses the existing native factory
frame/title-block Family variants explicitly; the base title-block Family alone
contains its common geometry and is not a complete language variant.
All source hashes must remain unchanged while rendering.

The Drawing is labeled "UNRELEASED STUDY - R2 OPEN". Its title block
publishes 0.065015 kg, ZIMA-Engineering and the review date from its own native
Save As dependency copy. The original study hashes remain unchanged. The native
geometry/mass checker also verifies this independently copied Part. Final
receipts and images are in `build/wing-nut-cast-study-art-v9/`.
This remains a local review artifact. Factory installation additionally requires the complete
editable model matrix, reviewed source dimensions and contour, accepted R2,
representative final-model GUI checks and packaged-native verification.
Windows-only results do not establish Linux support. No release has been
created from these studies.

Raster export omitted glyphs from longer free-text review notes during
inspection. The shared drawing text renderer now applies calibrated stroke
adjustments per shaped glyph rather than subtracting a stroke from an entire
multi-glyph path. See [Drawing text coverage](verification/20261020-drawing-text-coverage.md).

```text
python tools/generate-wing-nut-library.py --stage build/wing-studies-new
build/cpp-windows-release/fastener-runtime/zima_cpp_fastener_library_contract_tests.exe --wing-studies build/wing-studies-new
python tools/verify-wing-nut-studies.py --models build/wing-studies-new --output build/wing-edit-check-new
python tools/render-wing-nut-studies.py --models build/wing-studies-new --output build/wing-art-new
```

Use fresh output directories. The edit check intentionally returns failure
while any recorded driver is unresolved; artwork is not a publication gate.
