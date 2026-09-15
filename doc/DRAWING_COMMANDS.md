# Drawing commands

Drawing commands expose sheets, templates, view creation/properties, saved
references and explicit regeneration. Model annotation queries and Show/Erase
share GUI operations. The introductory implementation stage had 152 catalog
commands; further annotations, source hatch styles and exact interactive View
capture were not yet fully covered. Later sections record subsequent additions;
use the current coverage overview for overall completeness.

| Command | Arguments | Result |
| --- | --- | --- |
| `drawing.sheet.list` | `[document]` | Saved sheets in document order |
| `drawing.sheet.get` | `sheet`, `[document]` | Parameters and embedded-object counts |
| `drawing.sheet.create` | `[name]`, parameters below, `[document]` | New sheet with stable ID |
| `drawing.sheet.set` | `sheet`, parameters below, `[document]` | Partial parameter edit |
| `drawing.sheet.delete` | `sheet`, `[document]` | Delete sheet, views and dimensions |
| `drawing.frame.load` | `sheet`, `path`, `[document]` | Embed `.frmz` geometry |
| `drawing.frame.clear` | `sheet`, `[document]` | Remove embedded frame |
| `drawing.title_block.load` | `sheet`, `path`, `[document]` | Embed `.tblz` geometry |
| `drawing.title_block.clear` | `sheet`, `[document]` | Remove embedded title block |

`document` identifies an open Drawing, defaulting to active document. Mutations
require an active Drawing and no pending edit. `sheet` is an ID returned by
creation/listing, not name/index. Queries read saved data without copying
projection geometry or requiring source files.

Sheet create/edit parameters:

- `name`: nonempty single line, maximum 256 UTF-8 bytes.
- `format`: `A4`, `A3`, `A2`, `A1`, `A0`; queries return mm dimensions.
- `projection`: `first_angle` or `third_angle`.
- `scale`: 0.001–1000; `0.5` means 1:2 and `2` means 2:1.
- `thick_line_mm`, `thin_line_mm`, `red_line_mm`: widths 0.05–2 mm.
- `locale`: nonempty single-line language code, maximum 32 bytes.

Create defaults: A4, first-angle, scale 1, widths 0.5/0.25/0.7 mm and `cs` locale.
Omitted edit fields retain values. Identical edits and clearing an already-empty
template create no history entry.

```json
{"command":"drawing.sheet.create","arguments":{"name":"Detail","format":"A3","scale":2}}
{"command":"drawing.sheet.set","arguments":{"sheet":"SHEET_ID","scale":0.5,"locale":"cs"}}
{"command":"drawing.frame.load","arguments":{"sheet":"SHEET_ID","path":"config/formats/ZE-A3.frmz"}}
```

Sheet scale affects only views with `use_sheet_scale`. For sections this explicit
edit refreshes projection/hatching from available calculated meshes; hatch spacing
remains paper mm. Open source wins over file and identity is checked. Missing
required source rejects the entire edit. Sources/Assemblies do not regenerate;
sheet changes use no OCCT. Projection-method edits change sheet settings;
explicit view regeneration updates existing projected cameras, as in GUI.

Format changes clear old frame/title-block contents, including circles, images
and repeat regions, while retaining views/dimensions. Clearing a title block
retains local parameters and BOM rows that are not its drawing geometry.
`.frmz` format must match sheet format. Templates embed in `.drwz`; their original
files are unnecessary after saving. Invalid imports preserve the sheet. The
last sheet cannot be deleted, nor a sheet whose view has a dependent view on
another sheet.

## History and GUI

`DrawingState` shares Undo/Redo between CLI and main GUI. Every commit owns a
revision; a new branch clears Redo. History retains native Drawing snapshots
without reprojection, file loading or OCCT. Assigned dimension numbers survive
Undo and are never reused. Completion of an older asynchronous save must not
mark a subsequently edited Drawing clean. `.drwz` format is unchanged; history
requires no external sidecar.

