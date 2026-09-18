# Document operations without GUI

## First extraction stage (2026-09-11)

`cpp/modules/workspace/document_operations` contains shared Part, Assembly and
Drawing saving and document Undo/Redo. Public interface:
`include/zima/workspace/document_operations.hpp`; implementation:
`src/document_operations.cpp`. It uses no Qt, windows, picker or application loop.
GUI and console share this implementation; duplicate logic was removed.

Inputs are a Workspace, exact document ID and target path or history direction.
Output is a saved native document or changed history state. Display, dialogs,
localized messages and status panels remain in the application.

## Saving

Normal **Save** first asks the shared workspace whether the open document has
changed. The action remains enabled, but a clean Part, Assembly or Drawing whose
native target still exists produces no save job: it is not serialized, rewritten
or given a new file timestamp. A changed, unsaved or missing-target document
still uses the complete atomic native write below. **Save As** always writes the
requested independent copy.

1. On the Workspace-owning thread, `prepare_document_save` snapshots the document
   and already calculated data without writing or changing dirty state.
2. `DocumentSave::write` uses only the snapshot, either on a worker thread or
   synchronously without GUI. It uses existing native serialization and returns
   `SavedDocument` only after successful writing.
3. On the Workspace thread, `complete_document_save` looks up the document again
   by identity. It never retains a pointer into the open-document vector across
   the worker task. Only completion updates the path and possibly marks it saved.

Parameter revision alone is insufficient: explicit recalculation can change
calculated data without adding history. Completion checks revision, data generation
and allocated dimension-identifier count. Newer changes remain dirty.
`AssemblySession`, like `DocumentSession`, exposes a runtime data generation that
is not part of the file format.

Each open state has a runtime identity distinct from persistent document ID.
An old task cannot modify a document closed and reopened from the same file, or
one whose target path changed in the meantime. A worker snapshot is not an
Assembly-pinned revision and creates no sidecar.

Drawing uses tracked `DrawingState::commit`. Contents are externally read-only;
a committed change increments runtime revision. Save compares it with the snapshot
revision and leaves newer edits dirty. Tab/sheet switching, Cancel and display
refresh are not commits. Change detection needs neither an extra geometry copy nor
serialization. Document-level Drawing Undo/Redo was still missing at this stage;
this is a historical extraction record, not the current feature-coverage list.

## History

`can_step_document_history` and `step_document_history` use existing Part/Assembly
sessions. After a successful step, Part synchronizes external-sketch dependencies
as before. Operations change neither active nor displayed document.

GUI retains editing guards, section/sketch-local history, cancellation of a pending
segment and View restoration. These interactions do not belong in the document
core. This extraction is not a comprehensive Undo/Redo transaction audit.

## Verification and further boundaries

`zima_cpp_document_operations_tests` runs without QApplication and checks:

- actual write/reopen of all three native types;
- document identity, geometry and Part/Assembly-component references;
- immutable snapshots written on worker threads;
- newer edits and calculated data during saving;
- failed writes preserving path, revision and dirty state;
- rejection of missing documents and empty paths;
- Part/Assembly Undo/Redo, empty history and unsupported types;
- path changes and close/reopen during saving;
- clean Save as a byte- and timestamp-preserving no-op for all three native types;
- unchanged active and displayed documents.

Console integration still checks GUI editing, console saving and Undo/Redo against
real files. Full regression uses
`tools/build-windows.ps1 -Configuration Release -RunTests`.

Opening/template creation and regeneration are described in the next two stages.
The Qt-free shared command host now lives in `modules/command_host`; see
[CAD_CONSOLE.md](CAD_CONSOLE.md). File formats, extensions and config templates
remain unchanged.

## Verification results

Windows Release: full suite **52/52 passed**, 374.17 s. The new operations test
also ran independently; `dumpbin /dependents` confirmed no Qt dependency.
Full local log: `build/document-operations-full-tests.log`.

After localizing an error message and adding exact original-face ID comparisons,
affected targets rebuilt. `zima_cpp_ui_contract_tests`,
`zima_cpp_document_operations_tests` and `zima_cpp_console_ui_contract` passed
3/3 in 12.27 s. Geometry tests compare owner, semantic key and occurrence path of
every saved original face, not merely reference counts.

## Second stage: Open and New Document

Workspace `native_documents.hpp/.cpp` now provides:

- `read_native_document`: reads `.prtz`, `.asmz`, `.drwz` and saved geometry into
  a separate result; can run on a Qt-free worker thread;
- `prepare_new_native_document`: prepares a new document, units and start-template
  contents without creating a file or changing the open Workspace;
- `insert_native_document`: inserts on the Workspace-owning thread;
- `part_from_template` and `assembly_from_template`: shared factories also used
  by import for the same template settings and initial document.

