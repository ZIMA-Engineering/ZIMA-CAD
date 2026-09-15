# Mirror and Pattern

Both commands create referenced copies with their own Origin and standard container
placement. Select a source before starting or through the green Source field in View/Tree.

- **Mirror** uses a plane or planar face. XY/YZ/XZ buttons choose planes of its own Origin.
- **Linear Pattern** uses one to three distinct own-Origin axes. Clicking a direction
  reference offers Pattern X/Y/Z axes under the pointer. Each direction has positive
  spacing and its own count. Two directions form a grid; three form a spatial pattern.
  The reference cross removes that direction.
- Each direction supports **Forward**, **Backward**, **Both Sides**, or **Symmetric**.
  Count includes the source; Both Sides has an independent backward-copy count.
  Symmetric count is odd (3, 5, 7…) with source centered. A 3 × 2 × 2 pattern means
  12 occurrences total: source plus 11 copies. Maximum is 1000 occurrences.
- **Circular Pattern** uses an axis, straight edge, or circular edge. Divide a full
  circle or specify angle between occurrences. Axis/reference survives switching to
  linear mode, where its control is hidden. Linear directions likewise survive
  switching to circular mode.
- Pattern counts include the source; its container adds remaining copies while the
  original remains independent.

In Part, source selection follows the editing scope. With a Body active, the
Source field offers only that Body's solid features before its insertion cursor.
At Part level it offers whole Bodies/Boolean outputs and individual source solids.
Assembly offers immediate owned components. Linear and circular Pattern use the
same source contract, including Tree selection, View hover/click and preselection.
A selected solid keeps its own identity; it is never promoted to its owning Body.

Mirror and Pattern remain independent history results, selectable, hideable and
usable as Boolean tools/targets. A positive source creates a combined result of
its additional copies. A subtractive source repeats the cut in its source Body;
the resulting history step replaces that Body in the displayed Part result.
Pattern counts include the original, already applied source. Copy geometry has
no independent editable history; edit dimensions and Add/Subtract at the source.

Solid sources use the original feature operand, including its Body transform and
resolved extrusion limits, rather than the accumulated body at that feature's
history boundary. Sketches, construction objects, surface threads and body
treatments (Fillet, Chamfer, Shell) do not define independent source solids.
The inherited subtraction state is stored in the native document and refreshed
during explicit calculation. Native Part INI version 23 and Assembly payload
version 31 include this state; the tracked start templates use these versions.

Starting from an active Body inserts Mirror/Pattern immediately after it. Sources
must precede that boundary; later Bodies are suppressed in View and gray in Tree from
Properties opening, even before choosing a source. Cancel restores original history.
In the active-Body menu, Mirror/Pattern follows Drill Point and precedes the green
separator and Box.

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
