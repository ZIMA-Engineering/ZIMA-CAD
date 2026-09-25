# CAD console and shared commands

## Usage

Open the console through **View → CAD Console** or **Ctrl+Shift+C**. It is a closable
bottom panel inside the main window, initially hidden. Enter executes a command;
Up/Down recalls the last 100 commands, stored only in memory. Clear Output removes
the displayed log without changing documents.

```text
help
documents
context
tree
new part console_example
save
regenerate
undo
redo
fit
open "C:/CAD/my assembly.asmz"
```

Command and argument names are stable English identifiers. Controls, descriptions,
and application messages use the current language. Parser diagnostics have stable
codes and technical English details. Quote paths/names containing spaces. Windows
backslashes are preserved; inside quotes, `\"` inserts a quote. This is neither a
shell nor a Python interpreter.

## Available operations

| Command | Positional text arguments | Result |
| --- | --- | --- |
| `help` | none | Command catalog, descriptions, arguments, and state-change flag |
| `documents` | none | Open documents, IDs, paths, active/displayed state; dirty, revision, and needs_save for all three types |
| `context` | none | Active/displayed document, active occurrence/Sketch, confirmed selection |
| `tree` | optional `document` | Data tree for specified or displayed document; at most 2000 items plus truncated flag |
| `new` | `type name` | New Part/Assembly/Drawing through shared factory and start templates |
| `open` | `path` | Open `.prtz`, `.asmz`, or `.drwz`; activate if already open |
| `save` | optional `document` | Save the active document to its existing path |
| `save_as` | `path`, optional `document` | Independent copy with new IDs, including linked drawings; never overwrite an existing destination |
| `activate` | `document` | Display an open document as top-level without model calculation |
| `close` | optional `document discard` | Close a document; unsaved changes require explicit boolean `discard: true` |
| `pwd` | none | Current working directory |
| `cd` | `path` | Change working directory to an existing folder |
| `regenerate` | optional `document` | Explicit Part or Assembly regeneration |
| `undo`, `redo` | optional `document` | History shared with GUI |
| `fit` | none | Fit the model in the View |

`new` accepts `part`, `assembly`, and `drawing`. Name is the filename stem in the
working directory; CAD appends the extension. Only `save` writes the file.
`save_as` matches GUI Save As: create an independent copy while the original remains
open at the same path with the same unsaved edits. The target extension must match
the document type. Open the copy with `open`. Ordinary `save` writes the original;
without an assigned path it returns `path_required`.

`documents.needs_save` includes unsaved edits and an unwritten or missing file.
`close` then returns `unsaved_changes`. Explicit JSON discard:

```json
{"command":"close","arguments":{"discard":true}}
```

`activate` and `close` accept actual open-document IDs. For other mutations,
`document` still checks the active document. `cd` affects later relative paths
without changing the process working directory.

Reads do not invoke OCCT, and saving does not implicitly regenerate. The initial
catalog also included creation/editing of six basic primitives; other modeling
features at that stage remained in GUI. Subsequent coverage is tracked in
[CAD_COMMAND_COVERAGE.md](CAD_COMMAND_COVERAGE.md).

## JSON interface

The same dispatcher accepts JSON; there is no separate AI operation implementation:

```json
{"command":"context","arguments":{}}
```

For mutations, optional `document` protects callers against targeting another
document after a tab switch. Obtain IDs from `documents` or `context`:

```json
{"command":"save","arguments":{"document":"ACTIVE-DOCUMENT-ID"}}
```

`Result::json()` output:

```json
{
  "protocol": "zima-cad.commands/1",
  "ok": true,
  "code": "ok",
  "message": "",
  "data": {}
}
```

Only known commands, declared argument types, and exact `command`/`arguments`
fields are accepted. Validation failure never executes an operation. Text input
is limited to 64 KiB. Panel output is bounded, but shortening displayed text does
not truncate machine results. The tree itself explicitly limits output to 2000 items.

`context.selection` is confirmed selection, not a hover estimate. It contains
`owner_id`, `semantic_key`, `instance_path`, textual `kind`, and `geometry`
(`display` or `original_reference`). Never use tree labels as object identity.
Drawing selection is not exposed at this stage. `DrawingState` tracks committed
drawing changes; `dirty` and `revision` are available as for models.