`NativeTemplateSettings` carries settings paths and the translated first-Body name.
A small application adapter copies values from Qt settings. Reading/validating
templates and creating IDs needs no Qt. Config templates are not written or changed.

Part and Body get new identities; the Body binds to the new Part Origin using
existing `create_origin_bound_body`, with unchanged implementation and placement
contract. Assembly also gets a new ID. Application units transfer as before;
accuracy and other values come from the template. Drawing retains its existing
default constructor.

GUI Open first checks whether the path is already open. Insertion repeats the
check: if that file opened in the meantime, reuse its ID without overwriting
unsaved changes. An identical persistent ID under another path is rejected as
before. New rejects occupied paths both before preparation and insertion.

Insertion does not explicitly switch documents. Workspace still makes the first
inserted document the default. GUI handles tabs, tools, temporary-selection cleanup,
first active Body and View refresh, preserving post-Open/New interaction.

Graphical `.tblz`/`.frmz` templates and font-outline restoration remain in their
existing editor; they are not additional Part/Assembly/Drawing native types.
This stage changes no saved formats or required document files.

`zima_cpp_native_documents_tests` runs without QApplication and checks all three
types, View data, exact original-face identities, UTF-8 paths, config templates,
new IDs and correct Body-Origin binding, units, path collisions (including during
preparation), already open modified documents, duplicate IDs, missing/damaged files
and rejection of legacy `.prt`.

## Second-stage verification

Windows Release built; **53/53 tests passed** (354.86 s),
`build/native-documents-full-tests.log`. Native operations also passed independently;
`dumpbin /dependents` confirmed no Qt DLLs. Tests compare config templates byte for
byte before and after creation.

## Third stage: explicit regeneration and references

On 2026-09-11 the user explicitly approved changing protected container-placement
code for this structural move from the main window into workspace. Placement solver,
pass order, `history.size() + 2` limit, convergence and transactions stayed unchanged.
This records that specific approval, not general approval for later placement changes.

Qt-free `model_calculation.hpp/.cpp` provides:

- `calculate_part`: calculates while preserving valid geometry on failure;
- `calculate_part_with_resolved_references`: calculates until placement,
  constructions, external sketches and drilling points stabilize, including sections;
- `calculate_resolved_assembly_cuts`: calculates Assembly cuts with saved references,
  real rollback input and derived copies;
- `regenerate_part` and `regenerate_assembly`: explicitly regenerate an exact open
  document under existing history/dependency rules.

`part_references.cpp` contains the original pure helpers for owner selection,
external-reference refresh and saved-reference-geometry composition. GUI uses the
same functions through private workspace declarations. None derives geometry
identity by new OCCT topology traversal.

`PartCalculationPolicy` carries explicit-calculation context only: whether to reject
errors and which document/boundary is being edited. During rollback, the edited
feature's errors are assessed; downstream errors stay with their owners. GUI
converts dialog/rollback state into this structure. The module needs no window,
viewer or QApplication.

Regeneration runs synchronously on the Workspace-owning thread, without changing
the active document or displayed Assembly. GUI retains pending editing, View
refresh, selection and result reporting. Recalculating a Part without definition
changes updates calculated boundaries without another Undo step. Changed placement
or references commit through the same transaction as before extraction.

Explicit Assembly regeneration still uses open unsaved sources. Ordinary display
and tab switches do not trigger it. Existing
`Workspace::calculate_assembly_cuts` in dependency refresh remains unchanged and
uses saved cut definitions. The extracted application phase additionally solves
Assembly-cut references and its owned sketch. Merging these distinct phases was
outside the structural move.

Extensions, serialization, config and start templates are unchanged. There are no
revision directories or required files outside `.prtz`, `.asmz` and `.drwz`.

Independent comparison with the previous commit confirmed all eight moved helpers
were identical. Calculations and both regeneration transactions were identical
after replacing window-member access with explicit parameters.
Local audit: `build/model-calculation-extraction-audit.txt`.

Qt-free `zima_cpp_model_calculation_tests` checks three linked containers, a linked
section, source-dimension changes, original-face IDs, Undo/Redo, unchanged repeated
calculation, editing/regeneration errors, an unsaved Part in an Assembly, occurrence
identity and Assembly subtraction. Cut volume is independently checked as
2000 − 2 × 10 × 10 = 1800 mm³.

## Third-stage verification

Final Windows Release passed **55/55 tests** (365.35 s), including GUI, console,
file, Assembly, Sketcher and geometry regressions.
Log: `build/model-calculation-final-tests.log`. The first full run found one Axis
Properties compactness issue; the button-space fix is documented in
[NUMERIC_VALUE_LOCKS.md](NUMERIC_VALUE_LOCKS.md). The next full run passed.
`dumpbin /dependents` confirmed no Qt DLLs in the new calculation test
(`build/model-calculation-dependencies.txt`).

