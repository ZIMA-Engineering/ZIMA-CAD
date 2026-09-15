# Reordering history items in the Tree

Drag a top-level item with LMB. A green line marks the insertion boundary between
siblings; a prohibited cursor marks an invalid destination. Clicking without
dragging still confirms Tree/View selection. Escape cancels the move.

Part reorders containers, standalone Sketches, and constructions in the shared
`history_order`. Assembly retains its existing collections: Parts/subassemblies
can be reordered together; operations, constructions, and Sketches move within
their respective collections. Moving never changes ownership or the active
occurrence. Reordering is disabled while an editor or command is open.

Pre-drop validation uses persisted identities and dependencies, including
placement, source Sketches, external Sketch references, Up-to targets, edge/face
references, constructions, and Assembly occurrence mates. It protects both move
directions. Part Move Up/Down commands use the same validation.

Dragging performs no OCCT calculation. Releasing calculates the changed Part
history or Assembly operations on a working copy. Lost persisted references,
missing original reference geometry, or calculation failure prevent commit.
Assembly component order changes without recalculating source documents.
A successful change is one Undo/Redo transaction, persists with the document,
and preserves the view.

`zima_cpp_ui_contract_tests` covers bidirectional dependencies, external references
in owned Sketches and Assemblies, the green line, clicks, invalid drops, and Escape.
`--verify-startup` checks moving real documents, persistence, and Undo.

Run the targeted integration test with `ZIMA_VERIFY_HISTORY_DRAG_ONLY=1` and
`--verify-startup`; it uses the same test function as the full run.
