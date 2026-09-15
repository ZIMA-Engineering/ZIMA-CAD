# Assembly history ordering through GUI and CLI

`history.list`, `history.can_move`, and `history.move` support Part and Assembly.
The GUI tree commits Assembly item moves through the same
`workspace::move_assembly_history` operation as the command host.

## Lists and identities

An Assembly maintains four separate lists:

- `components`: immediate Part and subassembly occurrences;
- `constructions`: standalone construction objects;
- `sketches`: standalone Sketch containers;
- `cuts`: the Assembly's own profile cuts.

`history.list` returns `type: "assembly"`, `items`, and an `orders` object with
these four lists of stable IDs. An item's `suppressed` field is the persisted
suppression flag; `component.get` provides effective component state including dependencies.

The move target is `object`. For a Sketch, use its container ID from
`orders.sketches`, not the internal Sketch ID. For a component, use its occurrence
ID in the immediate owning Assembly. A parent Assembly cannot move an internal
Part of a subassembly by its ID.

Optional `before` identifies the next item in the same list. Omitting it moves
the object to the end. Moving before itself or leaving the last item at the end
is a no-op. Moves between different lists are rejected.

## Transactions and dependencies

`history.can_move` only checks order, ownership, and references, returning
`allowed` and `would_change`. It changes neither the document nor calculated data.

Moving components, constructions, or Sketches requires no OCCT, component mate
solving, or cut regeneration. It preserves original references, resolved working
frames, and shared calculated results. A changed `history.move` creates one
Undo/Redo step and returns `body_calculated: false`.

Checks include dependencies passing through another list, such as
Sketch → construction plane → another Sketch. A move must preserve valid
source/dependent order. The existing rule allowing independent repair of
already broken dependencies remains.

Cuts use the existing `move_assembly_cut`, including explicit calculation and
original-reference checks. An actual cut reorder returns `body_calculated: true`.
The original `assembly.cut.move/can_move` commands remain available.

For queries, optional `document` can target another open document. Mutation
requires the active owning document and respects an open GUI editing session.
Nested Assembly activation preserves the displayed parent Assembly and exact
active occurrence path.

## Verification

The model test covers all lists, rejected changes and no-ops, component mates,
indirect Sketch dependencies, Undo/Redo, native saving, and nested activation.
Data-item moves are checked for preservation of shared results, every component
position, and the source Part revision.

An independent cut check uses a 10 × 10 × 10 mm box. Two separate cuts remove
1 and 2 mm³; after reordering, 997 mm³ remains and the other Part occurrence
retains 1000 mm³.

The GUI test invokes the actual tree callback for components, constructions,
and Sketches. It compares the entire saved `.asmz` with the result of the same
CLI command after Undo. It also verifies identical rejection of an order that
would break a reference.

The baseline Part/Assembly model regression passed 2/2 in 0.79 s. Both
applications and all test programs built. Broader regression passed **13/13 in
257.92 s**: Part/Assembly history, cuts, components, references, Assembly Sketches,
catalog, separate CLI process, translations, GUI console, and the full application
walkthrough. Logs: `build/assembly-history-verified-build.log` and
`build/assembly-history-verified-tests.log`.
The catalog has 292 commands; CTest registers 160 tests.
