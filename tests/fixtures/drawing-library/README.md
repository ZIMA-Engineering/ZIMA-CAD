# Drawing library reference fixtures

These original single-Sketch frames and localized title blocks are reference
inputs for regression tests and the native Family generation tools. They are
not user-selectable factory library entries. The active library contains only
`config/formats/ZE-DRAWING-FRAME.frmz` (A4–A0) and
`config/formats/ZE-TITLE-BLOCK.tblz` (CS, EN, DE, FR, RU).

Keep the fixtures for exact geometry, text, BOM and insertion comparisons.
The company logo is embedded in the title blocks. Tests must use this directory
explicitly; ordinary new-document discovery must use `config/formats`.
