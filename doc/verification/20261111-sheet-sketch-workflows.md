# Sheet, Sketch and derived-copy verification on Windows

Date: 2026-11-11

Target release: `2026111101`

Host: Windows x86-64, Qt 6.11.0, OCCT 8.0.1

## Scope

This finite verification covers the changes made after `2026111003`:

- Pattern and Mirror occurrences inherit the current source Part and Body
  appearance without regeneration.
- Sheet Cut resolves an Up-to target from the selected Sketch and accepts a
  valid target on the side opposite the initial direction.
- ordinary subtractive Features retain the exact preceding Body checkpoint, so
  editing a cut on an imported sheet does not rebuild every earlier Flat and
  Bend;
- ordinary Sketches can own embedded reference images with rotation, opacity,
  visibility and selection locking. Images are presentation-only and do not
  enter profiles, constraints or model calculation;
- a tangent relation at an already shared native point does not add a duplicate
  Coincident relation, and dimension value text keeps click priority over an
  overlapping relation glyph;
- the empty-workspace watermark displays the complete `ZIMA-CAD` product name.

The optional Sketch image fields extend current native persistence but do not
change the native format version. Documents without images serialize as before;
factory native assets and start templates therefore require no regeneration.
The protected shared placement implementation and geometry-side meaning are
unchanged.

## Automated regression matrix

The following focused executables passed from the current Windows build:

| Area | Contract | Result |
| --- | --- | --- |
| Derived copies | `zima_cpp_derived_copy_contract_tests` | Passed |
| Appearance | `zima_cpp_appearance_command_tests` | Passed |
| Selection wires | `zima_cpp_source_wire_gpu_tests` | Passed |
| Sketch equations and edits | `zima_cpp_sketcher_contract_tests` | Passed |
| Sketch references | `zima_cpp_sketch_reference_command_tests` | Passed |
| Circle coincidence | `zima_cpp_coincident_reference_circle_tests` | Passed |
| Mirror/tangent persistence | `zima_cpp_sketch_mirror_tangent_tests` | Passed |
| Embedded images | `zima_cpp_template_object_command_tests` | Passed |
| STEP-to-sheet reconstruction | `zima_cpp_sheet_exchange_tests` | Passed |
| Sheet Cut geometry | `zima_cpp_sheet_cut_skin_to_skin_contract` | Passed |
| Localization | `zima_cpp_translations_contract` | Passed |

The GUI matrix also passed:

| Interaction matrix | Result | Elapsed |
| --- | --- | ---: |
| Circle endpoint editing | Passed | 9.44 s |
| Native tangent contact | Passed | 110.10 s |
| Native arc/axis dimensions and repeated edits | Passed | 554.33 s |
| Embedded image creation/editing | Passed | 18.64 s |
| Sketch Mirror interaction | Passed | 29.74 s |
| Common tangent interaction | Passed | 76.41 s |
| Feature-axis external reference | Passed | 11.26 s |
| Sheet Cut opposite-side Up-to | Passed | 6.32 s |

These suites exercise the applicable native/external contact orders, equation
checks, repeated value edits and drags, rejected redundant edits, persistence
and Undo/Redo paths represented by their fixtures. The embedded-image workflow
also verifies that the image remains display-only. Five-language catalog
coverage passed; the changed image labels and validation messages are present
for Czech, English, German, French and Russian. A separate manual visual review
confirmed the watermark and the reconstructed imported sheet. A complete manual
interaction pass in every application language was not repeated.

## Reported models and performance

The user's original files were read-only inputs and were not overwritten.

`17.prtz` was copied to a disposable current-format diagnostic file and converted
from the side face selected by the user. Cold regeneration and reopen passed.
The result contains six Flats and five Bends, including both compound rounded
ears as one Bend each. Its 74 circular openings are native circles rather than
paired semicircular arcs. The requested 1 mm reconstruction produced
335,083.299349 mm3 from the 3 mm source body whose volume was
1,007,258.55036 mm3.

`02.prtz` was likewise copied to a disposable current-format file. Fresh full
calculation and save/reopen passed with 34 boundaries and no calculation errors.
Editing the final Sheet Cut took 484.286 ms. Editing the ordinary subtractive
Feature `Tažení 03` took 988.122 ms while preserving the unchanged history prefix,
output boundary count and Undo state. Full cold recalculation of the diagnostic
copy took approximately 24.2 s; this is not presented as an interactive edit
time.

## Known finite limit

`zima_cpp_model_calculation_tests` retains a pre-existing unrelated failure:
OCCT accepts a ZIMA-only endpoint-face definition that the contract expects it
to reject. The failure was reproduced before this release and is not caused by
the sheet checkpoint, Sketch image, occurrence appearance or UI changes. This
record does not claim a complete repository-wide green test run. Native Linux
compilation and execution remain a separate host gate; Windows and Linux use the
same changed C++ implementation.