GUI sheet create/delete, Properties, bottom bar and frame/title-block import/
removal use the same model operations. Invalid values keep Properties open;
Cancel writes nothing. Pending edits block command mutations and main GUI Undo/
Redo. Enlargement scales correctly display as e.g. 2:1 in the bottom bar.

## Verification

Focused Windows Release tests passed **7/7** (21.42 s),
`build/drawing-sheet-integration-tests.log`: real CLI, GUI dialog/history,
native save/reopen, import errors and format changes. Independent geometry checks
verify 1.2 mm paper hatch spacing at scale 2, retained custom scale of another
view and unchanged source Part revision. Other tests cover missing/wrong-identity
sources, dimension numbering and saving during history changes.

The full Windows Release stage passed **79/79** (407.77 s),
`build/drawing-sheet-full-tests.log`, with both GUI/CLI executables built.

## Views, references and regeneration

| Command | Arguments | Meaning |
| --- | --- | --- |
| `drawing.view.list` | `[sheet]`, `[limit]`, `[document]` | Saved views and metadata |
| `drawing.view.get` | `view`, `[document]` | Saved view including actual camera |
| `drawing.view.references` | `view`, `[kind]`, `[limit]`, `[document]` | Original measuring curves/points stored with projection |
| `drawing.view.delete` | `view`, `[document]` | Delete view and projected descendants |
| `regenerate` | `[document]` | Explicitly regenerate all active Drawing views |

`view`/`sheet` are stable query IDs. `limit` is 1–10000, default 2000; `total`
includes omitted items. Queries open no files, project no geometry and change
neither confirmed selection nor history. They return view properties, source
ID/path, parent linkage, actual camera/orientation, paper-mm position, scale,
line styles, section, caption/guide settings, locks and projection/reference counts.

`drawing.view.references kind` accepts `all`, `curve`, `point`. Each row includes
original `owner`, semantic `key` and exact `instance_path`; the result also gives
the view's root `source_document`. IDs are not OCCT indices or invented projection
identities. Curves report line flag, sample count, first/last point and optional
circular data; points report coordinates. Positions use source-model mm
(`source_model_mm`); directions are dimensionless. Normal list queries omit full
sample arrays.

Regenerate loads available calculated sources, annotations, current selected
section/path definitions and refreshes projections, measured dimensions and BOM.
Projected descendants follow parents regardless of file order. Missing parents,
source identity mismatch, cycles, missing selected sections or inaccessible sources
reject the entire operation. Maximum projection-chain depth: 256. An empty
Drawing with no views creates no history entry.

Open sources are authoritative. Closed Parts use the same native snapshot as
open Parts, including standalone Sketches, construction and original datum
references. Auxiliary loading creates no user tabs. Drawing regeneration calculates
no source bodies or source Assembly/Part histories; it uses their latest calculated
state. Relative paths resolve from the Drawing file; BOM paths are UTF-8 on Windows.

Deletion removes all `parent_view` descendants and their measured dimensions.
An independent section view only loses its deleted route-parent view link;
section/source remain. One Undo restores views, dimensions and original IDs.
GUI Regenerate/Delete share these operations.

This stage passed **12/12** (36.66 s), `build/drawing-view-final-tests.log`:
box length 20→40 mm and measured dimension, reversed stored projection-tree order,
Undo/Redo, missing/wrong sources, annotations, sections, BOM, real CLI, GUI deletion
and paths with Czech characters. `Projects/test/command-drawing-views.png` was
visually checked.

Final source/input checks passed **3/3** (17.21 s),
`build/drawing-view-source-tests.log`, adding Sketch-only source, `.PRTZ` extension,
Czech filename and readable GUI view placement. Final build:
`build/drawing-view-source-build.log`.

## View creation and properties

`drawing.view.create` requires `sheet` and either open Part/Assembly `source` ID
or `parent_view`. `drawing.view.set` requires `view` and changes supplied fields
only. Both accept optional `document` and share the model operation used by
View Properties OK.