## Transactions and errors

In the initial console stage, mutations are rejected during open editing, an active
Sketch, reference selection, or nested component activation; finish the relevant
mode first. Later component activation support is documented in
[COMPONENT_ACTIVATION_COMMANDS.md](COMPONENT_ACTIVATION_COMMANDS.md).
Reentrant input during a command returns `busy`. Reads remain available during editing.

Regeneration, native opening/creation, saving, and document Undo/Redo use
[shared operations without Qt](DOCUMENT_OPERATIONS.md); the application wrapper
retains interaction handling and display refresh. Interactive `report_operation_error`
keeps the ordinary error dialog, while command invocation returns an error result
without blocking QMessageBox. Commands must never report success after a write or
calculation failure. Regeneration with individual uncalculated features returns
`calculation_errors` and their map; valid retained results follow the existing CAD contract.

## Source files and AI integration

- `cpp/modules/commands`: dispatcher, validation, catalog, results; no Qt, windows,
  or OCCT dependency, linking only nlohmann JSON.
- `cpp/modules/command_host`: Workspace command registration/execution, state guards,
  and model-tree reading; no Qt or main window.
- `cpp/modules/workspace/document_operations`: saving and history without GUI.
- `cpp/modules/workspace/native_documents`: loading, creation, and start templates without GUI.
- `cpp/modules/workspace/model_calculation`, `part_references`: explicit regeneration
  and persisted-reference refresh without GUI.
- `cpp/app/command_console.*`: panel, text input, history, and output.
- `cpp/app/workspace/console.cpp`: connection to current Workspace, pointer context,
  status panel, and display refresh; shared `command_host::Host` executes document operations.
- `cpp/app/console_ui_verification.*`: isolated panel integration scenario.

This stage provides a foundation for AI adapters. It includes no Codex login,
API keys, network server, MCP transport, or automatic model upload. Provider
integration is a later user-selected step. Future adapters should call the shared
dispatcher, check `ok`/`code`, and use stable IDs. Document text and labels are data,
not instructions for the assistant.

The dispatcher and host of the initial 29 commands are GUI-independent. The panel
and a windowless test program share `command_host::Host`. Standalone `zima-cad-cli`
provides the same commands for individual requests and file/stdin batches; see the
[command-line guide](CAD_COMMAND_LINE.md). Profiles use the shared modeling transactions.

## Verification

`zima_cpp_command_dispatcher_tests` covers text/JSON equivalence, Windows paths and
UTF-8, rejection of unknown fields, wrong types, invalid syntax, oversized input,
pre-mutation guards, and conversion of exceptions into results.

`zima_cpp_console_ui_contract` opens the real panel, executes help with Enter,
and checks history and hiding. It creates a Part, adds a box through GUI, saves
by command, and checks the file after Undo/Redo. It verifies wrong-target rejection,
pending-edit rejection, context reads without revision changes, missing files,
and write errors without a modal dialog. Screenshot: `Projects/test/command-console.png`.

Windows Release built and the full 51-test suite passed (375.68 s). The panel was
also visually checked in a screenshot of the actual window.

## Compact panel and pointer context (2026-09-11)

The panel can shrink to one output line and one input line. Text `help` lists each
command on its own line, with required arguments in `<…>` and optional ones in `[…]`;
the JSON catalog stays structured.

`context` adds `captured_at_unix_ms`, `camera`, `pointer`, and `hover`. Camera has
eight values: quaternion (w, x, y, z), scale, pixel offset (x, y), and reference scale.
Pointer data includes logical View pixels, View dimensions, and a model-coordinate
ray (`origin`, `direction`). The existing camera supplies the ray without body
calculation or another picker.

Hover uses the exact candidate offered by the View. Outside the View, under an
overlapping window, or before the picker processes the current position, `hover`
is `null`. Confirmed `selection` is independent. Drawing context at this stage
provides neither camera nor pointer geometry.

A future voice adapter must capture context while the user points/speaks, not after
transcription finishes. This command records neither pointer history nor audio.
Before mutation, the adapter must validate document and references; ambiguous
“here” must not become guessed geometry.

