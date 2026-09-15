# Numbered archive versions through GUI and CLI

Numbered backups use shared Workspace operations and native-save numbering.
Files remain beside their document, for example `part.prtz.2` and `part.prtz.10`;
no new required storage type is introduced.

| Command | Arguments |
| --- | --- |
| `file.archives.list` | `path` |
| `file.archives.prune` | `path`, `keep` |
| `directory.archives.list` | optional `path`, otherwise working directory |
| `directory.archives.prune` | `keep`, optional `path`, otherwise working directory |

Relative paths use the host working directory. No open document is required.
File paths identify the base native document, not an individual backup. Supported
extensions are `.prtz`, `.asmz`, `.drwz`, and current `.frmz`/`.tblz` templates,
case-insensitively. The base file need not exist. Its filename is compared exactly,
including case.

## Listing and ordering

Listings contain `path`, `scope`, `groups`, `count`, and `total_size_bytes`.
Each group returns `document_path` and `archives`. Archives contain absolute UTF-8
`path`, exact string `version`, and `size_bytes`. Versions remain strings to preserve
long-number precision in JSON. Order is numerical, oldest first: 2 before 10.
Leading zeros are allowed; equal numerical versions are ordered deterministically by path.

Directory listing processes only immediate files, skipping subdirectories, file
symlinks, non-native extensions, and nonnumeric suffixes. It does not open native
contents, load models, or call OCCT.

## Pruning older versions

Required `keep` is a nonnegative integer. Zero removes all selected backups, 1 keeps
the newest, and N keeps the newest N **per document**. Counts above the available
backup count are no-ops. The current file is never removed.

Results return `selected_count`, `removed_paths`, `removed_bytes`, `keep`, and
`changed`. File deletion has no model Undo and changes no open-document revision,
unsaved edits, geometry, or selection. After changes, the host refreshes GUI file-action
availability. Pending editing blocks command deletion under the normal mutation
contract; listing remains available.

The shared operation first validates the complete selected list: valid numbered
native names, unique paths, regular files, sizes, and modification times. GUI retains
this snapshot through its existing confirmation dialog. Newly created backups are
not automatically added to an earlier confirmed list.

Invalid or changed snapshots are rejected before any deletion. If the OS fails during
deletion, results contain `archive_io_error`, `failed_path`, and exact already-deleted
`removed_paths`. GUI and console text report the error and actual removed-file count.
The operation never claims model Undo can reverse file deletion.

## Shared implementation

`archive_operations` replaces separate deletion loops in four old-version menu
actions. Existing Yes/No confirmation and keep-latest options remain.
`versioned_file.hpp` shares recognition and numerical ordering across GUI, CLI,
and native saving. New backup numbers increment as decimal strings without
`int`/`unsigned long long` overflow. Suffixes append to native paths without converting
Czech filenames through system ANSI encoding.

## Tests

Model and host tests cover all five extensions, Czech names, numerical order
001/2/10, 30-digit versions, and actual backup creation on save. They check file sizes,
grouping, keeping newest versions, wrong types, missing paths, maximum nonnegative
count, active editing, no-ops, and unchanged open models. Separate cases check stale
snapshots, duplicate paths, current-document rejection, and partial Windows deletion failure.

Actual CLI regression uses JSONL and checks resulting files and exit codes. GUI
uses all four menu actions, Yes/No, native backups created by saving, and a later
console command that must refresh menu state. Menu availability reads backup names
only; sizes/times are read for listings or a specific deletion snapshot.

Final Windows Release verification: both applications and all targets built.
The full run passed **150/151 in 591.33 s** (`build/archive-full-tests.log`); the new
GUI fixture incorrectly supplied unsupported `path` to `save`. It now sets the
working directory, creates the document, and uses ordinary `save`.

After that fixture repair and completion of visible partial-error console reporting,
final tests passed **7/7 in 164.31 s**: archives, native documents, document saving/
history, actual CLI, catalog, translations, and GUI console.
Logs: `build/archive-final-build.log`, `build/archive-final-tests.log`.
A genuinely locked Windows file verifies sharing errors, exact removed paths in
JSON, and the removed-backup count in text.
