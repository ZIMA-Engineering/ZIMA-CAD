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

- `ZE-TITLE-BLOCK-CS.tblz`: Czech labels and localized parameter names.
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

View Properties has three numeric inputs: horizontal, vertical and in-plane rotation. Type
an angle directly, use the input arrows in one-degree steps, or use the adjacent
−90° and +90° buttons. The inputs are relative to the camera at dialog opening
and start at zero; reopening starts a new relative adjustment. Selecting a
standard orientation resets this base and all three inputs. The camera is calculated
from the fixed base and all three values, so editing an input does not accumulate
rounding drift. Its existing persisted basis stores the result; no extra angle
record is needed. Changes preview immediately, OK commits, and Cancel restores
the saved view. Derived projected views continue to inherit their parent's
orientation and keep these controls disabled.

New base and projected views receive distinct numbered default names (`Pohled 1`,
`Pohled 2`, etc.). Existing and manually assigned names are preserved.

The dialog groups source/name, orientation, display/line styles, scale/position,
guide settings, and sections/breaks. Related inputs occupy adjacent columns with
labels above them, using the available width instead of a long form.

Drawing guides are horizontal/vertical rectangles enclosing the actual 2D
projection, including retained fragments of broken views. Model annotations do
not substitute projected 3D boxes or enlarge these bounds. Both Drawing-created
and Show/Erase dimensions use this same snapping aid. Source dimensions retain
their model values and references; Drawing-created dimensions measure the
unshortened projection in the view plane. Moving a view carries both types of
dimensions without changing their values or view-local layout. Confirming an
orientation change removes Drawing-created dimensions in every affected view,
including projected descendants, because their measurement plane has changed.
Model dimensions retain their source ownership. Cancel preserves all dimensions;
Undo restores the former orientation and removed dimensions together. The spatial
frame in Part/Assembly is unchanged.

The positive snap offset and guide spacing are in paper millimetres. Dimension
guide offset and spacing inputs both display three decimal places. View Properties
also exposes the number of offset guide frames (0–100, default 4); zero disables
the frames and their snap targets. This count is saved with each Drawing view
and is available as `guide_count` through the view command API.
Dimension and balloon interactions show only their own view's active snap guides and a
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

The guide-count follow-up passes the native view command test (count and spacing
Save/reopen, removed snap targets and zero count) and the offscreen view-controls
UI test (three-decimal spacing, count editing, OK, reopen and Cancel). Both the
native application and Drawing harness were rebuilt successfully.

## Broken views

View Properties offers an isolated editor for view breaks, with model-space
position/length dimensions and a paper-space gap. See [Broken Drawing views](DRAWING_BREAKS.md)
for editing, reference preservation and persistence.

### Drawing projection and break acceptance (2026-09-20)

The current Linux native application, CLI and Drawing harness are rebuilt.
The repository-root `./zima-cad` continues to launch this native build.
`build/drawing-resume-acceptance.log` records 12/12 passing targeted tests:
view, dimension and annotation commands; PDF/DXF commands; Show/Erase,
measurement-dimension, balloon, view-controls and break-editor GUI contracts;
measurement geometry and Drawing sources. The main-workspace Drawing check also
passes (`build/drawing-resume-workspace-tests.log`).

Coverage includes oblique horizontal/vertical/direct projected measurements at
multiple scales, paper-axis rectangular snapping for both dimension kinds,
actual mouse dragging of already dimensioned ordinary and broken views, native
save/reopen, numbered view names, three-axis rotation, dimension removal in
rotated parents and projected descendants, Cancel and Undo/Redo. Break coverage
includes endpoint/segment dragging, expression entry without committing the
Properties dialog, cursor-centred zoom, source references, hidden attachments,
multiple intervals, clipping, fixed paper gaps and export. Antialiased colour
checks compare the actual colour against its coverage over the black canvas,
rather than requiring fully covered exact-RGB pixels on thin lines.

Inspected captures:

- `build/drawing-view-properties.png`: compact grouped Properties with three angles.
- `build/drawing-break-editor.png`: independent full-view break editor.
- `build/drawing-break-result.png`: shortened view retaining its 1000 mm model dimension.

