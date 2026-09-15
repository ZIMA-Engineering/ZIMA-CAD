# Assembly profile-cut history

This extends [PROFILE_COMMANDS.md](PROFILE_COMMANDS.md). Operations manage only
cuts in the exact open owning Assembly. These commands do not modify source
Parts or internal components of inserted subassemblies.

## Commands

```json
{"command":"assembly.cut.suppress","arguments":{"container":"CUT-ID","suppressed":true}}
{"command":"assembly.cut.can_move","arguments":{"container":"SECOND-CUT","before":"FIRST-CUT"}}
{"command":"assembly.cut.move","arguments":{"container":"SECOND-CUT","before":"FIRST-CUT"}}
{"command":"assembly.cut.remove","arguments":{"container":"CUT-ID"}}
```

Each command accepts optional `document` under shared document-targeting rules.
`container` is the stable cut identity, not its name or tree position. Omit
`before` to move to the end. Unknown source or destination IDs are rejected
rather than treated as successful no-ops.

- `suppress` suppresses or restores the cut. An identical state returns
  `changed=false` without calculation or a new Undo entry.
- `move` recalculates the new order through the shared transaction. Identical
  order is a no-op. Moving before a previously valid reference source is rejected;
  calculation also verifies preservation of original reference geometry and links.
- `can_move` checks only identity and ordering dependencies. It returns `allowed`
  and `would_change` without calculating bodies. It cannot guarantee successful
  future calculation of a changed source; the actual move performs that check.
- `remove` removes the cut and its owned Sketch. Another Sketch can retain its
  last projected curve; its local reference to the removed source becomes broken.
  Undo restores the cut, its owned Sketch, and the valid reference.

Successful changes return `changed`, `document`, `container`, `revision`, and
`order`. An active dialog prevents conflicting mutation. Calculation uses a
prepared document with current open sources and publishes only the completed
result in one history commit. Failure leaves no partial component refresh.
This stage does not change the native format.

## Shared GUI operations

The cut context menu, Move Up/Down actions, tree dragging, and CLI share
`set_assembly_cut_suppressed`, `move_assembly_cut`, and `remove_assembly_cut`.
Separate GUI calculations were removed. Removal no longer leaves an orphan
Sketch with a nonexistent owner. Menu moves use the same dependency checks as dragging.

## Verification

Model tests passed **3/3 in 1.58 s**, covering two independent operations in one
occurrence, volumes of 970/994/1000 mm³, input bodies after reordering, suppression,
no-ops, dependencies, missing IDs, the editing guard, Undo/Redo, and native saving.
A separate scenario verified projected-curve preservation after source removal,
including the broken reference and its restoration.
Log: `build/assembly-cut-history-model-tests.log`.

After both applications and all tests built, extended integration passed
**9/9 in 115.96 s**. It covered actual GUI Move Up/Down, suppression, restoration,
and deletion with confirmation, volume and Undo checks, a separate CLI process,
Part history, profile references, and translations. Logs:
`build/assembly-cut-history-integration-build.log`,
`build/assembly-cut-history-integration-tests.log`.
The catalog has 240 commands and the full suite 137 tests. The last full 136/136
regression belongs to the preceding profile-cut stage; this stage has the targeted
regression reported above.
