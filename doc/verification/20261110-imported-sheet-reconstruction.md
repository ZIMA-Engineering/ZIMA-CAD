# Imported sheet reconstruction and dependent operations — 2026-11-10

## Scope

This finite verification covers the reported Body-to-Sheet conversion of an
imported STEP sheet, the first native Bend created on the converted result,
compound rounded end ears, circular openings, consecutive Sheet Profile bends,
single active Up To branches and complete occurrence selection wires.

The implementation remains shared C++ for Windows and Linux. It does not change
the native document format, the protected shared container-placement contract,
geometry-side meaning, calculation precision, Qt 6.11.0 or OCCT 8.0.1.

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

## Verification matrix

| Contract | Result | Coverage |
| --- | --- | --- |
| `zima_cpp_sheet_exchange_tests` | Passed | STEP round trip, one true circle, branch order, reversed endpoint anchor, first following Bend, cold calculation, native save/reopen and Undo/Redo |
| `zima_cpp_sheet_exchange_ui_contract` | Passed | Sheet conversion command interaction |
| `zima_cpp_sheet_profile_tests` | Passed | Three-Bend chain and exact carried-offset edge gaps |
| `zima_cpp_sheet_profile_ui_contract` | Passed | Sheet Profile creation/edit interaction |
| `zima_cpp_feature_side_reversal` | Passed | Single external Plane Up To on both logical branches and reversed sides, plus the existing direction matrix |
| `zima_cpp_source_wire_gpu_tests` | Passed | Occluded occurrence hover/click in No Hidden Edges, Shaded With Edges and Shaded; sibling isolation and zero selected face triangles |
| `zima_cpp_bend_command_tests` | Passed | Bend geometry, eight attachment edges, both endpoints, angles, state, persistence, missing references and Undo/Redo |
| `zima_cpp_sketcher_contract_tests` | Passed | Circle/external-reference tangency, both selection orders and contact sides, dimensions, repeated edits/drags and reopen |
| `zima_cpp_translations_contract` | Passed | All five catalogs; no user-visible text was added or changed |

The Sketch external-constraint GUI matrix passed its actual mouse-driven circle
tangency scenarios against an external line and external arc. Its later unrelated
scenario 6 still reports the pre-existing endpoint P/H/V relation failure; that
suite is therefore not claimed completely green.

The complete repository-wide regression matrix is not claimed green. Native
Linux compilation and execution remain a separate host gate.