- `name`: nonempty single line, up to 256 UTF-8 bytes.
- `orientation`: `front`, `back`, `left`, `right`, `top`, `bottom`, `isometric`.
  Alternatively `camera` has three-number vectors `horizontal`, `vertical`,
  `depth`, forming an orthonormal basis with GUI convention
  `horizontal × vertical = -depth`. Both alternatives together reject. A new
  base command-created view defaults to front.
- `x_mm`, `y_mm`: −10000–10000 in paper coordinates: origin lower right,
  X grows left, Y up. A new base view defaults to 100,100 mm.
- `scale`: 0.001–1000, selecting custom scale. `use_sheet_scale:true` adopts the
  current sheet scale; specifying it with explicit `scale` is contradictory.
- `display_style`: `visible_edges`, `hidden_edges`, `shaded_with_edges`, `shaded`.
  `hidden_edge_style`: `dashed`, `gray`. `tangent_edge_style`: `visible`, `thin`, `hidden`.
- `show_caption`, `show_section_label`, `show_dimension_guides`: booleans.
  `guide_offset_mm`: 0–1000; `guide_spacing_mm`: 0.1–1000.
- `value_locks`: complete `x`, `y`, `scale` lock array; empty clears. As in GUI,
  a lock permits deliberate numeric edits.
- `section`: stable source Section ID from `model.tree`; empty clears.
  `section_markers`: complete displayed section-path ID array; empty hides them.
  `hidden_hatch_components`: complete exact component-key array excluded from
  hatching in this view. Visibility is view-owned, style source-Section-owned.
  Source style edits were not part of this initial stage; GUI retained its
  separate source transaction. Later coverage is documented below.

Projected creation uses `parent_view`, `projection_direction` and `distance_mm`
(0.001–10000 mm along a unit ray) instead of `source`. Directions: `right`,
`top_right`, `top`, `top_left`, `left`, `bottom_left`, `bottom`, `bottom_right`.
Source/camera derive from parent and sheet projection method; `source`,
`orientation`, `camera`, `x_mm`, `y_mm` reject for projected views. Later
`distance_mm` moves along the existing ray. Changing an existing parent/direction
was outside this stage.

```json
{"command":"drawing.view.create","arguments":{"sheet":"SHEET","source":"PART","orientation":"front","x_mm":120,"y_mm":80}}
{"command":"drawing.view.create","arguments":{"sheet":"SHEET","parent_view":"VIEW","projection_direction":"right","distance_mm":40}}
{"command":"drawing.view.set","arguments":{"view":"VIEW","scale":2,"show_caption":true}}
```

Parent movement translates all projected descendants by the same delta. Camera/
source edits reproject their chain while retaining child custom scales. Dimensions
refresh from original measuring references. Invalid parameters, unavailable
sources or a later child failure reject the entire draft before history changes.
One Undo restores the whole Drawing operation.

Property changes explicitly project, like dialog confirmation: they use latest
calculated source data, without source body calculation or OCCT. Short-lived
`DrawingProjection` shares source loading and identical-camera projections within
one operation/dialog, keeping different sources separate. Regenerate shares it.
Stored relative paths remain. Opening a saved Drawing and queries do not project.

This stage added `drawing.view.create/set`, reaching **138 commands**. GUI/CLI
share projection, atomic changes, child/dimension refresh. Coverage includes
orientation, custom camera, scale, paper placement, styles, sections and paths.
Loading BOM from an unsaved open Part on Windows was fixed. Full Windows Release
passed **80/80** (410.05 s), `build/drawing-edit-full-tests.log`; both executables
built. Tests cover real GUI/CLI, Undo, exact references, later-child failure
without partial writes, locks, scales, unsaved sources, sections and native save.
GUI screenshot: `Projects/test/command-drawing-views.png`.

## Model annotations and Show/Erase

`drawing.annotation.list` reads saved annotations without source loading or OCCT.
Optional filters: `view`, `kind` (`all`, `dimension`, `axis`, `construction`),
`mode` (`all`, `show`, `erase`), `limit` (1–10000, default 2000), `document`.
It returns `items` and untruncated `total`. Items include view, sheet, type,
visibility, invalidity, text, value, curve count and exact `reference`.
`all` includes invalid references for diagnosis; `show` offers valid hidden
items, `erase` valid visible ones. GUI and query share eligibility without
copying geometry merely to list it.