Broader tests are not all green. `build/drawing-resume-regression-tests.log`
reproduces the previously documented full-Drawing-UI assertion `Selected dimension
text is not cyan` and title-block assertion `Shared 10mm master dimension cannot
drive its equal lengths`. The separate 3D dimension-layout test cannot obtain
an OpenGL framebuffer offscreen; with Wayland and software OpenGL it reaches
`View text background erases its dimension line: sketch=0, font=12, angle=0.000000`
(`build/drawing-resume-spatial-frame-tests.log`). These failures are not counted
as passing. No Part/Assembly spatial-frame or 3D dimension-renderer implementation
was changed in this task.


### Dimension follow-up, 2026-09-20

New two-point Drawing dimensions use Sketcher's `classify_linear_dimension`
during placement: the cursor chooses horizontal, vertical or direct projected
distance. Live movement can switch repeatedly before the placement click.
Explicit direction choices remain fixed; existing dimensions, chain extensions,
line/tangent bindings and transferred model dimensions retain their semantics.
The chosen direction persists in the native drawing. GUI regressions verify all
three measured values at view scale 2 and save/reopen.

Double-clicking an existing Drawing dimension now retains its cyan selection
while Properties is open and after OK or Cancel, including tree synchronization.
The older selected-text test inspected a region that excluded most of the small
antialiased glyphs; its region now covers the glyphs beside the label handle,
excludes the support line and checks cyan hue with partial pixel coverage.

The title-block master-dimension failure recorded above is fixed. Decorative
circles no longer disable the simultaneous rectilinear equation seed; unsupported
nonlinear constraints still use the general solver. Tests cover 10 → 12 → 8 mm,
EqualLength rows, unchanged circle radii, native save/reopen and transactional
rejection with a fixed conflicting point. The persistence test now expects the
current Drawing INI version 19. The unrelated 3D text-clearance issue is unchanged
at the user's request.


### Running chain and break-mark follow-up, 2026-09-20

The user-provided `screenshots/01.png` defines Drawing chain presentation:
all ordinates are measured from the original attachment identified by
`anchor_attachment`, with one shared dimension line, one arrow at each target,
and labels perpendicular to the dimension line, beside each target on the side
opposite its witness/reference geometry. The datum has a filled dot and a
literal `0`, including when a common prefix, suffix or tolerance is configured.
For targets at 10, 25 and 40 mm the labels are 10, 25 and 40, rather than the
successive intervals 10, 15 and 15. Transferred model dimensions are unchanged.

Adding a target at either end preserves the datum and existing segment IDs.
Moving the line through any arrow grip or its placement field moves the shared
line for every target. Labels stay above their witness line with a fixed paper
gap and slide only along that line; the witness extends below the text. Each
branch draws its own connection back to the common datum. Native save,
reopen, unresolved-reference caches, selection and PDF/DXF consume the same
ordinate presentations. The screen and exports share one renderer, including
the explicit zero. Evidence: `build/drawing-chain-proof.png`, `.drwz`, `.pdf`
and `.dxf`; the GUI regression checks DXF text values 0, 10, 25 and 40.

The chain remains one Drawing object. Its stable segment IDs identify selectable
branches in the View and child rows in the Tree. Delete on a branch removes only
that target, preserving the datum, other values and segment identities. Deleting
the last branch retains the standalone zero. Select Parent selects the complete chain for
whole-object deletion. These changes are one Undo transaction. Removing the first
target preserves an automatically established measuring axis in the native
Drawing's optional `chain_direction` field; it never changes the Part or Assembly.
Creation starts with the datum only: select a straight edge and place zero, or
select a datum point and a second point defining the measuring direction, then
place zero. The direction point is not a measured branch. Point-based creation
preserves the existing cursor-driven horizontal/vertical/direct placement choice;
explicit direction choices in Properties remain fixed. Add branch accepts
the next point or compatible edge without repositioning the established chain.
Native `chain_datum_only` records retain the direction inputs and datum layout;
their internal presentation slot is not exposed as a branch in the View or Tree.
Regression coverage includes branch deletion on either side of the datum, text
constraints, automatic-axis preservation, native persistence and Undo.
The follow-up passed ten targeted contracts plus the main Drawing workspace
check. The measurement GUI contract also switches through Czech, English,
German, French and Russian and verifies the Add branch control in each language.
The standalone datum proof is `build/drawing-chain-zero-proof.png`.

Zigzag break marks now use 20-degree included angles at their two sharp corners.
The existing paper amplitude and gap are retained. Tests measure the angle for
horizontal and vertical breaks at multiple scales.

