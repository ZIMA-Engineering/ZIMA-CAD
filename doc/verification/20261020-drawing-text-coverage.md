# Weighted Drawing text retains every glyph

## Reproduction and correction

The native casting-study Drawing's long note `UNRELEASED STUDY - R2 NOT FINISHED`
lost complete T/H glyphs in raster output. The renderer calibrated physical
stroke weight correctly, but applied its boolean stroke subtraction to one
long path containing many disconnected glyphs. That Qt path operation could
discard entire later glyphs.

The shared renderer now lays out each line with QTextLayout, retains actual
glyph-run positions, kerning, shaping and fallback fonts, and applies the same
calibrated stroke adjustment around each glyph's local origin. Existing text
alignment, scale, rotation/handedness and the native-text output path remain
under their existing conventions. No document format, model geometry or
user-authored text is changed. No new UI string requires catalog changes.

## Windows evidence

The new `zima_cpp_drawing_text_coverage_tests` independently locates nonempty
glyph outlines with Qt shaping and requires actual raster ink in each glyph
box. Four strings cover the original failing note, lower/upper Latin letters
and digits, Czech/German/French accents and Russian. Five text heights
(1.8/2.5/3/5/10 mm) and five pen widths (0.13/0.25/0.35/0.5/0.7 mm) produce
100 cases. The baseline failed on glyph 55 (T), height 3 mm, pen 0.13 mm;
the corrected cases pass. This detects disappearing glyphs, not exact font
outline equivalence or every possible script/rotation combination.

Image, DXF and PDF command contracts, the Drawing GUI contract (including
physical I-stem pen-width calibration), and dimension-layout contracts pass.
The original long-note Drawing was rerendered and visually inspected with all
letters present; the source file's hash is retained. Evidence is under
`build/drawing-text-fixed-review/`, `build/drawing-text-coverage-baseline.log`,
`build/drawing-text-and-metadata-fixed.log` and
`build/drawing-export-and-metadata.log` (the unrelated metadata fixture in that
batch required the subsequent updates below).

The metadata command tests were stale after the generic Feature and save-time
physical-Relations changes. They now select explicit generic Feature drivers,
recognize the owner's read-only thread aliases, configure the angular fixture
as an angle-limited generic Revolution, and isolate authored table order from
the template's derived mass Relation. A division-by-zero physical Relation
must reject Save and retain the previous file. The corrected metadata suite
passes in `build/metadata-current-final.log`; engineering metadata also passes.
No product compatibility adapter or relaxed geometry assertion was added.

Linux execution, exhaustive complex-script coverage and every rotated/flipped
text permutation remain unverified. The implementation is shared C++.

```text
ctest --test-dir build/cpp-windows-release -R "^zima_cpp_(drawing_text_coverage_tests|drawing_image_command_tests|drawing_dxf_command_tests|drawing_pdf_command_tests|drawing_ui_contract|dimension_layout_contract_tests|metadata_command_tests|engineering_metadata_command_tests)$" --output-on-failure
```