`drawing.annotation.show_erase` requires `views` (1–1000 entries), optional
`document`. Each entry contains:

- `view`: existing ID, once at most in the batch.
- `mode`: `show` (default) or `erase`.
- `selection`: `keep_selected` (default) or `remove_selected`.
- `kinds`: optional array of `dimension`, `axis`, `construction`, default all.
- `selected`: required exact-reference array from the query.

References require `source_document`, `owner`, `key`, `instance_path`. First
three are nonempty; an empty path is valid for a root source. Path participates
in identity, distinguishing repeated Parts. Unknown keys, duplicates and
unoffered references reject.

Among offered items, `keep_selected` shows selected/hides others;
`remove_selected` hides selected/shows others. Other types, unoffered annotations
and invalid references remain unchanged. `mode` determines the candidate offer,
as in Show/Erase GUI.

```json
{"command":"drawing.annotation.list","arguments":{"view":"VIEW","kind":"dimension","mode":"show"}}
{"command":"drawing.annotation.show_erase","arguments":{"views":[{"view":"VIEW","mode":"show","selection":"keep_selected","selected":[{"source_document":"PART","owner":"SKETCH","key":"DIMENSION","instance_path":"OCCURRENCE"}]}]}}
```

All views validate before writing; a final-entry error also prevents the first
change. GUI shares the atomic visibility operation. Multiple views form one
Undo/Redo; drafts, Cancel and no-op create no history. Active GUI dialogs block
console writes. Geometry, values and layouts remain; visibility uses existing
`.drwz` without format changes.

GUI/CLI build and integration passed **6/6** (21.27 s),
`build/drawing-annotation-tests.log`: native model, exact occurrences, atomic
batch, Undo/Redo, real CLI, GUI console, preview write protection and existing
multi-view Show/Erase with Cancel.

## Measured dimension queries and deletion

`drawing.dimension.list` accepts optional `sheet`, `view`, `limit` (1–10000,
default 2000), `document`, returning `items` and full `total`.
`drawing.dimension.get` requires exact `dimension`, optional `document`.
Both read Drawing data only: no sources, OCCT, history or last-valid-presentation
changes.

Each dimension reports IDs `dimension`, `sheet`, `view`, `kind` (`linear`,
`radius`, `diameter`, `chain`, `angular`) and `state`:

- `resolved`: valid measurement from saved view geometry.
- `hidden`: not displayed in this projection, e.g. an oblique circle.
- `unresolved`: missing/invalid reference, with last value if available.

`measurements` contains individual `segment`, `value`, `unit`, rendered `text`,
`last_valid` and use of local `angular_leaders`. Hidden dimensions and invalid
ones without a previous value may have an empty array. A last-valid value is
not a current measurement and must not be presented as one. A chain stays one
dimension with multiple segments, each with a stable ID.

`get` additionally returns `attachments`, `resolved_attachments`, `direction`,
`direction_resolved`, `parallel_reference`, `anchor_attachment`, `style`,
`segments`. Attachment `kind`: `point`, `curve_point`, `line`, `center`, `tangent`,
`intersection`; exact `reference`/`other_reference` contain `owner`, `key`,
`instance_path`; `parameter` and `side` select curve position/branch. Segments
include saved `layout`, `last_presentation`, `last_angular_leaders`. These
read-only diagnostics are not an unvalidated serialized-document write interface.

`drawing.dimension.delete` requires `dimension`, optional `document`, and removes
only measured Drawing dimensions (including chain segments). Model annotations
use Show/Erase. GUI Delete/context menu shares the operation. One Undo restores
references, last presentation and invalid dimension identity. Missing ID rejects
without history; assigned dimension numbers are never reused.

```json
{"command":"drawing.dimension.list","arguments":{"view":"VIEW"}}
{"command":"drawing.dimension.get","arguments":{"dimension":"DIMENSION"}}
{"command":"drawing.dimension.delete","arguments":{"dimension":"DIMENSION"}}
```