Final follow-up validation: 13/13 targeted contracts pass in
`build/drawing-chain-final-tests.log`, including the complete Drawing GUI,
measurement GUI, Sketcher, title-block, view breaks and PDF/DXF command checks.
The root launcher's main-workspace Drawing check also passes in
`build/drawing-chain-workspace.log`. The native application has been rebuilt.


### Drawing dimension conventions, 2026-09-20

Millimetres are implicit in Drawing dimension labels. New local length dimensions
have an empty unit suffix; existing local and transferred model dimensions omit
an exact `mm` suffix at presentation time. Model units, values, angular degree
symbols, prefixes, tolerances, non-millimetre suffixes and explicit text overrides
are retained. Screen, PDF and DXF use this same Drawing-only convention.

A linear dimension's context menu now offers `Převést na řetězovou kótu…`.
Conversion opens the shared Properties dialog using the two existing references
and commits only with OK; it never starts entry for an additional reference.
Adding references is a separate action on an existing chain. Undo restores the
original linear dimension.

Chain label baselines are perpendicular to their common dimension line, with
readable text orientation. Their full text bounds stay outside the line, on the
side opposite the witness geometry, for either side of vertical, horizontal and
oblique dimensions. The zero follows the same rule.

For in-plane dimensions with text between the arrows, text width does not force
the label outside. The chosen label centre determines placement, even when the
text overlaps the arrows. The shared drawing/model presentation preserves this
choice; users decide whether the resulting spacing is acceptable.

Drawing conventions validation: all five core/export tests and four GUI tests
pass (`build/drawing-conventions-core-tests.log` and
`build/drawing-conventions-ui-tests.log`). These include direct context-menu
conversion with two existing references and Undo, both text sides at three
orientations, hidden millimetres and retained tolerance text. The native root
launcher has been rebuilt.


## Entity selection and deletion

A normal left click selects one drawing entity. Ctrl+left click adds or removes
an entity from the selection; clicking empty paper clears the selection.
Selected entities are cyan. Delete and the selection context menu remove the
same set in one document history transaction, so one Undo restores the set.
This applies to manual dimensions, displayed model dimensions, axes,
construction geometry, free text, balloons, captions, section labels, cutting
traces and views. Removing a model annotation hides its drawing representation;
it does not delete the source Part geometry. Removing a view also removes its
dependent projected views and attached dimensions/balloons, using the existing
view deletion contract. Title-block fields remain part of their dedicated editor.
Selection is unavailable while an editing command owns input.

Caption and section-label grips snap to the same view-local helper rectangles
as dimensions. Cutting-trace end grips snap where their permitted movement line
intersects a helper rectangle. End direction and minimum extension are preserved.
The six-screen-pixel tolerance remains stable while zooming. Snapping changes
stored paper placement only and never recalculates source geometry.


## Partial views and local presentation of model sections

View Properties → Partial view → Crop view starts boundary drawing on the
existing drawing canvas. No additional properties window opens. Select an
anchor point on the view geometry, then define a circle radius or ellipse
half-width/half-height. For a closed spline, place at least three interpolation
points and click the first point or press Enter. The periodic cubic spline
passes through the points and closes smoothly. Esc cancels boundary entry.

Edit boundary exposes draggable points and an anchor that moves the complete
boundary. Enter returns to View Properties; only its OK commits the changes.
Cancel leaves the original document unchanged. Remove crop restores the full
view and participates in the same document Undo/Redo history.

The same boundary applies to a normal projection and to a previously calculated
model Section view such as A–A. It clips projected body edges, shading and
hatching, not the source model. It does not create a new section or define a
section depth. Dimensions, view captions and A–A labels remain independently
positionable outside the crop. The boundary is stored in the native `.drwz` in
displayed view coordinates, before paper scale; moving the view on the sheet
moves its boundary too. Its initial anchor is picked on stored projection
geometry; it is a drawing placement, not a persistent topology attachment.
A changed projection may therefore require editing the crop boundary.

The screen, PDF and raster exports share the same clipping path. DXF applies
that path to exported strokes and fills as well. Crop entry and grip movement
consume already calculated projection data and do not call OCCT.

### Detail views and local hatch boundaries