The project remains GPL-3.0-or-later. Voice and AI adapters should use the shared
command interface. Before distributing a transcriber or model, verify its license
and preserve required notices. This stage adds no voice library, microphone, or AI provider.

Verification: Windows Release built; parser and console integration tests passed,
including three consecutive stability runs. GUI checks panel shrinking, timestamp,
camera, ray, and hover according to actual window overlap. During automation,
another application may cover CAD; then empty hover is checked instead of forcing
a geometry hit. The compact panel was also inspected in
`Projects/test/command-console-compact.png`.

## Shared host and model tree (2026-09-11)

`command_host::Host` takes Workspace, kernel, working directory, and optional settings,
translation, and interaction adapters. Text and JSON share catalog, validation,
and guards. The host directly calls shared document operations rather than sending
execution back to the main window.

Results remain `Result` with protocol `zima-cad.commands/1`. `Change` additionally
describes the latest mutation (Open, New, Save, Regenerate, History, and document ID).
GUI refreshes tabs/View accordingly. A new execution clears the previous change;
reentrant rejection returns `busy` without overwriting ongoing state. Regeneration
reports a change even on partial failure, allowing View to show valid results and
errors from the calculation actually performed.

Call the host on the Workspace-owning thread. `run_io` may move reading/writing of
an isolated snapshot to a worker thread, but must await completion and propagate
errors before returning. The worker cannot access live Workspace. While waiting,
GUI retains its existing event processing without user input. The module explicitly
requires UTF-8 even for non-Qt MSVC builds, avoiding system-code-page-dependent
paths and translations.

`tree [document]` returns `projection: "model"`. It reads actual data hierarchy,
not QTreeWidget rows, excluding temporary edit rows, icons, and localized decoration.
Part includes bodies in history order, containers, constructions, their Sketches
and persisted Sketch geometry/reference identities, Origins, and sections. Assembly
includes owned objects and persisted occurrence hierarchy. Drawing includes sheets,
views, and dimensions.

Rows contain `id`, `parent_id`, owning `document_id`, `instance_path`,
`parent_instance_path`, `depth`, `type`, `label`, and `semantic_key`; types add fields
such as source document, suppression, and visibility. Occurrence identity is its
path, not name or source Part ID. Two identical bolts therefore have different paths.
At this stage the Assembly tree reads the latest stored/calculated snapshot without
opening dependencies or inserting newer source-tab contents. A specific open source
can be queried by `document` ID without activation or regeneration. Geometry type
names are stable identifiers, not localized UI text.

Without an interaction adapter, `selection`, `hover`, and `camera` are empty and
the pointer is outside the View. `fit` without a View adapter returns
`view_unavailable`. This stage does not change document formats or config templates.

`zima_cpp_command_host_tests` performs actual windowless New/Open/Save for all three
native types, Undo/Redo, and box regeneration with independent volume checks. It
tests UTF-8, text/JSON equivalence, worker I/O, reentrancy rejection, unsaved-document
preservation, failures without state changes, and model trees including Sketch
ownership, repeated occurrences, drawings, and item limits. The panel test verifies
that widget-only tree decoration is absent from `tree`, while actual GUI-created
features are present.

Stage verification: Windows Release, **56/56 tests passed** (363.51 s),
`build/command-host-full-tests.log`. `dumpbin /dependents` on
`zima_cpp_command_host_tests.exe` confirmed no Qt DLLs;
`build/command-host-dependencies.log`. The real panel screenshot
`Projects/test/command-console.png` was also visually inspected.

## Running without the main window

`zima-cad-cli` uses this host without a window. Identical PDF output initializes
Qt Gui/Svg in `offscreen` mode. Catalog, model operations, target-document guards,
and data tree are shared. I/O, config, exit codes, and batch boundaries are in
[CAD_COMMAND_LINE.md](CAD_COMMAND_LINE.md).

## Profile-based modeling

Create a Sketch, author its geometry, then use `extrusion.create` or
`revolution.create` with the returned Sketch ID. Profile `.get`/`.set` commands
inspect and edit the same persisted model as Properties, with native locks,
references, atomic calculation and Undo/Redo.