Native format is unchanged. Create/Properties follow below. GUI/CLI build and
focused tests passed **6/6** (23.53 s),
`build/drawing-dimension-command-tests.log`: 60° value, original occurrence,
invalid last value, hidden radius projection, filters, CLI/actual GUI Delete,
Undo/Redo, assigned-number retention and native save.

## Creating and editing measured dimensions

`drawing.dimension.create` requires `view` and `attachments`. `kind` defaults to
`linear`; alternatives `radius`, `diameter`, `chain`, `angular`. Radius/diameter
need one attachment; linear/angular two; chain 2–4096. ZIMA allocates dimension/
segment IDs before calculation. Sheet derives from view; cross-sheet mismatches
are impossible. Result ID is `dimension`.

`drawing.dimension.set` requires `dimension`; other fields are partial edits.
It can reattach to `view` on the same sheet. Changing `kind` requires explicit
new `attachments`, resets layout/last presentation, adapts default mm/° suffix
and retains dimension/surviving segment IDs. Reference-only replacement keeps
layout. Empty patches on valid dimensions create no history. Invalid dimensions
can be repaired with exact references; no nearby edge silently substitutes.

Each attachment has `kind`, `reference` (`owner`, `key`, `instance_path`), optional
intersection `other_reference`, `parameter` (default 0), `side` (±1, default 1).
Parameter is normalized along the original curve and also selects intersection
branch. Angular dimensions require two distinct straight `kind: line` references.
Center/tangent/intersection use the same native geometry/validation as Dimension GUI.

Optional create/edit parameters:

- `direction`: `automatic`, `horizontal`, `vertical`, `parallel`.
- `parallel_reference`: original straight reference for parallel direction.
- `anchor_attachment`: starting attachment index for measurement direction.
- `style`: partial `prefix`, `suffix`, `text_override`, integer `decimals` 0–12,
  `tolerance_mode`, text `symmetric_tolerance`, `single_tolerance`,
  `upper_tolerance`, `lower_tolerance`.
- `layouts`: one partial layout per segment: `text_along`, `text_outward`,
  `line_offset`, `arrows_reversed`, `radius_rotation_degrees`,
  `radius_center_line_hidden`.
- `placements`: one `[x,y]` per segment in the projected view plane; null retains
  a segment's position. Uses mouse placement, including minor/supplementary
  angular-sector selection.
- `document`: target open Drawing.

Coordinates/length shifts use projected-model mm, X right/Y up, not absolute
paper coordinates. Radius rotation/angular values use degrees. Apply `layouts`
before `placements`. Tolerance modes: empty, `symmetric`, `single_deviation`,
`deviations`; deviations are text like GUI and can contain decimal commas.

`drawing.dimension.extend` requires `dimension` and one `attachment`.
`at_first:true` prepends; default false appends. `position:[x,y]` places only the
new segment. Linear/chain dimensions are accepted; existing segment IDs/layouts
remain, including anchor-index adjustment on prepend. Use this command to prepend,
not attachment-array reordering through `set`.

```json
{"command":"drawing.dimension.create","arguments":{"view":"VIEW","kind":"angular","attachments":[{"kind":"line","reference":{"owner":"FEATURE","key":"EDGE_A","instance_path":""},"parameter":0.5},{"kind":"line","reference":{"owner":"FEATURE","key":"EDGE_B","instance_path":""},"parameter":0.5}],"placements":[[10,5]]}}
{"command":"drawing.dimension.set","arguments":{"dimension":"DIMENSION","style":{"prefix":"A=","decimals":2},"layouts":[{"arrows_reversed":true}]}}
```

GUI OK/CLI share atomic validation/commit. Invalid references, parameters or final
array entry leave no partial write. Invalid dimensions cannot commit until
references are repaired; valid hidden radial projections are allowed. Measurement
uses saved ZIMA view data, without OCCT/source opening. One Undo/Redo; persistence
uses existing `.drwz` without format change.