## Fourth stage: shared command execution

`modules/command_host` connects the text/JSON command catalog directly to these
operations. Main Window no longer registers its own command implementations. It
supplies settings, localization, interaction state, worker I/O and Fit, then receives
a change description for display refresh. Shared `finish_document_switch` preserves
temporary-selection cleanup and tree/View refresh for GUI and console Open/New.

The model tree and document list are readable without Qt, opening dependency source
files or OCCT. Command-program contracts, fields and boundaries:
[CAD_CONSOLE.md](CAD_CONSOLE.md).

## Document lifecycle (2026-09-11)

Shared `document_needs_save` and `close_document` protect all three native types
against closing unsaved edits or never-written files. GUI retains Save / Discard /
Cancel; console returns `unsaved_changes` until the caller saves or explicitly
passes boolean `discard: true`.

The host adds `activate`, `close`, `pwd`, `cd` and `save_as`. The last uses existing
`Workspace::save_copy` on a separate snapshot, as GUI does. It creates new identities
and redirects linked Drawings without changing original document identity. Targets
must use the correct native extension. Tab activation and copying do not call OCCT.
Formats and start templates are unchanged.

A Czech-directory-name regression exposed Windows system encoding in native
reference paths. Source paths for Drawings, views, BOMs and Assembly components
now serialize and read as UTF-8. Copy redirection/names and Part import metadata
use UTF-8 too. This is not a new format or migration branch.

Copying a Drawing redirects a BOM row directly referencing the copied model to its
new ID/path. Other components retain their sources.

Windows Release full run **58/59** (376.32 s,
`build/document-lifecycle-full-final.log`) exposed the missing BOM redirection.
After correction, **9/9 affected regressions** passed (16.71 s,
`build/document-lifecycle-verified-tests.log`): shared operations, Qt-free host,
real CLI processes, console panel, three Drawing GUI contracts, Workspace and all
five localizations. GUI/CLI built from the corrected state
(`build/document-lifecycle-verified-build.log`). Checks cover copied volume, new
IDs, actual native files, Unicode paths, unsaved-change protection, the last open
tab, and document switching without a new calculated-geometry generation.

## Archive files (2026-09-14)

GUI and CLI share listing/deleting numbered backups. Four commands,
`file.archives.list/prune` and `directory.archives.list/prune`, operate on real
files without changing open models. They are neither model Undo nor new required
sidecar storage. Native formats/templates are unchanged. Contract, errors and
verification: [ARCHIVE_COMMANDS.md](ARCHIVE_COMMANDS.md).

## Removing a saved document (2026-09-14)

`file_removal_operations` shares both current-file deletion menu actions with
`delete_file`. It closes the document after deletion without saving again. A locked
current file preserves document and archives; failure on a later archive returns
an exact partial result and reflects completed closure in GUI. Preparation checks
open identity, data generation, history and file snapshots. Formats, start templates
and geometry-calculation rules are unchanged. Contract/tests:
[FILE_REMOVAL_COMMANDS.md](FILE_REMOVAL_COMMANDS.md).

## Rename preparation (2026-09-14)

Data redirection is shared by open sessions (including Undo/Redo) and private native
snapshots. It preserves IDs, calculated bodies and dirty state, changing live data
in one deferred batch without geometry copies. `PreparedNativeDocument` validates
Drawing ownership, redirects saved paths and writes its own snapshot. File
transactions and GUI/CLI wiring followed in the next step. Design/verification:
[NATIVE_FILE_RENAME.md](NATIVE_FILE_RENAME.md).

## Rename completion (2026-09-14)

`rename_file` and GUI use `FileRenameJob`: private saved snapshots, input-freshness
checks, file publication, then live-metadata redirection. Rename neither saves
unsaved work nor clears Undo/Redo. Original IDs drive link updates in open/closed
documents; automatic Drawings are checked for actual ownership. GUI Save and tabs
preserve Czech characters after renaming all three types. Details, bounded search
scope and error recovery: [NATIVE_FILE_RENAME.md](NATIVE_FILE_RENAME.md).

Native opening was separately audited after further user feedback.
[NATIVE_DOCUMENT_OPEN_AUDIT.md](NATIVE_DOCUMENT_OPEN_AUDIT.md) describes saved-
geometry use and repeated checks that can slow loading. Rename did not change the
opening contract.

## Unicode names and directories in GUI (2026-09-14)

GUI file adapters use UTF-8 like CLI when creating, opening and copying native
documents and choosing working directories. Copying a Part with its Drawing
preserves linkage. Tests then open GUI-created copies of all three types through
CLI; Drawing JPG/DXF exports are also checked. Details and **5/5 regression results**:
[UNICODE_NATIVE_FILE_COMMANDS.md](UNICODE_NATIVE_FILE_COMMANDS.md).
