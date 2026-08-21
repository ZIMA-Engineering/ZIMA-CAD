# C++ parity audit

Audit date: 2026-08-19

This audit compares the integrated `zima-cad-cpp` target with the Python
application that remains the behavioural reference. It is a cutover gate, not
a percentage estimate. A row is complete only when the same user scenario is
covered by a C++ contract test and no Python-only model path is required.

## Result

The C++ application is not ready to replace the `zima-cad` launcher. Its core
Part, Sketch, Assembly occurrence, mate, Drawing, viewer-selection and explicit
regeneration contracts are native and testable, but the Python application
still owns material user workflows and release/runtime coverage listed below.

The persistence parity slice now has deterministic Python-produced Part,
nested Assembly, and Drawing fixtures, plus Python adapters for C++-produced
Sketch, Assembly occurrence/mate/cut, and Drawing sheet/view payloads. These
adapters are tested in isolation; they do not yet prove complete semantic
round-trips for every advanced Assembly and Drawing operation.

A cross-language persistence contract now also runs in the other direction:
a native `cross_language_persistence_emitter` writes C++-produced Part,
nested Assembly, and Drawing documents that the bundled Python runtime loads
and validates (`tests/test_cpp_python_persistence.py`). This closed two real
defects in the C++ INI writer — `combine_mode` was serialized as
`"add"/"subtract"` instead of Python's `CombineMode` enum values, and Sketch
and Construction entities reused their owning Container's id for their own
Entity section, which made Python's tree loader recurse into itself
(`RecursionError`) instead of terminating at the leaf entity. Each headless
contract test (`zima_cpp_contract_tests`, `zima_cpp_assembly_contract_tests`,
`zima_cpp_drawing_contract_tests`) now also edits its Python-produced fixture
(appends a native Box feature, Part occurrence, or Drawing view
respectively), regenerates it through OCCT where applicable, saves, and
reopens it — proving edit/regenerate/reopen survives a cross-language round
trip for all three document types. Interactive GUI-driven editing of these
same workflows remains unverified (see the Full integrated GUI regression
row below).

## Verified native vertical slices

| Area | C++ state | Evidence |
|---|---|---|
| Part history | Native | Revisioned DocumentSession, persisted calculated boundaries, rollback, explicit Regenerate and OCCT contract tests |
| Primitive solids | Native | Box, Cylinder, Sphere, Cone, Pyramid and Wedge with Add/Subtract and stable original references |
| Sketch profiles | Native | Exact line, circle, arc, ellipse, elliptical-arc and B-spline loops; multiple regions; nested holes; semantic Text; mixed Text/ordinary profiles |
| Modeling features | Native | Extrusion, Revolution, Fillet and Chamfer with Properties rollback and history fingerprints |
| Viewer selection | Native | One ordered candidate list for hover, confirmation and RMB cycling; exact instance paths and original-reference ownership |
| Assembly ownership | Native | Immediate-owner placement, nested snapshots, exact occurrence activation, dependency-cycle rejection and explicit recursive regeneration |
| Assembly mates | Native core | Plane, axis, point and angular mates; transactional editing; conflict rollback; numerical constraint-rank DOF diagnosis |
| Drawing foundation | Native | Sheets, persisted projections, dimensions, templates/title blocks and explicit view regeneration |
| STEP/DXF foundation | Native | Current-format import/export adapters and contract tests; OCCT remains behind explicit calculation/import boundaries |

## Cutover blockers

| Priority | Missing parity | Acceptance gate |
|---|---|---|
| P0 | End-to-end project fixtures | Open, edit, regenerate, save, close and reopen representative Part, nested Assembly and Drawing projects in C++; compare persisted model and measured geometry with the Python reference. Deterministic open/load fixtures are complete in both directions (Python-produced fixtures load in C++; C++-produced fixtures load in bundled Python). Part, nested Assembly and Drawing fixtures each now have an edit/regenerate/reopen contract test (append a native feature/occurrence/view, save, reopen, verify). Remaining gap is full interactive GUI-driven editing, not headless contract coverage |
| P0 | Windows packaged-runtime verification | Build from committed Git data and pass the repository OpenBLAS DLL, archive, SHA-256 and packaged smoke validators described in `WINDOWS_RUNTIME_AND_BUILD.md` |
| P0 | Full integrated GUI regression | Exercise creation/edit/cancel/undo/redo, middle-button double-click OK, nested activation, selection cycling and explicit dependency regeneration through `zima-cad-cpp`. Fixed a real destruction-order double free in `AssemblyWorkspaceWindow` (Qt only destroys QObject children, e.g. an open Properties dialog, from inside the `QWidget` base destructor, which runs after the derived class's own members; a dialog's `destroyed` handler was reading those already-destroyed members). An explicit destructor now deletes `properties_dialog_` first, forcing its `destroyed` handler to run while members are still valid; verified crash-free over 30 repeated `--verify-startup` runs (previously ~25% crash rate). Scripted creation/edit/cancel/undo/redo/selection-cycling/regeneration scenarios remain to be built out |
| P1 | Advanced Drawing parity | Verify all Python projection-layout, annotation, BOM, template and title-block editing scenarios against C++ output |
| P1 | File-management parity | Decide and test the C++ contract for Python workflows such as rename, version cleanup and working-directory maintenance; do not silently omit destructive actions at cutover |
| P1 | Deep Assembly mechanisms | Practical nested/repeated-occurrence fixtures, angular drag and free-DOF drag remain unverified even though mate calculation and DOF rank are native |
| P1 | Performance gate | Record clean/incremental build, startup, large-assembly scene, picking and explicit-regeneration benchmarks on fixed fixtures |
| P2 | Secondary Python utilities | Audit animation, material/catalog and remaining convenience tools; either migrate them or explicitly remove them from the cutover scope |

## Profile identity gate

Profile calculation now separates engineering intent from geometric
representation. Sketch entities retain their stable IDs; each calculated
profile region and boundary carries a deterministic identity through the
kernel request, history fingerprint and generated side-face reference. OCCT
edge order is not the identity. Polygonal sampling is used only for
containment/intersection validation of mixed analytical loops; the actual
kernel request retains its exact circle, ellipse, arc and B-spline data.

## Assembly mechanism gate

`remaining_degrees_of_freedom()` now computes the numerical rank of the local
persisted mate equations for the exact dependent occurrence. Redundant mates
therefore do not falsely remove additional degrees of freedom. The tree shows
the resulting DOF count for immediate live components. This is diagnostic; it
does not claim that arbitrary free dragging or a global nonlinear multi-mate
solver has reached practical parity.

## Cutover decision

Keep `zima-cad` pointed at the Python application. Re-run this audit only after
all P0 rows pass from a clean committed tree. Cutover requires evidence from
the same fixtures on Linux and the validated portable Windows runtime; source
coverage or successful compilation alone is not sufficient.
