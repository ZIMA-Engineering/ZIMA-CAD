# Converted Bend start corrections and website contact

Baseline: `8e29beba40b093908261801e827696d3071f3104`. Native Windows,
MSVC Release, Qt 6.11.0 and OCCT 8.0.1. Linux was not executed.

## Behavior and ownership

Sheet from Body Bends expose the existing first-Sketch endpoint offset
dimensions during ordinary double-click inspection. Single selection still
does not expose dimensions. The same inline editor, signed distances and
native Sketch equations apply as for ordinary attached Bends. The second
Sketch continues to use its established correction mechanism.

When the source or conversion definition changes, reconstruction now retains
the first Sketch's native point/segment identities and authored equations,
while updating its exact source endpoint references and resolved frame from
the reconstructed region. The shared placement solver is unchanged. This
requires no native format change, sidecar, or factory conversion.

## Verification

| Check | Result |
| --- | --- |
| Native Sheet Exchange suite | Passed, 81.37 s |
| Existing native Bend suite | Passed, 63.10 s |
| Copied user box, actual GUI | Passed, 230.875 s |
| Website generation and reference validation | Passed: 806 pages, 17,096 local references, five languages |
| Footer browser inspection | Passed at normal desktop size and 390 px width; correct `mailto` target |

The new native cells exercise both first-Sketch ends with successive positive,
negative, positive-zero and negative-zero inputs. They check endpoint equations,
read-only external sources, unlocked driving/reference drags, locked driving
drags and serialized sign/solution-side preservation. Combined first- and
second-Sketch corrections survive successive source-radius changes, stable
point/segment identities, native reopening and cold calculation. Existing Bend
coverage remains responsible for its broader attachment and end-constraint
matrix; this is not a new exhaustive Sketcher solver certification.

The actual GUI fixture is an isolated copy of the previously tested complex
RIP box. Both first-Sketch corrections are selected by actual mouse events at
their dimension labels. Positive/negative/zero edits are checked against exact
source endpoint equations, save, Undo and Redo. The existing conversion, Flat
correction, cancellation, localization and DXF checks also pass. An attempted
older 2026-11-19 fixture was rejected as an unsupported native format; it was
not migrated or used as passing evidence. The current user-box copy supplies
the GUI evidence instead.

Artifacts: `build/bend-first-correction/native-matrix.log`, `native.log`,
`gui/results.json` and `gui/user-box/`. Initial test assertions were corrected
to respect the existing unlocked-dimension drag behavior and signed magnitude
input convention; no product solver behavior was changed to satisfy them.

## Localization and website

No new CAD UI strings are introduced. The Bend guide explains first- and
second-Sketch corrections in all five languages. The common website footer
adds the explicitly requested public contact email with a localized label.
Existing localized native images are retained. Build outputs and runtime
public-file inventory are checked together. Deployment and Windows publication
are separate final acceptance steps.
