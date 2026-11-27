# Sheet Metal

## Before you start

Activate an editable Part Body and select **Sheet Metal** using the workspace buttons. Use a calculated sheet model with authored Flats and Bends for the
following development workflow.

## Steps

1. Open **Sheet Metal Properties…**. Check the material thickness and K factor
   used by the Part; confirm the intended settings.
2. Inspect the folded model. Confirm bend directions and material sides before
   creating the developed state.
3. Choose **Unbend**. Keep all eligible regions, or enable **Select individual features** and select the required regions in the Tree or View.
4. Confirm **OK** and inspect the developed geometry. Save the Part, then use the Sheet Metal toolbar’s **DXF** command for its supported flat-pattern export.
5. Use **Bend Back** when the following history needs the folded state again.

## What to check

Unbend and Bend Back are history operations; the preceding definitions remain
available. Reopen Properties for changes, or use Undo/Redo for committed actions.

Check developed dimensions against your manufacturing process. A default K
factor is not a material-specific calibration. **Sheet from Body** recognizes
supported tangent planar/cylindrical skins; arbitrary freeform conversion is
outside that recognition path.

See [Sheet Metal](../SHEET_METAL.md), [sheet states](../SHEET_STATE_DEVELOPMENT.md)
and [conversion and DXF](../SHEET_EXCHANGE.md).