Insert Detail follows Insert View in the Drawing toolbar. Pick a point in the
source view, define a circle, ellipse or closed spline, then place the enlarged
view. The shared creation/edit properties window controls name, scale, caption,
source boundary and source label. Names start with X, Y, Z, then X1, Y1, Z1.
Only OK commits; Cancel discards the complete preview. A detail inherits its
parent's calculated geometry, visibility, model annotations, section, breaks and
crop. Regeneration refreshes parents before details; deleting a parent also
deletes its dependent details. Native Drawing version 20 persists these links.
Boundary anchors remain view coordinates, so a source orientation change can
require repositioning the boundary.

The source detail reference uses the same yellow as dimensions on screen and
thin continuous lines: a boundary, an attached leader and a horizontal underline
with a white, 5 mm high detail letter above it, without a balloon. The enlarged
view caption remains white. Hover and selection use the common orange/cyan feedback; print,
PDF and DXF output use black. Drag the reference letter or underline grip to
move the label while its leader follows the nearest point on the boundary.
The source area does not move. Esc cancels a pending drag; release commits one
Undo/Redo step. Position is stored in the Drawing relative to the source view,
so moving the source moves its reference and moving the enlarged detail does
not. Renaming the detail updates the source letter automatically.

This simple presentation follows the configurable leader-note concept in
[Creo detail options](https://support.ptc.com/help/creo/creo_pma/r12/usascii/detail/detail_options.html),
where the default detail boundary line style is continuous. Yellow is a ZIMA
screen convention, not a prescribed print color.

The section table in View Properties has a separate hatch-region action. Its
circle, ellipse or spline limits only hatch strokes. It leaves material contours
and the Part/Assembly Section definition unchanged. Each section's local region
is saved in the Drawing. Finished view-crop and local hatch-region boundaries use thin strokes only
where they cross the projected body; the complete boundary remains visible
during editing.

The interaction reference is PTC's
[Insert a Detailed View](https://support.ptc.com/help/creo/creo_pma/r12/usascii/detail/To_Insert_a_Detailed_View.html):
source point, boundary, sheet location, then name/scale presentation. ZIMA keeps
its shared OK/Cancel transaction instead of introducing an Apply action.

### Threads, dimensions and tolerance presentation

During dimension entry, the hovered attachment point uses the same filled orange
marker as a Sketcher point (5 px radius with a 2 px outline, in screen space).

Axial thread views suppress spurious cone/cylinder mesh generators. Thread
lead-in chamfer circles are hidden by default; View Properties can reveal them.
The chamfer stays in the model. Diameter dimensioning accepts symbolic thread
boundaries in axial, hidden-line and section projections and displays the
stored thread designation, including external threads. Measurement metadata is
native Drawing data and remains available after reopening.

Projection resolves analytic cone/cylinder data from persisted original face
references as well as live display references. This preserves lead-in detection,
axial seam suppression and the complete thread arc after reopening a Part,
without calling OCCT. A thread's own cone facets do not hide its axial bore rim.
Previously saved Drawing projections require explicit regeneration to replace
their stored line geometry.

Show/Erase collects primitive parameter and embedded Sketch dimensions from
the source document. Linear dimension presentation rotates into the drawing
plane without modifying model witnesses, values or layouts.
Generated model-layout text is reformatted with implicit millimetres on the
sheet, including Box parameter dimensions. Authored text overrides remain
literal; model text and units are not modified.

Global Settings offers inline or stacked upper/lower deviations, shared by
Part, Assembly, Drawing and exports. Stacked deviations use 75% of the nominal
text size;
the stacked lower deviation shares the nominal baseline and decimal separators
align. Literal text overrides and symmetric tolerances keep their existing
presentation. The saved setting is `Dimensions/ToleranceLayout=inline|stacked`.
Painting, picking, decimal alignment and the text background all use the same
reduced tolerance size. The Drawing Tree uses a simple yellow dimension icon for
individual dimensions and their group, an axis icon for the Axes group, and
distinct sheet/view icons for the hierarchy.
The Windows workspace verification paints the canvas before querying annotation
handles, so an obscured test window still exercises Tree/View selection,
dimension properties, Ctrl axis selection, deletion and Undo deterministically.
Stacked text has a larger hit box and background than plain values. The standard
3.5 mm text and its background fit within the 8 mm guide spacing in the layout
regression check. Spacing is not automatically increased, so dense drawings or
larger text can still require manual placement.
Inline deviations are an optional compact presentation, not a claim of standard
conformity. The relevant presentation standard is
[ISO 129-1:2018](https://www.iso.org/standard/64007.html), whose scope covers 2D
dimensions and tolerances and can also apply to 3D annotation.
