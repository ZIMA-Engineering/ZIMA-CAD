# Drawing text

**Text** follows **Dimension** in the Drawing toolbar. It creates ordinary text
on a sheet, with no modeling contours or solid-kernel calculation. It uses the
shared Sketch text properties implementation in Drawing mode: a larger multiline
editor, height in mm, alignment, ISO font, colour, rotation and horizontal flip.

1. Choose Text. A point and an empty-text dash follow the cursor over the sheet.
   Click to place the anchor; the preview then stays at that position. Text can be
   entered before or after placement. New text starts green at **2.5 mm**.
   The compact multiline editor provides the same symbol menu as dimension text;
   a symbol is inserted at the current text cursor.
2. Review the transient preview. OK commits one Drawing history transaction;
   Cancel discards it. A middle-button double-click also confirms over the sheet;
   a short middle click does not confirm.
3. Select existing text with LMB. Its cyan selection and purple anchor remain
   confirmed after leaving the glyphs. Drag the text or anchor to move it;
   Escape restores a pending drag. Double-click opens the same properties window.
4. Delete or the text context menu removes selected text. Undo/Redo restores edits,
   movement and deletion through the ordinary Drawing history.

Empty text remains selectable as a dash in the editor. Its stored value is empty,
not a literal hyphen; printing/export does not emit the placeholder. OK accepts an
empty value after an anchor has been placed.

Text belongs to its sheet and retains a stable local ID. Drawing INI **17** /
JSON payload **9** stores `texts` with the complete presentation, position and
Unicode string, including newlines. No external text file is required. The
shared sheet renderer draws each line as text in View, PDF, DXF and image exports.
PDF and DXF retain real text; each DXF line is a TEXT entity.

The Drawing GUI regression covers creation, cancellation, the shared middle-click
contract, editing, dragging, Undo, deletion, native reload and multiline exports.
Its `multiline-text` native/PDF/DXF/JPEG files and screenshot live under the
existing Drawing UI test output directory and are not packaged.

Dimension labels use font-aware clearance above their actual dimension line.
The opaque text mask includes glyph descenders, paper zoom and line weight; it
must not erase the line directly beneath the number. Both committed annotations
and property previews use this layout in the shared sheet renderer, including PDF.
