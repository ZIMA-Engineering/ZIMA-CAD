# Fillet and Chamfer

## Before you start

Have a calculated solid in an active editable Part Body. These commands select
edges of the actual input body at their history position.

## Steps

1. Choose **Fillet** or **Chamfer**. Creation and later editing use that
   command's same Properties window.
2. Click the required input edges to add or remove them, and review the selected-edge list.
3. For Fillet, choose a constant radius R or variable R1 → R2 and check the direction. For Chamfer, choose A × A, A × B or A + angle; check the reference side for asymmetric values.
4. Select **ZIMA** or **OCCT** and review the automatically collected tangent segments. Removing a row removes only that edge segment; ZIMA does not switch to OCCT after a failed calculation.
5. Confirm **OK** to calculate. **Cancel** discards the pending change.

## What to check

The result should follow the intended edges and side. Reopening Properties
displays the preceding sharp input so edges can be repaired or replaced.
Double-clicking a Fillet exposes its parameter dimensions.

A failed first OK reports the error and keeps the draft open. A second unchanged
OK can retain a structurally valid definition as unresolved; that does not make
its geometry valid. Repair it through Properties.

See [edge-treatment usage](../USER_MANUAL.md#fillet-and-chamfer) and
[parameter rules](../EDGE_TREATMENT_COMMANDS.md).
