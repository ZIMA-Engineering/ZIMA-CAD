# Sheet Profile multiple targets and Bend corrections

Date: 2026-11-11
Status: focused Windows development verification; publication pending

## Scope

This increment gives each free Sheet Profile width side the shared ordered
multi-target **Up To** table. Profile generation resolves the nearest valid
target at every generated route station. Competing planar targets are affine
along a straight Flat, so their exact pair intersections are included in the
Flat boundary whenever the nearest target changes inside that span.

Every generated Bend also owns its two established end-profile endpoint
corrections. A non-master Bend Properties window exposes only that end Sketch.
All parent-owned placement, route, start, material and corner controls are
disabled. The following Flat consumes the corrected physical end edge.

No native format, protected placement contract, OCCT version or platform-specific
modeling branch changed. Windows and Linux continue to share the same C++ path.

## Focused matrix

| Contract | Result |
| --- | --- |
| Two Up To targets retained on one Profile side | Passed |
| Exact nearest-plane switch inside one Flat | Passed |
| Broken boundary calculates as a valid sheet Body | Passed |
| Target identities and kinds survive native save/reopen | Passed |
| Second generated Bend accepts its own first-end correction | Passed |
| First and second Bend corrections remain independent | Passed |
| Following Flat inherits the corrected Bend edge | Passed |
| Child container and Sketch identities remain stable | Passed |
| Non-end Bend parameter edit is rejected | Passed |
| Generated Flat remains parent-edited | Passed |
| GUI uses the shared multi-target table and exits entry cleanly | Passed |
| GUI restricts a non-master Bend to its end Sketch | Passed |
| GUI correction commit, save/reopen and Undo/Redo | Passed |
| Authored-wire View selection maps to the logical Profile/first-Bend exception | Passed |

The backend contract is `zima_cpp_sheet_profile_tests`; the real application
workflow is `zima_cpp_sheet_profile_ui_contract`. The latter performs actual
Tree and View interaction against the Windows Qt application.

## Executed Windows suites

| Suite | Result |
| --- | --- |
| `zima_cpp_translations_contract` | Passed |
| `zima_cpp_sheet_profile_tests` | Passed |
| `zima_cpp_sheet_profile_ui_contract` | Passed |
| `zima_cpp_bend_attachment_ui_contract` | Passed |
| `zima_cpp_profile_command_tests` | Passed |
| `zima_cpp_flat_command_tests` | Passed |
| `zima_cpp_bend_arc_only_contract` | Passed |
| `zima_cpp_bend_arc_ui_contract` | Passed |
| `zima_cpp_bend_command_tests` | Passed |
| `zima_cpp_feature_up_to_parameters_tests` | Passed |
| `zima_cpp_feature_parameters` | Passed |
| `zima_cpp_sketch_dimension_command_tests` | Passed |
| `zima_cpp_sketch_relation_command_tests` | Passed |

The applicable Sketcher matrix exercises unlocked dimension-value changes,
successive edits, underconstrained and locked point dragging, native and
external-support Profile variants, opposite endpoint/contact sides, equation
checks after movement, invalid-edit rejection without document mutation,
Undo/Redo and native save/reopen. The new target table is exercised through
the actual GUI, including independent inspection and repeated removal. General
Sketch trimming is unchanged and is covered only by its existing contracts;
this increment does not claim a new exhaustive repository-wide Sketcher run.

## Explicit gaps

- Native Linux execution must run on the Linux host.
- Exact internal envelope switches are currently generated for planar targets.
  Curved finite faces still use the existing persisted ray geometry at span
  stations.
- Exact circle, ellipse, spline and mixed analytic Flat target boundaries are
  not implemented by this increment. No display-mesh approximation is treated
  as authored geometry.
- Bend remains one ordinary circular Bend. Only its two existing endpoint
  corrections are independently editable, as requested.

These gaps are stated explicitly so the focused matrix is not interpreted as
universal target-surface or Sketcher coverage.
