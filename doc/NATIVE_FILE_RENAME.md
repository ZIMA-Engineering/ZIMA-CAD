# Renaming native files

## Command and shared behavior (2026-09-14)

`rename_file name [document]` renames the saved file of an open Part, Assembly, or
Drawing. `document` is a stable ID, defaulting to the active document. `name` is the
new filename in the same directory. A missing extension is appended; changing native
type is rejected. Commands remain English; messages are localized in cs/en/de/fr/ru.

```json
{"command":"rename_file","arguments":{"name":"new name.prtz","document":"existing-document-id"}}
```

GUI Rename uses the same `FileRenameJob` as CLI and one internal PropertiesSubWindow
with OK/Cancel and shared middle-button confirmation. Invalid names are reported
inside the window; Cancel changes nothing. Renaming and current-file deletion must
not interrupt other editing windows, including material Properties, active Sketcher,
or selection.

Successful results contain `document`, `from`, `path`, `changed`, `updated_paths`,
and `recovery_paths`; errors may add `failed_path`. Identical names succeed with
`changed=false`. This is not model Undo: later dimension Undo must not restore an
obsolete nonexistent path.

## Identities, unsaved work, and dependencies

`document::FileRelocation` contains existing ID and old/new absolute paths.
`FileRelocationEdits` prepares all replacement strings/paths in advance. `apply`
swaps them without further allocation and updates runtime generations once.

PartSession, AssemblySession, and DrawingState include current state and all Undo/Redo
history in a shared short-lived batch. They retain model revisions, dirty state,
dimension IDs, occurrence IDs, references, and calculated bodies. Valid shared Part
snapshots are not recreated merely for renaming. The batch is prepared after worker
I/O on the Workspace-owning thread.

- Part updates its own name. External Sketch references carry document/occurrence IDs;
  STEP/DXF import provenance is not rewritten as a native dependency.
- Assembly updates immediate-component source paths and names of all occurrences
  of the renamed source, including nested snapshots. Occurrence IDs, placement,
  mates, and shared bodies remain unchanged. Historical names are rebased as well,
  so a later model Undo does not restore an obsolete source name. Independently
  named Pattern/Mirror groups retain their feature names.
- Drawing updates document/view/BOM sources, source name, and `file_stem`. Manual
  contents and other models remain unchanged.

References are resolved by ID. Path/ID conflicts are rejected; missing IDs are not
guessed from names. Relative paths belong to their owning native document.

The Tree **Rename…** action on a real Part or subassembly targets its shared source
document, including when invoked from a nested occurrence. It loads that source if
necessary and reuses the file transaction without activating it or replacing the
displayed top-level Assembly. Open tabs and matching occurrence labels refresh after
success. Renaming an ordinary feature, Section, Drawing sheet or view uses a separate
undoable name-only metadata edit.

Private `PreparedNativeDocument` rewrites only saved snapshots. Renaming does not
save pending parameters or geometry of open documents. Their metadata is updated
only after the complete file batch succeeds.

A same-stem Drawing in the same directory is renamed together with its Part or
Assembly, even if the Drawing is closed. Renaming a Drawing alone does not rename
its model. The companion's pending in-memory edits and source ownership remain
unchanged; the filename rule does not transfer document ownership.

Dependency updates apply only to documents currently open in the Workspace.
Closed Assemblies and other closed Drawings are neither scanned nor changed.
The same-stem companion is the only closed-document exception. The working
directory is not traversed. This scope was explicitly agreed on 2026-09-25.
Numbered archives are not renamed.

## Preparation, publication, and errors

`prepare_document_file_rename` captures opening identities, revisions, generations,
paths, and dimension allocations. `stage` may run on a worker without live-document
pointers. It reads saved files, validates identities, prepares rewritten native files,
and verifies reopening. Originals remain untouched.

`commit` rechecks live documents, input sizes/times, the open-document input set,
and destination availability. Closed test fixtures cannot block renaming a current
project. Participating files remain fully validated; unreadable open documents or
the same-name companion cannot be silently skipped. Identity conflicts in renamed
documents or older Undo states reject the batch before any original changes.

Before publication, originals move to transaction-owned temporary backups. Prepared
files then move to destinations. Only full success swaps live metadata, preserves
camera, and updates tab titles. GUI saving and tab titles use UTF-8 on Windows too.

On failure, published files and originals are restored. Complete recovery reports
no change. If recovery also fails, return `file_rename_recovery_required` with exact
remaining paths and recovery-data directories. Last preserved copies are not deleted.

Stable error codes include `invalid_filename`, `document_type_mismatch`,
`document_not_found`, `path_required`, `destination_exists`, `destination_open`,
`stale_document`, `stale_file`, `duplicate_document_identity`, and `file_io_error`.
Other validation failures return `rename_rejected`. Open Properties triggers
`editing_in_progress` before I/O.

Temporary `.zima-rename-<ID>` directories belong to one transaction; cleanup verifies
parent and exact name. They are not required storage for reopening documents.
Multi-file publication is not an OS-atomic transaction against process crashes or
concurrent external changes; incomplete recovery must explicitly retain manual-recovery
data. On Windows, case-only renaming is rejected as an occupied destination.

The operation invokes no OCCT or mate solving and changes no native schema. Start
templates remain valid. Required data remains in native `.prtz`, `.asmz`, and `.drwz`.

## Verification

`zima_cpp_file_relocation_state_tests` checks batches for all three types, relative
references, actual calculated bodies, sharing, historical conflicts, and unsaved
parameter preservation.

`zima_cpp_file_rename_command_tests` checks physical renaming, unchanged closed dependencies,
open documents outside the working directory, same-name Drawing companions,
standalone Drawing rename, drawings/BOMs, identical
names, Unicode, input changes during I/O, identities, and cache/Undo/Redo preservation.
On Windows it locks an original, later dependency, and staged file, verifying restoration
of original bytes.

Actual CLI tests use `.prtz`, `.asmz`, and `.drwz`. GUI checks invalid names, Cancel,
confirmation, later command renaming, actual source references, model history,
camera, tab titles, and Save after renaming all three types with Czech characters.

Final Windows Release built both applications and all targets. All **13 affected
tests** passed across several runs; this is not a new full regression of all 154 tests.

- Initial targeted run: **11/13 in 161.47 s** (`build/native-file-rename-targeted-tests.log`).
  Model, actual CLI, catalog, translation, and file/recovery tests passed. Two GUI
  fixtures failed: a numerical argument instead of an expression, and unfinished
  component-insertion Properties.
- After correcting input, GUI console passed in **116.32 s**
  (`build/native-file-rename-ui-tests.log`).
- Added invalid-closed-dependency checks passed in **1.63 s**
  (`build/native-file-rename-isolated-tests.log`).
- Final workspace-window test passed **1/1 in 105.84 s** unattended
  (`build/native-file-rename-unattended-tests.log`).

The older shared GUI fixture directory contained unsupported documents from other
runs. Workspace testing now uses its own subdirectory. A recurring timer handles
expected close/delete questions based on the visible window. Unexpected confirmation
after 10 seconds is logged, rejected, and marks the run failed. Helper-window cleanup
explicitly discards only test documents. Ordinary user-save confirmation is unchanged.
Runs requiring manual clicks do not count as successful verification.

One intermediate run failed an older angular-dimension GUI check selecting two
segments. The final complete workspace-window test passed it; this does not separately
rule out recurring instability of that scenario.
Latest build: `build/native-file-rename-unattended-build.log`.
