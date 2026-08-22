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
| P0 | End-to-end project fixtures | Open, edit, regenerate, save, close and reopen representative Part, nested Assembly and Drawing projects in C++; compare persisted model and measured geometry with the Python reference. Deterministic open/load fixtures are complete in both directions (Python-produced fixtures load in C++; C++-produced fixtures load in bundled Python). Part, nested Assembly and Drawing fixtures each now have an edit/regenerate/reopen contract test (append a native feature/occurrence/view, save, reopen, verify). `--verify-startup` now also exercises save/close/reopen through the real window for all three document types: Part (existing), a two-level nested Assembly (save, close, reopen, verify the subassembly and its leaf Part occurrence both survive) and Drawing (save, close, reopen, verify the sheet/view tree survives). Remaining gap is broader interactive GUI-driven editing beyond the scripted scenarios already covered |
| P0 | Windows packaged-runtime verification | Build from committed Git data and pass the repository OpenBLAS DLL, archive, SHA-256 and packaged smoke validators described in `WINDOWS_RUNTIME_AND_BUILD.md` |
| P0 | Full integrated GUI regression | Exercise creation/edit/cancel/undo/redo, middle-button double-click OK, nested activation, selection cycling and explicit dependency regeneration through `zima-cad-cpp`. Fixed real destruction-order double frees in `AssemblyWorkspaceWindow` and the Part harness `MainWindow` (Qt only destroys QObject children, e.g. an open Properties dialog, from inside the `QWidget` base destructor, which runs after the derived class's own members; a dialog's `destroyed` handler was reading those already-destroyed members). Explicit destructors now delete the active dialog first, forcing its `destroyed` handler to run while members are still valid; verified crash-free over 30+ repeated `--verify-startup` runs (previously ~25% crash rate). Implemented the previously-missing nested Assembly occurrence activation user action (context-menu "Aktivovat komponentu"/"Aktivovat podsestavu" and "Zpět do sestavy", matching Python's `_activate_assembly_component`/`_return_to_assembly`): `AssemblyWorkspaceWindow::show_component_context_menu` now offers this action, opening the occurrence's source document from disk if not already open and calling the previously write-side-dead `Workspace::activate_occurrence`/`active_occurrence_path_`. `--verify-startup` already runs Box/Extrusion/Assembly/Drawing creation, edit, Cancel, OK, Undo/Redo, rollback-tree activation, save/reopen and a middle-button double-click OK over the embedded viewer, and now also activates/deactivates an inserted Assembly occurrence through the real window. Also added scripted RMB selection-cycling coverage through the real window: switches the selection filter to Face to obtain a screen position with multiple overlapping candidates (Face/Edge/Vertex) near a Box corner, hovers it, walks the full RMB cycle back to the original candidate, confirms the exact cycled-to candidate with LMB, then clears via an empty-space click; verified stable over 100+ combined `--verify-startup` runs with 0 failures and no interference with later scenarios. Also added scripted multi-level (nested-within-nested) Assembly occurrence activation coverage: creates a second, outer Assembly, inserts the already-populated Assembly as one of its own components (a subassembly occurrence), then activates the leaf Part occurrence two levels deep through the same `activate_occurrence_for_test` entry point, verifying the composed instance path and the "Zpět do sestavy" toolbar action; verified stable over 40+ `--verify-startup` runs with 0 failures and full ctest 9/9 still passing. The "Full integrated GUI regression" P0 blocker's scripted-coverage gaps are now closed |
| P1 | Advanced Drawing parity | Verify all Python projection-layout, annotation, BOM, template and title-block editing scenarios against C++ output |
| P1 | File-management parity | Implemented the previously-dead C++ action stubs for rename, delete-current-file, delete-all-versions, delete-old-versions (with and without keep-latest) and working-directory-wide old-version cleanup (with and without keep-latest), matching the Python reference's confirmation-dialog and numeric-suffix (`document.ext.N`) archive conventions. Rename uses the shared in-application `RenameDocumentDialog` (a `PropertiesSubWindow`), validates/defaults the extension, rejects name collisions for both the main file and a companion Drawing (`.prtz`/`.asmz` → `.drwz`), rewrites in-memory Assembly component and Drawing view `source_path` references for currently-open documents, then renames on disk and refreshes tabs. `--verify-startup` now scripts the full workflow through the real window and real `QMessageBox` dialogs (Yes clicked via `QTimer::singleShot` + `QApplication::activeModalWidget()`, since the dialogs are modal): creates versioned archive files, verifies delete-old-versions-keep-latest removes only the older archive, delete-old-versions removes all archives but keeps the current file, rename moves the file on disk, and working-directory-wide keep-latest cleans archives across multiple documents at once; verified stable over 80+ combined `--verify-startup` runs with 0 crashes and full ctest 9/9 still passing. Known remaining gap versus Python: the C++ rename only rewrites references in currently-open documents — it does not scan, load and rewrite on-disk-but-not-open Assembly/Drawing documents that reference the renamed file, unlike Python's `_rename_document_file_to` |
| P1 | Deep Assembly mechanisms | Implemented scripted GUI-level coverage of mate creation, DOF display and mate-dimension drag (mate/DOF math and mate-handle drag were already native, but had zero `--verify-startup` coverage). `--verify-startup` now: reopens the already-open inner Assembly document (switching tabs, since it is never closed to disk in that scenario), inserts a second occurrence of the same Part (a repeated-occurrence fixture), repositions the second occurrence 100 mm along X through the real `ComponentPropertiesDialog` (needed because two coincident occurrences produce indistinguishable overlapping Face candidates for the pixel-scan picker), triggers the plane-mate action, picks the two occurrences' real opposing `z_min`/`z_max` Face candidates in the viewer (disambiguated by `instance_path`, matching the shared `MatePropertiesDialog`/`selectionFilterCombo` "Plochy" contract), sets an offset and lower/upper limits and confirms — verifying the mate appears in the tree under "Vazby" and the mated occurrence's DOF label correctly drops from `[6 DOF]` to `[3 DOF]` (a PlaneCoincident mate removes 1 translational + 2 rotational DOF). It then switches the selection filter back to the default (Dimension candidates are excluded by the Face/Vertex/Axis filters), finds the mate's draggable Dimension handle (`semantic_key` starting `mate:`) and drags it via real mouse events, then reopens the mate's Properties dialog (re-locating the tree item after the drag, since the drag's `refresh_scene()` invalidates the previous `QTreeWidgetItem*`) to verify the dragged offset stayed within its configured `[0, 20]` limits. Verified stable over 30+ combined `--verify-startup` runs with 0 failures and full ctest 9/9 still passing. Known remaining gap versus Python: C++ has no free-component-drag capability at all (`ComponentPropertiesDialog` only exposes plain spinbox translation/rotation fields, unlike Python's draggable `_on_insertion_origin_dragged`); this is a missing feature, not just missing test coverage, and would need to be implemented separately if full parity is required. Angular mate (AxisAngle/PlaneAngle) drag remains unverified at the GUI level, though it is covered by module-level contract tests |
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
