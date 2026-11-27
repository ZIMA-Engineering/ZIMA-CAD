# Drawings

## Before you start

Prepare a source Part or Assembly. A Drawing (`.drwz`) stores its source link,
sheets, views and annotations.

## Steps

1. Choose **File > New**, select **Drawing** in the dialog and enter a name. Confirm **OK** and select the source model, or use **Drawing** in the open source’s Tree header.
2. Review the sheet format, frame and title block. **Format** changes the active
   sheet; the bottom **+** adds another sheet.
3. Choose **Insert View** and place the view. Review orientation, scale and display settings in the opened dialog, then confirm **OK**.
4. Add required dimensions and text. **Show / hide dimensions…** exposes supported source
   dimensions, axes and auxiliary geometry in a selected view.
5. Save the Drawing, then use the appropriate export for PDF, current-sheet DXF
   or a sheet/region image.

## What to check

Verify scale, annotation references and readable text before export. Sheet X
increases to the left from the bottom-right Origin; this is intentional.
Changing the format preserves that anchoring convention.

View Properties also edits existing views. Cancel discards pending changes.
Use **Regenerate** to update from current source data. A missing reference becomes
unresolved instead of silently attaching to another edge.

See [Drawing workflow](../USER_MANUAL.md#basic-drawing-workflow) and
[Drawings](../DRAWINGS.md).
