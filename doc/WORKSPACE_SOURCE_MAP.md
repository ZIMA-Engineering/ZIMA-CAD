# Main-window source map

## Split on 2026-09-11

Originally `cpp/app/assembly_workspace_window.cpp` had 29,076 lines. After the
split it contains 52: the main-window constructor and destructor. Its 299 methods,
shared helpers and local widgets were distributed across 39 separately compiled
`.cpp` files in `cpp/app/workspace`, plus the original window-lifecycle file.
Class declarations and data members remain in `cpp/app/assembly_workspace_window.hpp`.

This was the first stage: clearer source organization with unchanged behavior.
The split itself introduced no command API, separate process or GUI-free execution.
Operations initially remained methods of the same class and therefore retained
dependence on window state, dialogs and the viewer.

## Where to make changes

All files below are in [`cpp/app/workspace`](../cpp/app/workspace).

| Area | Files |
| --- | --- |
| Console and shared commands | GUI adapter `console.cpp`; execution and data tree in `modules/command_host`; see [CAD console](CAD_CONSOLE.md) |
| Actions, menus and event connections | `actions.cpp`, `layout.cpp`, `toolbars.cpp` |
| Documents, tabs, document-kind switching | `documents.cpp` |
| Parameters, material, relations, family table and settings | `document_commands.cpp` |
| Saving, renaming, file versions, operation state and application settings | `file_operations.cpp` |
| Import and export | `interchange.cpp` |
| Explicit-calculation adapters and editing context | `calculation.cpp`; calculations in `modules/workspace/model_calculation` |
| Component insertion and Assembly regeneration | `assembly_commands.cpp` |
| Component Properties and selection | `assembly_properties.cpp` |
| Component dragging | `assembly_drag.cpp` |
| Body and Boolean Properties | `body_properties.cpp` |
| Part history, reordering, suppression and deletion | `part_history.cpp` |
| Primitive Properties | `primitive_properties.cpp` |
| Sweep Properties and path profiles | `sweep_properties.cpp` |
| Construction objects, points and axes | `construction_properties.cpp` |
| Reference selection and temporary Origin visibility | `reference_selection.cpp` |
| Fillet, Chamfer, Shell and Extrusion target faces | `edge_treatment.cpp` |
| Parametric dimension display and layout | `dimension_display.cpp` |
| Direct View dimension editing | `dimension_edit.cpp` |
| View rotation and orientation dialog | `orientation.cpp` |
| Keys, ray conversion into the active occurrence, Undo/Redo | `view_events.cpp` |
| Scene refresh and selection contract | `scene.cpp` |
| Document, history and sketch tree | `tree.cpp` |
| Context menus, activation and opening component sources | `context_menu.cpp` |
| Sketch, spline and text Properties | `sketch_properties.cpp` |
| Active sketch, working copies, external references and finishing edits | `sketch_document.cpp` |
| Starting/ending Sketcher tools, trim and mirror | `sketch_tools.cpp` |
| Point snapping and geometric inference | `sketch_snapping.cpp` |
| Sketch geometry constraints | `sketch_constraints.cpp` |
| Dragging sketch points, references and dimensions | `sketch_drag.cpp` |
| Deleting geometry, construction mode and constraint removal | `sketch_edit.cpp` |
| Sketch dimension creation and Properties | `sketch_dimensions.cpp` |
| Interactive geometry creation and previews | `sketch_preview.cpp` |
| Offset Properties and preview | `sketch_offset.cpp` |
| Shared helpers | `geometry_helpers.cpp`, `tree_helpers.cpp`, `sketch_helpers.cpp`, `edge_preview_helpers.cpp` |

`layout.cpp` and `scene.cpp` remain the largest units, containing the original
large interface-construction and scene-refresh methods. Splitting their internals
is separate work so it does not obscure a verifiable code move.

## Boundaries and rules for further work

- `workspace_internal.hpp` is private: shared dependencies, helper declarations,
  types and templates. It is not public API and does not belong in model/kernel
  modules. Centralized dependencies are an intermediate step, not yet a minimal
  interface between independent modules.
