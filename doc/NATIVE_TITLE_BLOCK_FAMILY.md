# Native drawing library Families

The factory `config/formats/ZE-TITLE-BLOCK.tblz` stores a native Part document.
Its extension selects the title-block editor and insertion behavior; its Family
Table is the ordinary Part Family Table, not a separate template variant table.

## Authoring and insertion

- Open the file normally. A normal active Body owns `Frame`, `CS`, `EN`, `DE`,
  `FR` and `RU` Sketch containers. Edit their Sketches through ordinary properties.
  Deactivating the Body returns to the ordinary Body-list command context.
  Visible preceding Sketches remain available while editing another Sketch;
  suppressed language variants are not editing context.
- `Frame` contains shared geometry, without texts or repeat regions. Each
  language Sketch contains its localized content and its own BOM repeat region.
- Five Family rows enable the common frame and exactly one language Sketch.
  Their names are shared language codes; localized variant labels are optional.
- Insert this title block in a Drawing and choose a Family row. The generic
  choice inserts only the common frame. The Drawing menu's title-block variant
  command changes an existing insertion explicitly; Cancel leaves it unchanged.
- UI language does not change the selected row or the inserted content.

The Drawing embeds the complete native definition and selected row identity.
Variant replacement reads this snapshot, not a potentially changed library file.
The drawing's existing right-hand anchoring convention remains in use.

Repeat membership is evaluated within each contributing Sketch and persisted
alongside the materialized drawing primitives. A repeat region in a language
Sketch must not repeat geometry from the common frame Sketch.

## Scope

`config/formats/ZE-DRAWING-FRAME.frmz` contains an ordinary Body with five Sketches
and Family variants A4, A3, A2, A1 and A0. Insertion or explicit frame variant
replacement changes the sheet format without removing the title block or its
right-hand anchor. The Drawing embeds the frame definition and selected row.
New frame/title-block creation also creates a native Part with an active Body.
Existing single-Sketch frame/title-block factory files remain available for
comparison. Native symbol libraries use the same Body ownership rules; see
[Symbol user guide](SYMBOLS_USER_GUIDE.md).
The prototype renders template Sketch content; arbitrary Part solids are not
converted into printed title-block geometry. Replacing a variant rebuilds its
template fields; use the Drawing's parameter bindings for retained project data.

## Verification

`zima_title_block_family_tool --verify` checks native load, six-Sketch editing and
Undo, five variant outputs against the original libraries, repeated BOM rows,
embedded Drawing persistence and variant changes after reopening.

`zima_cpp_native_title_block_ui_contract` exercises ordinary Family Table,
language Sketch entry, template commands, native saving, insertion, explicit
variant replacement and Cancel. It also checks Body activation, preceding Sketch
visibility, suppressed variants, frame/title-block tree icons, table resizing
and A3 frame insertion without losing the title block. It writes screenshots
under `Projects/test`.

Regenerate the factory file from the original language libraries with
`zima_title_block_family_tool config/formats/ZE-TITLE-BLOCK.tblz`.
The same tool generates the frame family when its output extension is `.frmz`.
Its backend checks include grouped Assembly BOM rows and automatic Drawing
balloons across language/frame changes and native save/reopen.

Verified on Windows on 2026-09-28: the application and tool build succeeded.
The final focused run passed all nineteen contracts in 113.07 seconds: native
title-block backend/UI, template commands and object commands, Family Table
backend/UI/localized labels/rename, five-language catalogs, template and new
document GUI, balloon backend/GUI, and six symbol library/document/placement/
Drawing/integration/GUI contracts. Family and symbol GUI additionally passed
three consecutive runs each. Backend comparisons
include exact line coordinates/styles and text positions/orientation, not just
primitive counts. Editor and inserted Drawing screenshots were visually checked.
This does not constitute a complete application regression run or a release.
