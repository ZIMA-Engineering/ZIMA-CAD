# Drawings

This document describes the current ZIMA-CAD Drawing data and interaction model.
Basic usage is also in the [user manual](UZIVATELSKY_MANUAL.md#basic-drawing-workflow).

## Document, sheets and source model

A `.drwz` Drawing registers zero or more source `.prtz` Parts and `.asmz` Assemblies
and may contain multiple sheets. See [Drawing sources](DRAWING_SOURCES.md). Each sheet stores its own format, frame, title block,
views, dimensions and sheet-local field values.

**New > Drawing** asks for its source native Part (`.prtz`) or Assembly (`.asmz`)
before creating the Drawing tab. Canceling the source picker or selecting an
invalid file leaves New Document open and creates no Drawing. The new Drawing
stores its source link before any view exists; it does not open extra model tabs.

**Insert View** directly starts transient placement using the linked model.
Clicking the sheet opens the existing view properties; OK inserts the view and
Cancel discards it. The command does not open a source file picker. An unlinked
Drawing disables view insertion until a source is added in Drawing Settings. Source loading errors
also leave the Drawing unchanged. `zima_cpp_drawing_source_picker_contract`
checks missing-source safety and direct Part/Assembly placement with OK/Cancel;
the application-tools GUI contract covers New Drawing and subsequent insertion
inside the main tabbed workspace.

Verification on 2026-09-17 passed four focused Windows contracts: translations,
source insertion, Drawing UI and application-tools GUI. The main-window test
covers canceling source selection without creating a tab, successful New Drawing
linkage before any view exists, placement and OK, and the Part-to-Drawing shortcut
without a source picker. Logs: `build/new-drawing-source-tests.log` and the final
GUI rerun `build/new-drawing-workflow-tests.log`. The first run exposed an incorrect
button type in the new test; the corrected test and rebuilt application passed.

Deferred by user agreement on 2026-09-17: add Drawing settings for managing
multiple source documents, including adding, replacing and disconnecting sources.
This is future work; the current change only handles choosing the initial source.

The **DRAWING** button in the source Part/Assembly tree header opens its existing
Drawing, or creates a new Drawing file in another tab. Source linkage is stored
before the first view exists. The reverse **PART**/**ASSEMBLY** button returns
to that exact source. Source renaming updates its path/name in the linked Drawing.

Frame/title-block definitions are copied into sheet data when inserted. The
Drawing no longer requires those `.frmz`/`.tblz` files in `config/formats`.
Deleting, renaming or editing a library template does not change existing sheets;
a new definition is adopted only by explicit frame/title-block replacement.

## Frames and title blocks

`.frmz` and `.tblz` files are edited in Sketcher. The template editor always
shows the full sheet, without ordinary model Sketch physical-scale startup.
The stored template origin maps directly to the Drawing origin at the lower
right, without hidden compensation.

Supplied title blocks:

- `ZE-RAZITKO.tblz`: Czech labels and localized parameter names.
- `ZE-TITLE-BLOCK.tblz`: English labels and localized parameter names.

Text starting with `&` references a parameter. Internal keys such as `name` and
localized labels (`&Název`, `&Name`, `&Наименование`) identify the same parameter.
**Sheet Properties → Title-block language** determines the language of read/
written values. Insertion inherits template `Locale`; shared values take
precedence over language-specific ones. Empty values use field placeholder text.

**Title-block values…** or double-clicking any value opens the full table,
including parameters in standalone text and the BOM region. Double-click focuses
the clicked value; aliases do not duplicate a parameter. Model fields follow
the source's saved **Parameters** order, then other data. Editable fields hover
orange and confirm cyan. Values load before the first view if a source model
is assigned; after views are inserted, the active sheet's first view determines
the title-block source.

**OK** writes changed simple parameter fields marked `WriteBack` into source
Part/Assembly Parameters in one source revision. **Cancel** writes nothing.
Open models take precedence over disk. An unopened source is loaded into the
workspace on confirmation; changes remain unsaved like ordinary Parameters
edits and can be undone in the source. The `&parameter` expression remains in
the title block; no local value override replaces it.

Standalone `&parameter` text outside the BOM without its own `Field` section
also becomes a parameter field at insertion. `&drawing.*` and `&local.*` values
belong only to the sheet. System values, compound expressions and relation-driven
parameters are read-only; writable constituent parameters of a compound
expression have separate entries. Clicking a BOM row opens the full table for
that row's source Part/subassembly; OK writes to that source. Being inside the
purple rectangle does not restrict the table's scope.

Mass driven by `model.mass` and its unit follow source settings. See
[physical properties](PHYSICAL_PROPERTIES.md) for density, units, component
snapshots and explicit Assembly regeneration.

## BOM Repeat Region in title blocks

On insertion or explicit regeneration, an Assembly view loads source components.
Repeated source documents group into one item with quantity; different sources
create separate rows. Each row reads its Part/subassembly Parameters. Suppressed
components are omitted; hidden components still count. BOM item count determines
row count; the purple frame defines repeated geometry and spacing.

The title-block editor identifies the area as a **BOM Repeat Region**. It is a
purple wire rectangle with no fill and highest selection priority: hovering any
edge highlights the whole region and labels. RMB cycling exposes geometry beneath.

The first double row has two special functions:

- Upper left: **Item Number**, sequential from 1.
- Upper right: **Quantity**, number of component occurrences.

Other fields are ordinary source parameters. A standalone Part produces one
row; an Assembly repeats the region in its stored direction to form the BOM.

## Inserted views and updates

**Insert View → click sheet → View Properties** is shared by all sources. The
first view is isometric. Properties selects an open source model or file, name,
label, orientation, style, scale and position. Creation/editing share one internal
dialog. Changes are preview-only until OK; Cancel neither creates nor edits a
view. MMB double-click over the sheet confirms OK.

View selection uses its full projected rectangle, including empty interior.
Hover, click and context menu share that area and overlapping-view order. Its
frame appears only on hover/selection. Tree and View select the same view;
clicking outside views clears selection.

**Projected View** in the context menu attaches a preview to the cursor and
snaps direction every 45° according to parent and sheet projection method.
Children store parent ID, direction and camera. Moving a parent moves children;
child movement stays on the projection ray. Name/label and custom versus sheet
scale are saved in `.drwz`.

Views retain source linkage and last calculated projection. Opening/tab switching
invokes neither OCCT nor automatic dependency refresh. **Regenerate** explicitly
reads the open source's latest calculated state, or its saved file, and refreshes
projection/dimensions.

The bottom **Source** selector offers the registered models and their Family
Table variants. Selection is sheet-local and controls navigation and new views.
A title block retains the source selected when it was inserted. **Parameters** above the
View or in the Drawing-name context menu opens source Part/Assembly Parameters
while retaining the displayed Drawing.

Frame/title-block selection starts in global **Formats**, including after live
configuration changes.

## Linear dimension

The initial associative dimension implementation supports two parallel straight
edges of an inserted view. Faces/general curves are not references for this
initial mode. Current expanded dimension coverage is documented in
[DRAWING_DIMENSIONS_DESIGN.md](DRAWING_DIMENSIONS_DESIGN.md).

1. Activate **Dimension**; the cursor remains an ordinary arrow.
2. Hover an inserted-view edge. Valid edges turn orange; RMB cycles overlapping
   candidates and the status bar identifies the offered object.
3. LMB confirms first and second edges; confirmed references remain cyan.
4. A yellow preview shows actual model distance. Move to position it and confirm
   with short MMB.
5. The tool remains active for another dimension; quick MMB double-click exits.

Dimensions store stable topology references of original objects, not screen
coordinates or current mesh edge order. Model regeneration resolves references
and refreshes value/position. Missing or ambiguous references must not silently
reattach to another edge.

Arrows share a sharp shape with 10° half-angle, also used for View dimensions,
Origin axes and BOM region direction.

## Initial limitations and later coverage

The first implementation covered one associative linear dimension between
parallel straight edges. Further ISO dimension types, developed sections,
details, tolerances, balloons, technical symbols and DXF export were listed as
next steps. This is a historical scope statement; later dimension, section and
export behavior is described in the linked current documents. BOM Repeat Region,
Item Number and Quantity were already functional at that stage.


## Editing frames and title blocks in C++

**Open** accepts `.frmz`/`.tblz` directly in the existing Sketcher document tab;
**New** creates either type. **Save** retains the file type; **Save As** creates
a separate copy. A numbered previous file version is retained before overwrite.
Opening never automatically rearranges or aligns geometry.

During the original library conversion, geometry, text anchors, constraints,
dimensions, pen colors and parameter fields were retained. Negative point-to-point
length dimensions became positive magnitudes with reversed point order,
preserving the positioning equation. Axis/origin coordinate placement retained
its sign. Converted templates used SchemaVersion 4 with native C++ Sketch data.
This records the conversion stage, not a requirement for legacy migration paths.

**BOM Region** in title-block Sketcher takes two rectangle corners, then Properties
sets position, dimensions, repeat direction and spacing. OK saves; Cancel discards.
Double-click/tree opens the same Properties; the menu can remove the region.
MMB double-click over the View also confirms.

The unfilled purple outline draws above other geometry and has highest hover
priority. LMB selects the whole region and synchronizes the tree. Before
confirmation, RMB cycles underlying geometry. The helper rectangle is not
printed. Actual lines, circles and text inside it repeat in the stored direction/
spacing. `&bom.item_number` and `&bom.quantity` supply item number/count; other
model parameters belong to the row's source. A Part produces one row. Drawings
embed their own template/BOM copy, so later library edits do not change them.

### Images in title blocks (C++)

The `.tblz` editor offers **Image** with its own icon. Selecting SVG, PNG, JPEG,
BMP or WebP opens shared internal **Image Properties**. Placement uses a clicked
Sketch point or X/Y coordinates. Horizontal Left/Center/Right and vertical
Bottom/Middle/Top align the rectangle to that point. Coordinates may be negative;
dimensions must be positive.

Width/height use mm. **Preserve aspect ratio** defaults on; changing either
dimension calculates the other from the original image. Turn it off to set them
independently. **Choose file…** replaces content in the same dialog. Preview is
transient: OK saves, Cancel restores; MMB double-click over the View confirms OK.

Select an image anywhere inside its rectangle or in the tree. Double-click opens
the same Properties; context actions include Properties/Delete. Purple BOM regions
draw last and have higher selection priority. An image inside a region repeats
with its contents.

Raster images normalize to PNG, retain transparency and embed in `.tblz`/`.drwz`;
the external source is no longer required. The command is title-block-only.
Decoded images use a bounded shared memory cache, so cursor movement does not
reload source files.

SVG remains original vector data and renders through Qt SVG in editor/Drawing.
Scaling/resizing does not rasterize it. Aspect ratio comes from exact `viewBox`;
raster images use original resolution. Supported SVG content follows Qt SVG.
For a portable company logo, convert text to curves and embed other images in
the SVG. Technical Drawings do not play animations.

### Constraints in supplied title blocks

Czech/English title blocks retain 14 unique driving dimensions instead of 52
repeated ones. The first 10 mm origin offset drives other 10 mm spans through
equal-length constraints; other repeated values are similarly shared. H/V
constraints preserve alignment and the template origin is attached to Sketch
origin. Horizontal/vertical offsets between diagonally located points use
auxiliary projection segments, avoiding measurement of the wrong diagonal.
Original coordinates, text and printable geometry remain. Solver regression
checks residual and maximum point movement within 1e-6 mm.

Orthogonal H/V/equal-length chains use an exact linear initial solution; the
standard solver then validates all constraints. Changing the main offset from
10 to 12 mm checks dependent rows too.

Sketcher locks protect dimension values during geometry dragging. **Lock
Dimension / Unlock Dimension** in the View and Dimension Properties toggle them.
Locked driving dimensions are black, unlocked yellow, measured brown. Selection/
hover remain cyan/orange. Locks do not fix labels; intentional numeric edits
remain possible in Properties.

Numeric fields/View dimensions use [shared value locks](NUMERIC_VALUE_LOCKS.md),
including one-time capture of the current value during reference entry.

Image/BOM Properties, commands, alignment and repeat directions have Czech,
English, German, French and Russian labels according to
[application language](LOCALIZATION.md). Language changes do not alter sizes,
alignment, BOM tokens or custom title-block text.

## Standard views, hidden edges and PDF

View Properties offers front, rear, left, right, top, bottom and isometric views.
Derived projected views follow the sheet's first/third-angle method. Four display
modes: visible edges, visible plus hidden edges, shaded with visible edges, shaded
without edges. Hidden edges may use dashes or solid gray lines.

Projection uses stored curves/triangles of the calculated body. It splits edges
at occlusion boundaries and adds curved-face silhouettes, such as a cylinder's
two side generators. Periodic-face seams are hidden. Triangle-derived silhouettes
are not new model edges and cannot become stable dimension references. Smooth
silhouette accuracy follows source tessellation.

**Sheet Properties** sets pen widths in mm. Agreed defaults: **white 0.50 mm,
red 0.70 mm, yellow/green 0.25 mm**. Visible edges use white width; hidden edges
(dashed/gray) and dimensions use 0.25 mm. **Lineweight preview** shows physical
widths on screen; default **Thin lines** supports drafting without them. Export
always uses actual widths independent of preview.

On a black working background, hidden edges and visible tangent edges use muted
dark gray (#666666), including dashed hidden mode. PDF retains black dashed
hidden edges or the selected solid-gray variant; tangent edges print with their
configured pen/width. Hidden dashes measure 3 mm with 1.5 mm gaps on paper,
independent of model scale.

**PDF…** on the bottom bar or **File → Export → PDF** saves all Drawing sheets to
one PDF. **File → Export** also offers JPEG of the current canvas and DXF of the
active sheet. **Save As** offers only the native `.drwz` document format. The standalone Drawing window also offers **Drawing → Save as PDF…**.
Pages retain actual sheet sizes, including mixed A4/A3. Lines/text are vector.
Shaded fill uses a depth-evaluated bitmap at 720 dpi, capped at 16 million pixels
and 8192 pixels per view side; embedded raster images remain bitmaps. Selection
frames, highlights and pending previews do not print. Normal pens print black
on white paper; optional gray hidden edges stay gray.

Print PDF at **actual size / 100%**. Printer "fit to page" changes scale and
physical widths. Export calls no OCCT and does not regenerate sources; it prints
saved view state. Adopt source geometry changes with explicit View Regenerate first.

### Fillet tangent edges

View Properties offers **Tangent edges: Thick lines / Thin lines / Hide** to
show smooth face transitions, especially in spatial views. Thick uses the white
pen (default 0.5 mm); thin uses 0.25 mm. Settings are per-view and apply to PDF.
Occluded tangent edges never draw, even with ordinary hidden edges enabled.
Curved-face outer silhouettes remain. Working thin-line mode does not change
print widths.

Classification uses persisted adjacent-face directions; opening Properties calls
no OCCT. Missing data is not evidence of tangency. Regenerate existing saved
views to refresh projection/edge classification.

### Regenerating a linked Drawing

**Regenerate** refreshes every view on every sheet, even without view selection.
Open Parts supply current unsaved state; closed Parts load from their file.
Removing the last body clears projections too. Dimension references and BOM also
refresh. Changes apply only after all sources load successfully. Persisted
Drawing projections are view state, not a separate model; explicit regeneration
replaces them from the linked source.

### Sections and hatching

View Properties offers saved Part/Assembly sections, hatch spacing on paper and
per-component settings. See [Sections](SECTIONS.md) for the placed-container/
Sketcher workflow and limitations.

## Labels, quarter-turns and sections

Besides fixed orientation, View Properties supports actual quarter-turns of the
current camera. Projected children automatically inherit the rotated parent
camera. View names and section labels have independent visibility/positions,
defaulting above the outline. Labels/dimensions use handles. See
[Sections](SECTIONS.md) for labels, paths, hatching and printing.
[Show/Erase](DRAWING_SHOW_ERASE.md) shows/removes original dimensions, axes and
auxiliary geometry per view, including text/handle movement and working guides
with 8 mm default distance/spacing.

### View orientation independent of section (2026-09-09)

Orientation or manual quarter-turns determines the camera. Section A–A changes
only displayed geometry; its oblique Sketch must not rotate the base view or
projected children. Automatic section-normal view selection was removed.
Regeneration loads current sources, then rebuilds projections parent-first,
including multiple levels and both projection methods. Open sources need not
be saved first.

The retained section side adapts automatically to view orientation so looking
from the reverse direction does not leave the section hidden behind the body.
No additional side switch is required; source Part/Assembly orientation remains
unchanged. Linked direction arrows follow the displayed side. An exactly side-on
view has no cut face to hatch.

### Live properties and handles (2026-09-09)

With Properties open, LMB dragging the rectangular view area moves its preview.
X/Y fields update continuously. Movement respects coordinate locks and derived
projection linkage. Only OK saves position and moves dependent views; Cancel
restores the original state.

Selected dimensions/labels are cyan with purple handles: simple solid dots the
same size as ordinary View points. On hover, entity/point remain orange. Handles
never print.

Empty template/title-block text displays `-` in the View so the editable field
can still be selected/opened. This editing marker is not written to the value,
Parameters or PDF.

### Shared hatch parameters and editable title block

The body/component table controls hatch angle, spacing, shift and type. Values
belong to the model section and OK writes them back; the Drawing retains only
per-body hatch visibility. Cancel writes nothing. [Sections](SECTIONS.md) covers
3D presentation and source-document persistence.

Double-clicking parameter text also works in repeated BOM rows and compound
text. For example, `soubor.&Verze` edits parameter `Verze` without renaming the
file. Stock, Name and Standard in a BOM row belong to that source Part/subassembly.
Changes affect neither other rows nor parent Assembly parameters; repeated
occurrences of the same source share values and retain quantity. Calculated
parameters (e.g. relation-driven mass) and system data remain read-only. OK edits
source Parameters, Cancel does not; normal model Save persists the edits.

## Relative view rotation and annotation guides (2026-09-20)

View Properties has two numeric inputs: horizontal and vertical rotation. Type
an angle directly, use the input arrows in one-degree steps, or use the adjacent
−90° and +90° buttons. The inputs are relative to the camera at dialog opening
and start at zero; reopening starts a new relative adjustment. Selecting a
standard orientation resets this base and both inputs. The camera is calculated
from the fixed base and both values, so editing either input does not accumulate
rounding drift. Its existing persisted basis stores the result; no extra angle
record is needed. Changes preview immediately, OK commits, and Cancel restores
the saved view. Derived projected views continue to inherit their parent's
orientation and keep these controls disabled.

The positive snap offset and guide spacing are in paper millimetres. Dimension
and balloon interactions show only their own view's active snap guides and a
visible green snap indicator. Standalone text remains freely positioned. See
[Show/Erase](DRAWING_SHOW_ERASE.md#view-scoped-annotation-snapping).

## Saving model edits made through a Drawing

Changing a source dimension through a Drawing registers that source for the
Drawing's next native Save. The shared save operation used by the main workspace,
standalone Drawing window and command host prepares immutable source snapshots
alongside the Drawing snapshot. It writes each touched source first and writes
the Drawing only after all source writes succeed. A source without a native
filename must be saved normally first. A source write failure leaves the Drawing
file untouched and its pending save relationship intact.

The full current Part/Assembly state is saved, including other unsaved edits in
that model. Unrelated models are never added to this save batch. Saving a source
independently clears its pending relationship. Save completion clears dirty flags
only for matching runtime identities and revisions; an edit made during the
write remains dirty. Multiple native files are not a filesystem-wide atomic
transaction: if a later write fails, earlier successfully written source files
remain saved on disk, and the Drawing remains pending.

This coordination is runtime-only unsaved-edit state. Source geometry, calculated
results, Drawing references and presentations remain in their native files; no
sidecar is introduced. The native-save test covers source-first writing,
unrelated-model isolation, write failure, independent source Save and newer edits
arriving while a prepared save is in flight.

### Verification (2026-09-20)

The native release application and Drawing harness build successfully. Targeted
CTest coverage passes for `zima_cpp_document_operations_tests`,
`zima_cpp_drawing_view_command_tests`, `zima_cpp_drawing_balloon_tests` and
`zima_cpp_drawing_sources_tests`.

The Drawing harness passes `--verify-show-erase` and `--verify-balloons` on
Wayland, including source Save/reopen and actual grip positions after snapping.
`--verify-view-controls` passes with the offscreen Qt platform, covering typed
angles, one-degree steps, quarter turns, preview, Cancel, OK and projected views.

The wider `zima_cpp_drawing_contract_tests` still fails at the previously recorded
title-block assertion `Shared 10mm master dimension cannot drive its equal lengths`
(also recorded in `SHEET_METAL.md`). The complete Drawing UI suite also encounters
the selected-text colour assertion `Selected dimension text is not cyan`; it is
not counted as passing. The focused checks above verify the changes in this update.
