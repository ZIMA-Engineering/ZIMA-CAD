# Removing the current native file

## Scope

`delete_file` removes the saved file of an open Part, Assembly, or Drawing. The
target is the exact document ID and saved path; omitted `document` uses the active
document. This is neither arbitrary-path deletion nor Assembly component removal.
Native extensions and data formats remain unchanged.

| Argument | Type | Default |
| --- | --- | --- |
| `document` | string, open-document ID | active document |
| `archives` | boolean | `false` |
| `discard` | boolean | `false` |

`archives: true` includes all numbered archives in the confirmed snapshot. Naming
rules are shared with [archive management](ARCHIVE_COMMANDS.md). Other documents
and subdirectories are excluded.

Unsaved edits require `discard: true`. The command opens no dialog, saves no pending
model, and invokes no OCCT. Shared host guards block it during Properties or editing.

```json
{"command":"delete_file","arguments":{"document":"document-id","archives":true,"discard":true}}
```

## Shared operation and GUI confirmation

`workspace/file_removal_operations` contains preparation and execution. Preparation
only reads opening identity, path, revision, calculated-data generation, allocated
dimension-ID count, file size, and modification time. Including archives captures
their list, sizes, and times too.

GUI Delete Current File and Current File and All Versions use the same operation,
with one Yes/No confirmation defaulting to No. For unsaved edits, the question
explicitly includes discarding them. No changes nothing. Shared file-action guards
also block deletion during material Properties or other editing windows, not just
container dialogs.

After confirmation, the Workspace-owning thread revalidates documents and files.
Changed documents, another opening of the same ID, different paths, or stale file
snapshots are rejected before the first deletion.

Execution order:

1. Delete the current file. On failure, keep the document open and its archives untouched.
2. Close exactly that document without a Save prompt. The old GUI path could offer
   saving after deletion and recreate the file because a missing file needs saving.
3. Delete selected archives. OS failure here is a partial result, not restoration
   of the already deleted current file.

Closing uses shared selection of the remaining active/displayed document. Deleting
an activated source Part file preserves the parent Assembly, component, source identity,
and calculated snapshot. Its source path then points to the removed file. The
occurrence itself is not removed.

## Results and errors

Results contain `document`, `path`, `closed`, `changed`, `removed_paths`, and
`removed_bytes`. Execution failure adds `failed_path` and exact completed effects.
Visible errors report failed path and removed-file count. Even on partial failure,
the console refreshes GUI according to actual document closure.

Rejections include `no_document`, `document_not_found`, `path_required`,
`file_not_found`, `unsaved_changes`, `stale_document`, `stale_file`, and `stale_archive`.
Physical deletion errors return `file_io_error` or `archive_io_error`. Templates
are not targets of `delete_file`.

File deletion has no model Undo and does not use the recycle bin. Size/time checking
validates snapshots, not cryptographic contents. Another process can change a file
between validation and the system call; this is not a cross-process transaction.

## Verification

Model tests cover all three native formats, optional archives, discarded edits,
history preservation on rejection, stale document/file snapshots, reopening the same
ID, and activated-component context. Windows tests lock actual files without delete
sharing and distinguish current-file failure from partial archive failure.

Actual CLI regression covers all three formats, real files, JSON, exit codes, and
explicit `discard`. GUI uses both menus, Yes/No, clean/unsaved models, and open
Properties, checking one confirmation and no subsequent Save prompt.

Both applications and all targets built in Windows Release. Final targeted tests
passed **7/7 in 150.02 s**: new file removal, archives, document saving/history,
actual CLI, catalog, translations, and GUI console. Logs:
`build/file-removal-final-build.log`, `build/file-removal-targeted-tests.log`.
