# Removing the current native file

## Scope

`delete_file` removes only the current saved file of an open Part, Assembly,
or Drawing. The target is the exact document ID and saved path; an omitted
`document` argument uses the active document. This is neither arbitrary-path
deletion nor Assembly component removal.

| Argument | Type | Default |
| --- | --- | --- |
| `document` | string, open-document ID | active document |
| `discard` | boolean | `false` |

Unsaved edits require `discard: true`. The command opens no dialog, saves no
pending model, and invokes no OCCT. Shared host guards block it while Properties
or another edit session is active.

```json
{"command":"delete_file","arguments":{"document":"document-id","discard":true}}
```

## Shared operation and GUI confirmation

`workspace/file_removal_operations` owns preparation and execution. Preparation
only reads opening identity, path, revision, calculated-data generation,
allocated dimension-ID count, file size, and modification time. GUI Delete
Current File uses the same operation with one Yes/No confirmation defaulting to
No. For unsaved edits, the question explicitly includes discarding them.

After confirmation, the Workspace-owning thread revalidates the document and
file snapshot, deletes the current file, and closes exactly that document
without a Save prompt. A deletion failure keeps the document open. Closing an
activated source Part preserves the parent Assembly, occurrence, source
identity, and calculated snapshot.

Existing numbered files such as `part.prtz.1` are outside this operation. They
are neither discovered nor removed. ZIMA-CAD no longer creates or manages
numbered native-document archives.

## Results and errors

Results contain `document`, `path`, `closed`, `changed`, `removed_paths`, and
`removed_bytes`. Execution failure adds `failed_path` and exact completed
effects. Rejections include `no_document`, `document_not_found`,
`path_required`, `file_not_found`, `unsaved_changes`, `stale_document`, and
`stale_file`. Physical deletion errors return `file_io_error`. Templates are
not targets of `delete_file`.

File deletion has no model Undo and does not use the recycle bin. Size/time
checking validates snapshots, not cryptographic contents.

## Verification

Model, CLI, and GUI tests cover all three native formats, explicit discard,
stale document/file snapshots, locked files, opening identity, activated-source
context, single confirmation, and the edit-session guard. They also verify that
an existing numbered file remains untouched.
