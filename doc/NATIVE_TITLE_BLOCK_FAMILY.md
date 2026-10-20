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
Original single-Sketch frames and title blocks live in
`tests/fixtures/drawing-library` for generation and regression comparison, outside
the selectable factory library. Native symbol libraries use the same Body ownership rules; see
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

Regenerate the factory file from the reference fixtures with
`zima_title_block_family_tool config/formats/ZE-TITLE-BLOCK.tblz`.
The same tool generates the frame family when its output extension is `.frmz`.
Regeneration preserves the existing output's complete native user-parameter
tables, including every language value and label. A new output uses the matching
factory definition's metadata. An explicit `--metadata-from <native-file>` input
can restore authored metadata independently of the corrected geometry. The
result remains self-contained; the metadata source is not a document dependency.
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

## Factory library cleanup (2026-09-29)

Only the two combined native Family documents remain in `config/formats`.
The ten original single-Sketch inputs were moved byte-for-byte to
`tests/fixtures/drawing-library`; comparison, editing and generation tools now
read that directory explicitly. User-created documents and configuration were
not changed. New Drawing discovery continues to select a language Family row
and enumerate all five sheet formats through the existing implementation.

Verification now compares the actual committed factory Family documents against
the reference fixtures, including geometry, text, BOM and save/reopen behavior.
The focused Windows build and ten CTest contracts passed: native documents,
Drawing commands, Family Table, native title-block UI, new-document options UI,
thread Drawing, Drawing templates, template editing UI, symbol integration and
five-language translations. The packaging suite passed eight checks with two
platform/environment skips. No user-visible text or translations changed.
Existing published ZIPs were not rebuilt by this repository cleanup.

## Scale field and preceding Sketch context (2026-10-20)

The `&sheet.scale` token now uses the actual sheet scale, following the existing
view-description convention: `2:1` for enlargement, `1:4` for reduction and
`1:2.5` for fractional reductions. The former integer reciprocal incorrectly
reported enlargement as `1:1` or `1:0` and rounded fractional ratios. This is
numeric formatting only; no native format or template data changed.

The native contract checks ten scales from 0.001 to 1000 in all five title-block
locales, composed expressions and Drawing save/reopen. The actual Sheet
Properties GUI checks six scales with OK and Cancel, retained Family frames
and title fields, and native save/reopen. The formed-nose reference Drawings
independently verify `2:1`, positive calculated mass, constructor and date in
all five languages. No translated application text was added or changed;
the five-language catalog contract passed.

The full title-block GUI also exposed a separate missing-context defect:
`sketch_input_mesh` retained visible Sketches in passive Bodies but omitted
preceding Sketches in the active Body. The same display path now includes both,
using the existing visibility rules and the rollback Body cursor to exclude
downstream entries. This retains the common Frame while editing a language
Sketch, including the shared grip-preview path. It does not calculate solids,
change constraints or modify the protected placement contract.

The Windows native title-block GUI passed with preceding Frame visibility,
suppressed language exclusion, ordinary Body activation, Family selection and
Cancel, insertion, format changes and reopen. Sketch grip and context-conversion
GUI regressions passed. These are finite display/context checks, not an
exhaustive Sketcher dimension/solver matrix. Native Linux execution and release
package acceptance remain outstanding.

The first dependent nested-Body Sketch GUI run terminated with a segmentation
fault before diagnostic output. Its isolated rerun and two consecutive repeats
passed without code changes, including placed occurrence picking and dragging.
The cause of that first termination is unconfirmed; the repetitions are not
evidence that the transient failure has been fixed.
