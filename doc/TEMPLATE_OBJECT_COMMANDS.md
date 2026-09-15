# Images and BOM regions through CLI

`template.image.list/get/create/set/remove` and `template.region.list/get/create/set/remove`
operate on existing template-editor objects. Creation belongs in a `.tblz` title
block, matching GUI. Queries and edits of persisted objects use the current
template; optional `document` protects mutations against active-tab changes.

```json
{"command":"template.new","arguments":{"kind":"title_block","name":"Title block"}}
{"command":"template.image.create","arguments":{"path":"logo.svg","x_mm":10,"y_mm":20,"width_mm":30}}
{"command":"template.image.list"}
{"command":"template.image.set","arguments":{"image":"ID-FROM-RESULT","height_mm":12,"horizontal":"center","vertical":"middle"}}
{"command":"template.region.create","arguments":{"x_mm":0,"y_mm":0,"width_mm":180,"height_mm":8,"step_mm":10,"direction":"up"}}
{"command":"template.region.list"}
{"command":"template.save"}
```

Coordinates and dimensions are millimeters in the template frame: X left, Y up.
An image requires a path and placement point; default width is 30 mm and height
follows the source aspect ratio. The shared importer converts PNG/JPEG/BMP/WebP to
embedded PNG; SVG remains embedded SVG. GUI and CLI share limits and validation.
`path` in `set` replaces content and name, retaining identity, placement, alignment,
and locks. Queries return format, intrinsic size, embedded base64 length, and actual
`corners_mm`, not the entire encoded media.

`lock_aspect` defaults to true. Supplying one dimension derives the other from the
image's intrinsic dimensions. Supplying both requires the correct ratio or
`lock_aspect: false`. Enabling aspect lock or replacing content preserves width;
a locked height takes precedence. With both dimensions locked, size is unchanged.
Conflicting numerical requests are rejected without a transaction. Alignment is
`left/center/right` and `bottom/middle/top`.

Regions require `x_mm`, `y_mm`, `width_mm`, and `height_mm`. On creation, omitted
`step_mm` uses height; later height changes do not automatically change spacing.
`direction` is `up/down/left/right`. Regions retain existing drawing rules for
repeated BOM rows.

`value_locks` replaces numerical locks: `x`, `y`, `width`, `height`, plus `step` for
regions. An empty array releases locks. A locked value can change only after
unlocking; the same command may remove its lock and change it. A previously unlocked
field may receive a new value and lock together. Unsupported names, invalid dimensions,
foreign IDs, and invalid files are rejected before commit.

GUI and CLI share commit and removal. Editing preserves ID; one change is one Undo
step. Unchanged `set` and removal of an already missing object return `changed: false`
without history or recalculation. `body_calculated` is always false. Dialog previews
remain transient; Cancel writes nothing.

Content and locks are stored in the existing template Sketch. Reopening does not
require the source image. Native drawings receive embedded data as before; no new
format or required sidecar is introduced.

Verification: all ten commands in the model test **1/1 in 0.15 s**, integration
**8/8 in 121.90 s**, and drawing image export **1/1 in 0.29 s**. Tests check real
files, removal of source media before loading, Undo/Redo, rejected/no-op transactions,
and GUI dialogs including locks and Cancel/OK. Logs:
`build/template-objects-model-tests.log`, `build/template-objects-integration-tests.log`,
`build/template-objects-image-tests.log`.
