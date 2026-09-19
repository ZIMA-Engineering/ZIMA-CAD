# Mirror and Pattern

Both commands create referenced copies with their own Origin and standard container
placement. Select a source before starting or through the green Source field in View/Tree.

- **Mirror Properties** offers only XY/YZ/XZ planes of its own Origin for the
  reflection plane, through both the View and Tree. Other Origins and planar
  solid faces are rejected. The XY/YZ/XZ buttons select the same local planes.
- **Linear Pattern** uses one to three distinct own-Origin axes. Clicking a direction
  reference offers Pattern X/Y/Z axes under the pointer. Each direction has positive
  spacing and its own count. Two directions form a grid; three form a spatial pattern.
  The reference cross removes that direction.
- Each direction supports **Forward**, **Backward**, **Both Sides**, or **Symmetric**.
  Count includes the source; Both Sides has an independent backward-copy count.
  Symmetric count is odd (3, 5, 7…) with source centered. A 3 × 2 × 2 pattern means
  12 occurrences total: source plus 11 copies. Maximum is 1000 occurrences.
- **Circular Pattern** uses only X/Y/Z axes of its own Origin. Divide a full
  circle or specify angle between occurrences. Axis/reference survives switching to
  linear mode, where its control is hidden. Linear directions likewise survive
  switching to circular mode.
- Pattern counts include the source; its container adds remaining copies while the
  original remains independent.

Place the copy container first, then select its own local plane or axis. The same
rule applies in Part, Assembly, and an active nested occurrence. GUI selection,
command validation and reference resolution reject foreign Origins, source faces,
source edges, other occurrence paths, and separate plane/axis offsets. Use the
container's shared placement controls to locate and orient the Origin.

Double-clicking a derived child in the View shows the original source's edit
dimensions without opening Properties. In Assembly it resolves the exact source
occurrence, including Pattern children and repeated nested Assemblies. Explicit
source Properties remains a separate context-menu action.

In Part, source selection follows the editing scope. With a Body active, the
Source field offers only that Body's solid features before its insertion cursor.
At Part level it offers whole Bodies/Boolean outputs and individual source solids.
Assembly offers immediate owned components. Linear and circular Pattern use the
same source contract, including Tree selection, View hover/click and preselection.
A selected solid keeps its own identity; it is never promoted to its owning Body.

Creation preserves the active editing scope:

- Inside an active Body, Mirror/Pattern is a normal owned history feature at that
  Body's insertion cursor. It keeps the Body active and adds or subtracts copied
  source operands in the same Boolean chain. Later features can follow it.
- At Part level, copying a whole Body creates an independent history result,
  selectable, hideable and usable as a Boolean tool or target. Root-level solid
  copies also remain independent results.
- Assembly copies remain immediate components of the active Assembly.

The source is the original feature operand, not the accumulated Body at the
source boundary. A subtractive source repeats its cut; it never creates positive
cutter solids. The source must precede its copy in the same Body. Native files
persist both ownership and copy parameters. Editing, Cancel, Undo/Redo and
regeneration preserve that ownership. Removing or moving a source cannot silently
orphan its copies.

Copy placement and directions inside a Body are Body-local; the Body transform
is applied once to the final result. The common placement implementation is
unchanged. Properties displays the copy Origin immediately on opening, before
pressing a plane or axis shortcut. Closing the dialog retires this temporary
display state. A green check beside Active in Tree context menus identifies
the current Body or occurrence.

Sketches, construction objects, surface threads and body treatments (Fillet,
Chamfer, Shell) do not define independent source solids. Copy operations inherit
the source's Add/Subtract state during explicit calculation. Later Bodies remain
outside the active editing context.

In Assembly, containers reference immediate owned components. Each Pattern copy has
its own occurrence path, including subassembly copies. Linear identity derives from
integer local-axis position; increasing another direction's count preserves existing
copy identities. Geometry Properties and activation resolve to the original source.
Tree container Properties edits placement, source, and plane/Pattern parameters.
Editing shows input geometry with surrounding Assembly passive; Cancel preserves
the document, OK calculates/commits.

Explicit Regenerate updates derived-copy geometry from open sources. Tab switches/
View refresh do not calculate copies. Source, placement, or construction-reference
cycles are rejected. Current ordinary-source display sharing is documented in
[ASSEMBLY_GEOMETRY_SHARING.md](ASSEMBLY_GEOMETRY_SHARING.md).

OCCT calculates B-Rep only explicitly. Preview, picking, references, and Properties
use persisted ZIMA geometry. Derived topology identities store source owner and
semantic key; OCCT traversal never defines copy identity.

Verification: `zima_cpp_derived_copy_contract_tests`; GUI
`ZIMA_VERIFY_DERIVED_COPY_ONLY=1` with `zima_cpp_workspace_startup_contract`.
The command/query regressions also cover independent solid volume, placed Bodies,
linear and circular subtraction, source changes, native reload, cold regeneration,
Undo/Redo and rejection of foreign or downstream sources.

### Body ownership regression coverage

The command tests distinguish an active Body from the Part root explicitly.
An in-Body copy must have a preceding source in the same Body, remain an owned
history entry, preserve the active Body and support later ordinary features.
Tests cover Mirror and Pattern, Add and Subtract, overlapping copies, source
edits, source-deletion rejection, native save/reopen, cold regeneration,
Undo/Redo and a translated/rotated owning Body. Root-level tests continue to
exercise independent copied Bodies and their downstream Boolean chain.

The GUI test verifies owned Tree placement, Mirror/Pattern creation and
reopening, source picking, Cancel restoration, active context indication and
the operation's own Origin. Existing Assembly coverage includes nested and
repeated occurrences. Side preservation is governed separately by the
[geometry side contract](GEOMETRY_SIDE_CONTRACT.md).

### Acceptance on 2026-09-15

The GUI and CLI development builds passed. Eight contracts passed: derived-copy
model, command and query tests; multibody, native document, UI and translation
contracts; and the dedicated derived-copy workspace GUI contract. The GUI check
exercises the common View picker, solid/Body RMB cycling, all three copy modes
inside an active Body, circular creation/reopening and inherited subtraction.
Independent volume checks distinguish copying a 48 mm³ source from its accumulated
1000 mm³ Body; linear and circular cuts yield 856 and 808 mm³ respectively.

Local evidence: `build/pattern-solids-picker-build.log`,
`build/pattern-solids-tests.log` (the three model tests),
`build/pattern-solids-model-validation.log`,
`build/pattern-solids-native-validation.log`,
`build/pattern-solids-ui-validation.log`, and the final successful
`build/pattern-solids-gui-validation.log`. The capture is
`Projects/test/pattern-solid-circular-ui.png`.