Integration passed **6/6** (21.36 s),
`build/drawing-dimension-edit-integration-tests.log`, followed by full Windows
Release **82/82** (376.97 s), `build/drawing-dimension-edit-full-tests.log`.
GUI/CLI built. Tests cover creation, 60°/120°, reference repair, both chain ends,
identities, parameters, atomic rejection, Undo/Redo, native save and actual display
of console-created dimensions. `Projects/test/command-drawing-views.png` passed
visual inspection.

## Title blocks and BOM source parameters

`drawing.bom.list` returns saved rows with exact `row`, `source_document`,
`source_path`, quantity and metadata. Optional `sheet`, `document`, `limit`
1–10000 (default 2000). It opens no sources and does not regenerate BOM.

`drawing.title.get` requires `sheet`, returning `fields` with `field` ID,
expression, displayed value, `writable`, `write_back`. It includes parameters
in plain title text and respects source ordering/localized names. Optional
`bom_row` is exact queried `row`, selecting a source Part/Assembly, not row index.
Without it, target is the main view's source or Drawing source when no views
exist. Open sources are authoritative; closed sources read native files without
workspace opening or OCCT.

`drawing.title.set` requires `sheet`, `values` (field ID → text). Optional
`expected_values` contains original changed-field values from `get`; any mismatch
rejects the whole request. The GUI dialog automatically uses the same guard.
`bom_row`/`document` have the query meanings. System/calculated parameters,
compound expressions and nonwritable fields cannot be overwritten. All fields
validate before writing; no-op creates no history.

```json
{"command":"drawing.bom.list","arguments":{"sheet":"SHEET"}}
{"command":"drawing.title.get","arguments":{"sheet":"SHEET","bom_row":"EXACT_ROW_FROM_QUERY"}}
{"command":"drawing.title.set","arguments":{"sheet":"SHEET","values":{"NAME":"New name"},"expected_values":{"NAME":"Original name"}}}
```

Model parameters belong to source Part/Assembly. Writing a closed source opens
it in the workspace, retaining calculated geometry and active Drawing. The source
is not automatically saved; explicitly activate/save it. Local `drawing.*`
parameters and literal text belong to the Drawing. Source edits refresh matching
saved row metadata across all sheets, without changing quantity or regenerating
parent Assembly. Changed BOM composition requires explicit Drawing regeneration.

History follows ownership: Drawing Undo restores local changes/saved rows;
source Part Undo restores its parameters. This is not cross-document Undo.
GUI/CLI share one model operation; pending GUI editing blocks console writes.
Native formats/start templates are unchanged.

Stage verification: **70/70** independent tests (112.97 s),
`build/drawing-title-independent-tests.log`, plus **13/13** main GUI scenarios
(269.82 s), `build/drawing-title-gui-tests.log`: all **83 tests**. After duplicate-
field guards, focused tests passed **6/6** (9.83 s), `build/drawing-title-tests.log`.
Coverage includes exact row sources, repeated occurrences, calculated parameters,
stale/conflicting values, relation results in BOM, Undo/Redo, explicit save,
UTF-8 and bidirectional console/dialog editing. The real-template title dialog
was visually inspected.

At that historical test run, the user's running CAD locked `zima-cad-cpp.exe`.
CLI/standalone tests built normally; GUI linked the same current CMake objects/
libraries into `zima-cad-title-validation.exe` in the build directory. A temporary
CTest copy changed only its executable path. This was no distribution package;
the original executable/running process were untouched. A normal GUI build was
still required after closing CAD at that stage.

## PDF export

`export.pdf` requires `.pdf` `path`; optional `document`, `overwrite=false`.
It exports every sheet of the active open Drawing in order and actual formats,
including mixed A4/A3, without native mutation/regeneration. Paths may contain
Czech characters. Result: `document`, `source_revision`, `path`, `pages`, `bytes`,
`model_changed:false`.

```json
{"command":"export.pdf","arguments":{"path":"výkres.pdf","overwrite":true}}
```

