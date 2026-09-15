# Numerical locks in GUI and CLI

`value_lock.list` and `value_lock.set` read/edit the same persisted locks as Properties
lock buttons and View dimension context actions. Shared transaction
`workspace/value_lock_operations` invokes no OCCT, reference solver, or Assembly mate
solving. Calculated geometry is preserved; changes create one Undo step. Repeating
an identical state leaves revision unchanged.

```json
{"command":"value_lock.list","arguments":{"object":"CONTAINER-ID"}}
{"command":"value_lock.set","arguments":{"object":"CONTAINER-ID","key":"length","locked":true}}
{"command":"value_lock.set","arguments":{"object":"CONTAINER-ID","key":"placement:x","locked":false}}
{"command":"value_lock.set","arguments":{"object":"BODY-ID","key":"placement:reference_offset:2","locked":true}}
```

`object` is an exact ID in an open Part/Assembly. Listing accepts optional `document`
for another open document; writing requires the active document with dialog/Sketcher
editing finished. Part requires the owning Body active, except for Body-placement
locks themselves. Derived Bodies allow own-placement and copy-parameter locks, not
source-geometry edits. Assembly uses immediate occurrence IDs: repeated sources have
independent locks. Parent Assembly does not change source-Part locks.

`.list` returns `document`, `object`, `revision`, and `items`, each with `key`,
`locked`, and `editable`. `editable` indicates whether the reference kind supports
offset editing, without bypassing document/Body guards. Zero and hidden dimensions
are included. `.set` adds `changed`; `locked` must be an actual JSON boolean.

Keys match Properties fields, such as `length`, `radius`, `primary`, `secondary`,
`profile_offset`, `pitch`, `base_offset`, and `placement:x`. Always list valid keys
for the specific object. Unknown keys cannot create arbitrary metadata. Sketch and
Drawing dimensions retain existing commands. Pattern uses `pattern:angle` and
`pattern:spacing:0/1/2` for individual Properties rows.

Part/construction placement separates `placement:rotation_x/y/z` from
`placement:rotation_offset_x/y/z`. Ordinary Assembly components have only rotations
and position. Mirror/Pattern uses own `copy_placement` with Part-placement locks and
applicable numerical Pattern fields. GUI maps offered dimensions to actual stored
locks using viewer `value_lock_key`; CLI supplies explicit keys.

`placement:reference_offset:N` is the current zero-based reference-field address:
Part skips empty/orientation-only references; components address their stored rows.
It is not an OCCT geometry index. Axis/point coincidence offsets do not have user-
editable locks. Invalid/overflowing indexes are rejected against existing keys.
No new geometric identities are created.

Shared conversion also accepts existing viewer addresses `parameter:...`, shortened
`x/y/z`, `rotation_...`, and `reference_offset:...`. Existing field names `size`,
`included_angle`, and `thread_nominal_diameter` mean `primary`, `angle`, and
`thread_diameter`. Catalog dimension `thread_designation` uses actual lock
`nominal_diameter`; `thread_pitch` uses `pitch`. These are current UI names, not file
migration. Assembly `placement-reference:OCCURRENCE-ID:N` maps to the same component row.

With Properties open, lock clicks affect only the dialog. OK saves value and lock;
Cancel discards both. Commands return `editing_in_progress`. Outside dialogs, toggling
a lock updates history and dirty indication without refreshing the model scene.

Formats/start templates are unchanged; existing `value_locks` and `offset_locked`
inside `.prtz`/`.asmz` are used.

## Stage verification

The first new-test build required a mutable helper path matching the Host constructor.
Its first run exposed missing helper-construction names, corrected in fixture input.
Review removed nonexistent correction angles from component listings and added rejection
checks. Focused model tests passed **1/1** (0.19 s):
`build/value-lock-model-final-build.log`, `build/value-lock-model-final-tests.log`.

Tests check unchanged calculated shape/fingerprint, read/no-op immutability, one
Undo/Redo, locked-value overwrite rejection, zero placement, correction angles,
original offsets, local 3D-curve points, active Body, invalid keys/types, exact
occurrences, shared component geometry, unchanged placement/mates, and native saving.
Actual CLI and GUI regressions passed in full **102/102** (479.54 s):
`build/value-lock-all-build.log`, `build/value-lock-full-tests.log`.

Final review added shared catalog-size/pitch addresses. Model tests verify atomic
M10 → M12 rejection while locked, unlocking, and successful editing. GUI uses the
actual catalog dimension in View and checks that locking prevents opening its inline editor.

After these additions both programs rebuilt; final related tests passed **7/7**
(162.86 s), including actual CLI, console, Properties, and View inline catalog:
`build/value-lock-alias-build.log`, `build/value-lock-alias-tests.log`.
The 102-test full run preceded this final addressing change.

Mirror/Pattern extension and locked-spacing verification in actual Properties are
in [DERIVED_COPY_COMMANDS.md](DERIVED_COPY_COMMANDS.md). A locked angle also blocks
count changes that would alter full-circle angular spacing.
