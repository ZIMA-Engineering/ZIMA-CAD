# Version-owned technical fonts and Unicode verification inputs

## Rejected candidate and actual cause

Windows candidate 2026102001 passed its native loading and command/GUI checks,
but final visual inspection rejected it before publication. Drawing PNG/PDF
exports from an ordinary project directory contained missing/boxed glyphs and
empty title-block text. The same signed executable and unchanged Drawing
rendered correctly when its process working directory was the archived source.
The runtime already contained the exact committed ISO font.

The shared technical-font loader searched its embedded resource, repository
working directory and the development build's `../../config/fonts` location.
The console executable has no GUI resource bundle. In a versioned installation,
that last path names shared user configuration rather than factory runtime data.

The loader now checks the version-owned `config/fonts` beside a Windows runtime
executable and `../config/fonts` beside a Linux `bin` executable. Embedded GUI
resources retain priority, and existing development/tool fallbacks remain.
The font file, native document schema, model geometry and user font preferences
are unchanged. Registration remains cached once per process.

## Regression and release gates

A standalone Qt probe has no embedded font, repository working directory or
development fallback. It is run in both runtime layouts and must load the
actual bundled family and nonzero Latin/Czech/French/Russian glyph outlines.
Both cases failed before the correction in 0.18 seconds. The corrected cases
and the rebuilt glyph, PNG/JPEG, PDF, DXF, Drawing GUI and dimension-layout
contracts pass: eight checks in 26.21 seconds. These are Windows observations;
the Linux directory layout is simulated, not native Linux execution.

The repository package smoke now renders the first real factory reference
Drawing twice, from an ordinary project and the complete archived source.
Both JPEGs must have the same SHA-256. A blank default sheet had failed to
detect the defect; the real reference differs on the rejected 2001 binary.
The gate runs for both candidate and signed-package smoke. Final acceptance
also visually reviews the original long-note PNG and independently rendered PDF.

Evidence is retained under `build/portable-font-baseline.log`,
`build/portable-font-all-affected-regressions.log`,
`build/portable-font-unicode-local/` and the corresponding release acceptance
directory. A successful export command or valid PDF header alone is not evidence
that its glyphs rendered.

## Unicode GUI verification inputs

The rejected candidate's legacy annotation, symbol-picker, rounded-profile and
fastener GUI helpers mixed Windows local-codepage `path.string()` data with
UTF-8 Qt/environment strings. This affected their fixture/picker arguments,
not ordinary application startup and GUI Open, which passed with Czech names.

After correcting those inputs, the full Unicode run exposed the same narrow
conversion in native template Save. DrawingWindow's Open, Save and PDF dialog
bridges also used that conversion. These paths now reuse the existing UTF-8
native bridge, matching ordinary Part/Assembly saving. Save/Save As operations,
document identity, ownership, transactions and persistence format are unchanged.

Only the affected verification helpers now reuse `document::path_to_utf8()`
and `std::filesystem::u8path()`. Their equations, interactions, expected geometry
and persistence checks are unchanged. The full annotation matrix is repeated
in a Unicode fixture directory; symbol selection and library-source arguments
also use real Unicode paths. All three local cases pass, including native
template saving, symbol Family/authoring, Drawing symbols and PDF/DXF/image
outputs under the Unicode directory. The signed release repeats these paths without
ASCII fixture substitution. Native Sketcher coverage and its finite geometry
limits remain documented in the existing text/rounded-profile verification notes.

No new product UI text requires translation. Native data, factory assets, user
projects, custom libraries and the development launcher are preserved. Qt 6.11.0
and OCCT 8.0.0 remain aligned. Native Linux build, fonts and interaction still
require verification on the Linux host.
