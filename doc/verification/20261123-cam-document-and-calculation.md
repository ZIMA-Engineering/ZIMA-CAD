# CAM document lifecycle and calculation integration

Date: 2026-11-23. Baseline: `572167d55`, Windows x64 Release, OCCT 8.0.1.
This is a native implementation checkpoint, not completion of the CAM UI or a
portable release. The approved scope is recorded in `doc/CAM_BODY_DESIGN.md`.

## Implemented behavior

- A CAM Body stores its machine, tool profiles, ordered manual/facing operations,
  work frame, stock/design/fixture and clamping Sketch references in the native
  Part. Calculated generic programs and exact remaining-stock packets are also
  embedded; no sidecar is required.
- CAM does not enter the design material history. A later setup may use a prior
  setup's calculated remaining stock; forward dependencies and cycles are rejected.
- Definition edits use the existing metadata transaction and immutable calculated
  snapshots. Opening a draft, inspecting freshness, unchanged OK and Undo/Redo do
  not regenerate CAD geometry. Lost placement references retain the last frame.
- Freshness checks traverse the setup chain with operation-local memoization.
  Tool/operation/frame changes, changed native source geometry, clamping input
  changes, missing/suppressed inputs and collisions prevent downstream reuse.
  Retained results remain available for inspection and repair.
- Explicit calculation reads private native solids in the existing placement
  coordinate convention. It compiles the actual generic program and simulates
  continuous cutting/non-cutting swept volumes. The result is published back in
  document coordinates, so a later setup can use a different work frame.
- Successive manual programs require an explicit continuous transfer. Facing
  operations insert identified rapid connections; these connections pass the same
  machine and collision checks as authored motion. There is no tool teleportation.
- Publication requires the same live runtime identity, revision, calculated
  generation, definition and source keys. A late result cannot replace newer work.
  Colliding results may be retained for inspection but cannot supply accepted stock.

The machine definition includes axis capability, per-axis travel limits, maximum
cutting feed, maximum spindle speed, rapid feed and work-coordinate offset. Tool
or operation commands select the actual cutting feed and spindle speed. Controller
postprocessing remains separate. The first motion implementation is 3-axis milling.

## Verification

The following ten CTest targets passed together (18.27 seconds, concurrent with the
localization test; this is a verification duration, not a performance benchmark):

- `zima_cpp_cam_program_tests`
- `zima_cpp_cam_document_tests` (44 assertions)
- `zima_cpp_cam_sweep_tests`
- `zima_cpp_cam_simulation_tests`
- `zima_cpp_cam_operations_tests` (46 assertions)
- `zima_cpp_cam_calculation_tests` (44 assertions)
- `zima_cpp_render_document_tests`, `zima_cpp_render_operations_tests`
- `zima_cpp_fem_document_tests`, `zima_cpp_fem_operations_tests`

The new calculation fixture starts with a 20 x 10 x 6 mm stock and a centered
20 x 10 x 4 mm design. A translated work frame rotated 30 degrees about Z produces
1000 mm3 remaining stock from 1200 mm3, with 200 mm3 removed. A second setup uses
that retained stock and a work frame rotated 180 degrees about X: 800 mm3 remain
and another 200 mm3 are removed. Both passes report no collisions or missing target
material. Independent volume assertions use 1e-6 mm3 as their test bound; calculation
retains the explicit 1e-7 mm linear tolerance.

Native save/reopen preserves the exact remaining-stock packet. Repeating the second
calculation after reopening reproduces its expected volumes. Source packets remain
unchanged, Undo/Redo restores the calculated result, injected cancellation publishes
nothing, and an explicit fixture intersection blocks chained reuse. The initial
test incorrectly assigned derived rather than authored absolute rotation fields;
correcting the fixture exercised the existing placement solver without changing it.

The operations tests additionally change actual source solid dimensions, edit a
tool, suppress a clamping Sketch, lose a placement reference, reject another runtime
identity and reject late publication after edits and Undo. CAD calculation counters
remain unchanged for metadata, inspection and history restoration.

CAM diagnostic strings have Czech, English, German, French and Russian catalog
entries. `zima_cpp_translations_contract` passed in 15.20 seconds. New CAM dialogs
and their language-switching checks are still pending.

## Remaining gates

The CAM command UI, setup/machine/tool/operation editors, selection, NC pane,
stepping and overlays are not complete. Do not expose or deploy this checkpoint as
finished CAM. Before deploying the format extension, complete the agreed start
template GUI checks and affected factory/native-library audit; verify protected
user-document copies according to the existing inventory and backup agreement.
No protected user document or factory native asset was rewritten by these tests.
Linux verification has not been performed.

The normal development launcher still uses the previously verified Fillet build;
its executable SHA-256 remains
`FD192739063610FE0CF7406DA63FF349B79829E6A26BEF68221B5F86CC1BF3B9`.
