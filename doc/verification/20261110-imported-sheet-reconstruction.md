# Imported sheet reconstruction and dependent operations — 2026-11-10

## Scope

This finite verification covers the reported Body-to-Sheet conversion of an
imported STEP sheet, the first native Bend created on the converted result,
compound rounded end ears, circular openings, consecutive Sheet Profile bends,
single active Up To branches and complete occurrence selection wires.

The implementation remains shared C++ for Windows and Linux. It advances the
native Part format from 50 to 51 and the Part JSON contract from 74 to 75 for
associative Sheet-from-Body bindings. It does not change the protected shared
container-placement contract, geometry-side meaning, calculation precision,
Qt 6.11.0 or OCCT 8.0.1.

## Root cause and correction

The apparent first-Bend initialization dependency was not a cache dependency.
The imported STEP boundary represented each circular opening as two complementary
semicircular edges. Re-authoring both halves as separate Sketch arcs could create
an `UnorientableShape` during the first following Boolean fuse. The conversion
now recognizes complementary analytic circular boundaries and authors one native
Sketch circle. Non-complementary curved boundaries remain native arcs.

Finished imported sheets are rebuilt branch by branch with explicit native
Flat/Bend attachment references and endpoint anchors. A conservative imported-
only recognizer also accepts the reported Pro/E-style rounded ear represented by
two coaxial thickness-separated cylinders, and reconstructs each ear as one
native Bend followed by its planar Flat. An already represented ordinary Bend is
excluded from this recovery path.

The reported `17.prtz` source was copied read-only to an ignored build fixture;
the original project file was not modified. Conversion from the requested side
created eleven native features with no skipped branch, including the two end ears
as one Bend each and 74 true Sketch circles. Cold regeneration passed. The source
volume was `1007258.55036 mm3` and the reconstructed volume was
`1007303.05730 mm3`; the `44.50694 mm3` (`0.0044%`) difference is the expected
replacement of the original compound two-radius ears by the requested single-
Bend representation.

Sheet Profile now carries the material-side thickness offset across consecutive
Bends and applies only the offset change at the next turn. This removes the
reported terminal gap without changing isolated Bend geometry. A single active
external Plane Up To target is covered for both logical branches and reversed
sides. The existing calculation already resolved those cases, so no duplicate
direction implementation was introduced.

Whole Assembly occurrence hover and selection now reuse the existing persisted
edge buffer through occlusion, like ordinary whole-feature selection. The cache
key distinguishes an occurrence from a local Body at the same instance path;
faces remain unfilled and sibling occurrences are not highlighted.

Selecting an authored sheet feature while an Unbend/Bend Back state is shown
now reveals two independent wire presentations: the existing authored preview
and the corresponding calculated sheet-state edges. The calculated wire reuses
the already uploaded display-line ranges; it is not added to the authored
preview packet, does not fill faces and does not calculate or upload geometry
during hover, selection, inspection or navigation.

The Sketch endpoint regression was caused by an unrelated Parallel inference
winning before the direct H/V relation of a new segment whose second endpoint
was constrained to an external support. The preview and confirmation paths now
prefer that direct segment H/V relation while retaining the independent endpoint
contact. This does not replace a requested endpoint Perpendicular relation.

Native-solid Sheet-from-Body conversion now persists stable source face/edge
roles, generated child bindings and source surface signatures. Planar dimension
and cylindrical radius edits regenerate the dependent conversion while retaining
Flat/Bend and owned-Sketch identities. The calculation reuses the valid source
prefix, truncates at the conversion marker and republishes current boundary
fingerprints; a missing required source face retains the last valid result and
marks the association unresolved. Imported STEP reconstruction remains an
ordinary independent Flat/Bend sequence.

## Verification matrix

| Contract | Result | Coverage |
| --- | --- | --- |
| `zima_cpp_sheet_exchange_tests` | Passed | STEP round trip, one true circle, branch order, reversed endpoint anchor, first following Bend, native associative planar/radius edits, stable child/Sketch IDs, unchanged regeneration, last-valid source loss, current boundary fingerprints, cold save/reopen and two-level Undo/Redo |
| `zima_cpp_sheet_exchange_ui_contract` | Passed | Sheet conversion command interaction |
| `zima_cpp_sheet_profile_tests` | Passed | Three-Bend chain and exact carried-offset edge gaps |
| `zima_cpp_sheet_profile_ui_contract` | Passed | Sheet Profile creation/edit interaction |
| `zima_cpp_feature_side_reversal` | Passed | Single external Plane Up To on both logical branches and reversed sides, plus the existing direction matrix |
| `zima_cpp_source_wire_gpu_tests` | Passed | Occluded occurrence hover/click plus authored/developed sheet-state correspondence; sibling isolation, navigation-cache reuse and zero selected face triangles |
| `zima_cpp_bend_command_tests` | Passed | Bend geometry, eight attachment edges, both endpoints, angles, state, persistence, missing references and Undo/Redo |
| `zima_cpp_sketcher_contract_tests` | Passed | Circle/external-reference tangency and endpoint contact with direct H/V; both selection orders and contact sides, dimensions, repeated edits/drags and reopen |
| `zima_cpp_sketch_external_constraints_ui_contract` | Passed | All 39 mouse-driven scenarios, including external line/arc tangency, endpoint P/H/V, save/reopen and Undo/Redo |
| `zima_cpp_sheet_state_ui_contract` | Passed | Part and repeated Assembly occurrences, common picking/filter, toggle, middle-button confirmation, Cancel and OK |
| `zima_cpp_translations_contract` | Passed | All five catalogs; no user-visible text was added or changed |
| `zima_cpp_start_templates_mm_ui_contract` | Passed | Fresh document from the regenerated millimetre Part template, active editable Body and normal command availability |

Part format migration recalculated and reopened all 1,058 repository-owned Part
assets (1,046 `.prtz` and 12 `.frmz`/`.symz`/`.tblz`) through the changed native
implementation. The final report contains 5,477 calculated boundaries, no
errors, exact readback and no output-hash mismatch. All scanned assets report
Part format 51; the corresponding start templates are included.

The Sketch external-constraint GUI matrix passed its actual mouse-driven circle
tangency scenarios against an external line and external arc. The formerly
failing endpoint P/H/V scenario 6 now also passes together with the adjacent
line-segment and direction-selection-order subsets.

The complete repository-wide regression matrix is not claimed green. Native
Linux compilation and execution remain a separate host gate.
