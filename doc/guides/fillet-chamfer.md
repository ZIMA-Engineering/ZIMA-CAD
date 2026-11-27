# Fillet and Chamfer

## Before you start

Have a calculated solid in an active editable Part Body. These commands select
edges of the actual input body at their history position.

## Steps

1. Choose **Fillet** or **Chamfer**. Creation and later editing use that
   command's same Properties window.
2. Select the required edges. Use Ctrl to extend or reduce the selection, and
   review the selected-edge list.
3. For Fillet, enter the radius. For Chamfer, choose symmetric distance, two
   distances or length/angle; check the reference side for asymmetric values.
4. Inspect any continuous route. Removing one member removes that member;
   removing its parent removes the route.
5. Confirm **OK** to calculate. **Cancel** discards the pending change.

## What to check

The result should follow the intended edges and side. Reopening Properties
displays the preceding sharp input so edges can be repaired or replaced.
Double-clicking a Fillet exposes its radius dimension.

A failed first OK reports the error and keeps the draft open. A second unchanged
OK can retain a structurally valid definition as unresolved; that does not make
its geometry valid. Repair it through Properties.

See [edge-treatment usage](../USER_MANUAL.md#fillet-and-chamfer) and
[parameter rules](../EDGE_TREATMENT_COMMANDS.md).
