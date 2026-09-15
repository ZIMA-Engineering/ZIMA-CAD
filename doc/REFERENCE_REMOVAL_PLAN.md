# Shared GUI/CLI reference removal: approved plan

On 2026-09-14 the user explicitly approved moving position/orientation reference
removal into a shared GUI/CLI function, including all dialogs, rows, locks,
highlights, and subsequent selection. Automatic review then permitted writing.
The approved implementation and verification are documented in
[PLACEMENT_REFERENCE_REMOVAL.md](PLACEMENT_REFERENCE_REMOVAL.md).

## Inputs, means, outputs

Inputs: open document, stable edited-object ID, and row index as in
`placement.reference.set` (0–2 position, 3–4 orientation).

Means: existing `PlacementReferenceRows`, shared model transactions, and current
removal handling in `ContainerPlacementSection`.

Outputs: the specified reference removed with remaining data preserved and identical
model results after GUI or command commit. Dialog cancellation or invalid-transaction
rejection preserves the original document.

## Verified existing behavior

In `cpp/modules/ui/src/container_placement_section.cpp`:

- `remove_reference` empties a position row in place without shifting later pending
  dialog rows, and releases the empty row's lock.
- If the orientation list contains the same original owner, geometric key, and
  occurrence path, remove that orientation and relabel remaining entries FRONT/TOP.
  This is existing position-removal behavior.
- Direct orientation removal in `refresh_orientation_table` only empties that row;
  it does not shift another orientation row.
- Trailing empty position rows are removed; internal gaps remain temporary dialog state.
- `combined_references/populated_references` filters empty entries on transfer to
  the model. The proposal preserves this persistence contract.
- GUI refreshes tables, invokes shared preview, then reactivates selection in the
  removed row. Order matters because View refresh can clear the previous selection filter.

## Proposed change

Extract only list/lock data mutation into a shared function beside
`assign_placement_reference`. Return whether data changed and whether a paired
orientation was removed. GUI updates labels/highlights accordingly; tables,
selection restoration, and preview remain in GUI.

Add `placement.reference.remove` over the same function. Existing domain transactions
validate ownership, prepare the proposed value, and commit one history step. Removal
must work for missing sources without resolving the geometry being deleted.
Remaining references and the resulting model still require normal validation.

Scope includes shared-section users: bodies, constructions, primitives, profiles,
Sweeps, holes, sections, and imported features. Inserted Assembly components retain
their separate mate management.

No changes are proposed to solving, formats, geometry identities, revision storage,
or tab-switch behavior. Body calculation stays in existing explicit model-transaction commit.

## Verification before completion

- First/middle/last position row, both orientations, invalid indexes, already empty
  rows, and locks of removed and retained rows.
- Paired orientation must match the exact occurrence path too.
- Identical pending data in GUI and model function; reference entry resumes after View refresh.
- GUI Cancel, OK, Undo/Redo, saving, and reopening.
- Actual geometry before/after changes for at least a primitive, body, and construction;
  rejected input without partial mutation.
- Missing original reference source: removal must not fail merely because it cannot load.

Linked-point repair is a separate stage with its own verification:
[CONSTRUCTION_REFERENCE_COMMANDS.md](CONSTRUCTION_REFERENCE_COMMANDS.md).