GUI/CLI use `drawing_render::SheetRenderer`, shared with the Drawing canvas.
Export omits hover, handles, previews and selection frames while retaining saved
projections, measured/model dimensions, sections, hatching, title blocks, embedded
images and view style. Lines/text are vector; shaded fill uses the existing
bounded raster. Resolution is 720 DPI without fitting to printable area; print
at 100%. PDF page sizes may have normal Qt rounding to printing points.

Title blocks read current open-source parameters without saving, or closed native
files without opening workspace documents. Stored BOM rows/geometry do not
regenerate. Export requires pending editing to finish, like other export commands.

Writing shares atomic completed-file publication with model exports. Any sheet
failure retains the original target, removes temporary data and preserves Undo.
Without `overwrite`, even concurrent creation cannot be overwritten. `.drwz`
and start templates are unchanged.

## Single-sheet DXF

`export.dxf` also accepts open Drawings, requiring `.dxf` `path` and exact
`sheet` from `drawing.sheet.list`. Do not specify Sketch ID in this mode.
Multi-sheet Drawings export one sheet at a time. `document` guards the active
document; `overwrite` defaults false.

```json
{"command":"export.dxf","arguments":{"path":"sheet-2.dxf","sheet":"SHEET_ID","overwrite":true}}
```

GUI passes its active sheet to the same `drawing_render::export_dxf` service.
Result: `document`, `sheet`, `path`, `source_revision`, `bytes`,
`model_changed:false`. Active sheet, model, history and sources remain unchanged.
Export reads finished projections/scales without OCCT/Regenerate. Title-block
metadata is current as for PDF; saved BOM is not recalculated.

Coordinates use paper mm (`$INSUNITS=4`), standard Y up, preserving view scales.
The existing Drawing DXF writer emits segments, text, lineweights, dashed hidden
edges, fills and hatches. Curved outlines become segments; analytic circles,
splines or parametric dimensions are not reconstructed from projection. Images/
shading become color fills without external raster files, potentially increasing
DXF size. Native Sketch export with `sketch` continues preserving supported
analytic circles/arcs.

Completed DXF publishes atomically. Invalid sheet, ambiguous Sketch+sheet input,
missing directory or source error preserves the original target.

## Sheet or crop image

`export.image` writes PNG (`.png`) or JPEG (`.jpg`, `.jpeg`) from one explicit
sheet. Required `path`, `sheet`; default `dpi=150`. Optional `crop_mm` is
`[left,top,width,height]` measured from paper upper left. Without it, render the
whole sheet. Crop must have positive size and lie inside paper. Output has a
white background and PDF print appearance, including calculated views, dimensions,
sections and title block.

```json
{"command":"export.image","arguments":{"path":"sheet.png","sheet":"SHEET_ID","dpi":150}}
{"command":"export.image","arguments":{"path":"detail.jpg","sheet":"SHEET_ID","dpi":254,"crop_mm":[30,10,60,40],"quality":95}}
```

Pixels equal `ceil(mm × dpi / 25.4)`; 60×40 mm at 254 DPI gives 600×400 pixels.
Resolution is stored in image metadata. Integer `quality` 0–100 (default 95)
controls JPEG lossy compression; PNG uses Qt's default lossless compression
and ignores quality. `overwrite=false`; optional `document` guards active target.
Result includes `document`, `sheet`, `path`, `source_revision`, `bytes`,
`width_px`, `height_px`, `dpi`, `model_changed:false`.

Before allocation, validate DPI 1–2400, maximum 16384 pixels per side and
64×1024×1024 total pixels (up to 256 MiB for RGB32 alone). Oversized requests
reject instead of silently shrinking. Geometry/history remain unchanged.
Atomic publication matches PDF/DXF; validation, source and encoder errors retain
the original target.

GUI **JPEG – current view** still captures the actual canvas, including zoom,
pan, colors and selection, through the shared atomic image writer. `export.image`
uses explicit paper crop/DPI and does not reproduce transient interaction highlights
or screen layout. Interactive 3D Part/Assembly capture was the next stage;
its later implementation is documented in [VIEW_EXPORT_COMMAND.md](VIEW_EXPORT_COMMAND.md).