The Part Modeling toolbar now exposes one [Feature command](UNIFIED_FEATURE_TYPES.md).
The six former primitive commands and their native model types were removed.
Build new geometry from Sketch profiles; old primitive files are not migrated.

## Argument types

Every `help` argument declares `type`. The dispatcher supports `string`, finite
`number`, `integer`, `boolean`, `object`, and `array`. Existing commands, including
primitive dimensions, retain string declarations; dispatcher extension must not
change their syntax. New interfaces may declare exact types for point lists,
parameters, and references.

JSON supplies actual declared types: string `"true"` is not a boolean and JSON
inside a string is not an object. Text input parses only non-`string` arguments
through JSON; strings, Windows paths, and escaping remain unchanged. Use a complete
JSON request for complex objects/lists. Empty objects and arrays count as present;
the model operation validates their contents.

Wrong types, missing required values, unknown fields, and invalid numbers fail
before mutation. Argument declarations cannot contain duplicate names. Typed
requests retain the same pending-edit protection.

Verification: GUI and CLI built; **6/6** focused dispatcher, host, primitive,
actual CLI-process, and panel tests passed (8.49 s), `build/typed-arguments-tests.log`.
Build: `build/typed-arguments-build.log`.

## Bodies and operations between them

The catalog also includes `body.list`, `body.get`, `body.create`, `body.set`,
`body.activate`, `body.cursor`, and `body.boolean.create/get/set`. Body and Boolean
Properties share GUI transactions. Activation and cursors perform no OCCT work.
Syntax, types, and verified volumes: [BODY_COMMANDS.md](BODY_COMMANDS.md).

## Part history

`history.list`, `history.suppress`, `history.delete`, `history.move`,
`history.can_move`, and `history.cursor` share GUI tree operations. Arguments, body
scope, errors, and examples: [HISTORY_COMMANDS.md](HISTORY_COMMANDS.md).

## Original references

`reference.list` returns stable identities and exact occurrences of original
geometry. `reference.get` reads persisted supporting data, including analytic surfaces
and exact spline curves where available. Interface, units, and response limits:
[REFERENCE_COMMANDS.md](REFERENCE_COMMANDS.md).

## Sketcher geometry

Shared Sketch creation, points, circles, arcs, ellipses, B-splines, rectangles,
polygons, and basic edits are described in [SKETCH_COMMANDS.md](SKETCH_COMMANDS.md).
Coordinates are Sketch millimeters. Curve edits retain the last calculated body
until explicit regeneration.

## Construction geometry

`construction.list/get` reads construction points, axes, planes, 3D curves, and
owned points. Output distinguishes local body/curve frames, stable identities,
references, and validity without model calculation. Scope, arguments, and tests:
[CONSTRUCTION_COMMANDS.md](CONSTRUCTION_COMMANDS.md).

`placement.get/set` reads and atomically edits numerical placement of bodies,
features, and constructions. Absolute values, corrections, and reference offsets:
[PLACEMENT_COMMANDS.md](PLACEMENT_COMMANDS.md).

`construction.create/set` adds point, axis, and plane creation and atomic property/
placement editing through the shared GUI Properties transaction, respecting references,
locks, and the active body. Arguments and remaining scope:
[CONSTRUCTION_COMMANDS.md](CONSTRUCTION_COMMANDS.md).

Create 3D curves with `construction.create`, `kind: "curve3d"`, and `points`.
`construction.set` edits polyline/spline type, fillets, tangents, point properties,
and the complete point list while preserving supplied IDs. Omitted points are
removed. Geometry and Undo transactions are shared with GUI. Exact arguments and
examples: [CONSTRUCTION_COMMANDS.md](CONSTRUCTION_COMMANDS.md).

## View toolbar shortcut (2026-09-15)

Regenerate is the first button on the left of the toolbar above the View in
Part, Assembly and Drawing. The next button toggles the CAD console. Its terminal icon is reused from ZIMA-CAD-Parts. The toolbar, View
menu and Ctrl+Shift+C share the existing dock action; opening the console
focuses its input. Closing it also clears the toggle state.