- Helpers use `zima::app::workspace_detail` and have exactly one definition.
  Default arguments belong in declarations, not repeated in `.cpp` files.
- Local widgets live in the consuming file's anonymous namespace: inline editors
  in `dimension_edit.cpp`, New Document in `documents.cpp`, operation progress in
  `layout.cpp`, for example.
- Files are explicitly listed in `sources.cmake`, loaded by `cpp/CMakeLists.txt`.
  Do not use globbing or `#include` `.cpp` files.
- Add features to the appropriate topic. Do not return command handlers to the
  lifecycle file.
- Keep modeling, reference, transaction and regeneration changes separate from
  mechanical moves. All `AGENTS.md` rules still apply.

This stage changed no document formats, start templates, config values, placement
rules, geometry identities or calculation algorithms.

## Move verification

Baseline commit: `35f7509`. Comparison covered all 386 method/helper/template/local-
type bodies in the original and split code. After normalizing line endings, they
were identical; none were added or missing. Independent signature checks confirmed
identical parameters and return types; only default arguments moved to declarations.
Use the standard project script for full Windows Release verification:

```powershell
./tools/build-windows.ps1 -Configuration Release -RunTests
```

Windows Release built. The initial full run passed 47/49 tests. Two failures were
test assumptions, corrected separately from the application-code move:

- The Origin test expected English `Point` after localized Czech `Bod` was added.
  It now checks the translated name and stable semantic key `origin:point`.
- The JPEG test compared image dimensions with the View after a status-message
  change could widen the window. It now measures the View at export confirmation,
  retaining exact image-size and unchanged-camera checks.

Both corrected tests passed (workspace window 87.91 s, Assembly restoration 3.14 s).
All 49 scenarios were thus verified; the other 47 were not repeated after changes
limited to these two test conditions.

A later stage can extract operations from windows and prepare shared GUI, console
and AI commands. This map does not authorize implementing the whole future
architecture or a comprehensive Undo/Redo audit.

The first extracted operations are described in
[DOCUMENT_OPERATIONS.md](DOCUMENT_OPERATIONS.md): native saving and document
Undo/Redo now live in the Qt-free workspace module.

Loading `.prtz`/`.asmz`/`.drwz` and creating documents from config templates now use
`modules/workspace/native_documents`. `documents.cpp` retains interaction and
activation; `geometry_helpers.cpp` only converts application settings for factories.

Explicit regeneration now lives in `modules/workspace/model_calculation`, and
pure saved-reference helpers in `modules/workspace/part_references`.
`calculation.cpp` translates editing state into calculation policy;
`view_events.cpp` and `assembly_commands.cpp` handle interaction before/after the
shared operation. Details and verification:
[DOCUMENT_OPERATIONS.md](DOCUMENT_OPERATIONS.md).

The command catalog and execution now live in `modules/command_host/src/host.cpp`.
`model_tree.cpp` reads data hierarchy without widgets or geometry calculation.
`workspace/console.cpp` handles the panel, interaction snapshot and display refresh;
`documents.cpp::finish_document_switch` shares refresh after GUI/console Open/New.
The host and its test program do not link Qt. The next stage adds standalone
`zima-cad-cli`: `cli/main.cpp`, `cli/runner.cpp`, `cli/settings.cpp`; see
[CAD_COMMAND_LINE.md](CAD_COMMAND_LINE.md).

The common Feature dialog and profile commands share
`modules/workspace/profile_operations` for validation and atomic commits.
`primitive_properties.cpp` retains the dialog, rollback and preview. The retained
Twisted Sheet command uses `modules/workspace/primitive_operations` and its
command-host adapter. The six basic solid primitives have been removed; tests
and examples create editable Sketch-based profiles instead. The solver and
placement contract are unchanged. See [UNIFIED_FEATURE_TYPES.md](UNIFIED_FEATURE_TYPES.md)
and [CAD_CONSOLE.md](CAD_CONSOLE.md).
