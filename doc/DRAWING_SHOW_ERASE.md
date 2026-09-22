# Show/Erase for Drawings (2026-09-09)

Status: implemented in the C++ Drawing workspace. Manual measurement dimensions
use the separate Dimension command; its references and shared properties are
covered by [Drawing dimensions](DRAWING_DIMENSIONS_DESIGN.md).

## Inputs, means and outputs

The input is a selected Drawing view and original dimensions, axes and auxiliary
geometry from its source Part/Assembly. The means are persisted ZIMA identities
and reference geometry, camera projection and common View selection. Opening
the tool, hovering or changing a filter must not invoke OCCT. The output is a
Drawing-owned list of visible items and presentation positions, saved in `.drwz`.

## Initial scope

- One internal Show/Erase dialog with OK/Cancel.
- Show/Erase modes and filters for original model dimensions, axes and auxiliary
  geometry. Additional annotation types can use the same data/selection contract.
- Select items to keep or remove from the offered preview. Show offers hidden
  candidates; Erase offers visible ones.
- Preview is transient. OK saves visibility; Cancel restores the input state.
- Double-clicking a driving dimension value edits its original model parameter
  through the shared inline editor; see the value-editing contract below.

## Identity and persistence

Each item references its source document, owner, stable dimension/geometry ID
and exact occurrence path in an Assembly. Visibility and movement are stored
separately per Drawing view. Coordinate copies cannot replace source identity.
Regenerate refreshes references from current open sources; tab switching does
not recalculate the parent.

Parametric dimension presentation is stored in its spatial basis in model
millimeters; Drawing overrides belong to one view. Moving presentation changes
neither dimension values nor source Sketch constraints. A missing source is
marked unresolved; other geometry must not silently replace it.

## Construction geometry and axes

Construction role is independent of curve type. A finite segment stays finite,
an arc retains its range and a circle/closed curve retains its shape. Switching
to construction preserves identity, points, dimensions and constraints;
chain-line rendering and exclusion from a body profile do not redefine the
curve. An infinite axis/line remains a separate reference kind, not a property
of all construction segments. Show/Erase offers finite construction geometry
and axes; cylindrical axes use the stored cylinder extent.

Origin axes, Sketch coordinate-basis axes and primitive coordinate-basis axes
(`axis:x`, `axis:y`, `axis:z`) are excluded from Drawing annotations. Previously
saved visible basis axes are also suppressed on load and in the shared painter.
Authored construction axes, hole axes and centerlines remain available.

## Handles and guides

Hover highlights the entity and its handle; LMB confirms the common candidate.
After confirmation, text and handles at arrow/extension-line junctions can move.
Individual handle positions should survive reopening, scale changes and
regeneration. The view itself retains a rectangular area.

View Properties exposes dimension working guides: enabled state, first-line
distance from the 2D projection bounds and subsequent spacing in paper mm. Guides are subtle gray
dashes like hidden edges. They are not Drawing geometry and do not print or
export to PDF. Agreed defaults are 8 mm for both first distance and spacing.

## Verification before completion

Switching an auxiliary circle must preserve ID, center, radius, dimensions and
constraints; chain-line display must not affect the solver. Construction still
excludes a curve from the solid profile. Verify independent visibility in two
views, repeated Assembly occurrences, preview/Cancel, position persistence,
source changes on explicit regeneration and no working guides in PDF.

## Implemented foundation (2026-09-09)

`ModelAnnotation` stores source document, owner, semantic ID and exact occurrence.
Each `DrawingView` owns a separate list in `.drwz`. Projected geometry uses model
units. A parametric dimension also stores original spatial geometry, frame,
model presentation and an optional local view override.

`refresh_model_annotations` accepts explicitly supplied ZIMA packets of
dimensions, axes and auxiliary curves. It preserves visibility/positions by
identity, marks missing items unresolved and leaves the input unchanged on
failure. It neither recalculates the model nor calls OCCT. Repeated occurrences
are distinguished by identity, not names/order. Finite auxiliary curves retain
their extent in projection; source points and construction role do not change.

`ShowEraseSession` supplies candidates and a preview copy for Show/Erase, type
filters and keep/remove selection. The view is unchanged until the caller
commits the preview. Foreign or unoffered identities are rejected.

`zima_cpp_show_erase_contract_tests` covers two views, repeated occurrences,
an auxiliary circle, noncommitting preview, source disappearance/return,
preserved paper positions after scale/regeneration, atomic duplicate rejection
and `.drwz` round-trip. **Show / Erase…** is in the Drawing toolbar. Its dialog
uses the shared internal Properties window positioned on the right. View
selection shares the annotation list with hover and RMB cycling. Empty clicks
clear confirmed selection.

## Usage

1. Select a Drawing view. Choose **Regenerate** to load new source items; a new
   view loads them when Properties is confirmed with OK.
2. Open **Show / Erase…**. Show offers hidden items, Erase visible ones. Filters
   separate original Sketch dimensions, axes and finite auxiliary geometry.
   Collection also includes dimensions in source viewer packets and stored body
   axes (such as a cylinder axis); it creates no manually measured dimensions.
3. Select items to keep or remove. View clicks and list checkboxes toggle the
   same item. Offered hidden items are gray; retained items use normal display.
   RMB cycles overlapping candidates.
4. OK commits visibility; Cancel restores the initial state. MMB double-click
   also invokes OK over the canvas; short MMB does not confirm. The initial
   single-view scope was later extended to all pending views (see below).
5. After closing the tool, select a dimension and drag its text or arrow handle.
   A linear dimension retains its direction when offset changes; an angular
   dimension changes arc radius. Source Sketch values/constraints do not change.

Enable **Dimension working guides** in View Properties. Guides are rectangles
aligned with the view's horizontal and vertical axes around its actual displayed
projection. Geometric bounds and working offsets are separate: moving a
dimension does not enlarge the model. First distance and spacing are **8 mm on
paper** by default. Model and Drawing-created dimensions share these snap
segments. Model dimensions retain their original spatial measurement and
references. Working frames are excluded from print, PDF and DXF.

The initial axial-view marker was a 6 mm cross with a center point; the later
radius-based sizing rule below supersedes that fixed span. Each hole owns its
marker. Side views retain the stored cylinder length with a 2 mm extension at
each end and a midpoint. Auxiliary segments/arcs never become infinite axes.
Rendering consumes stored projection; opening the tool, filtering and switching
tabs do not recalculate. Explicit regeneration uses open sources or saved files
and distinguishes every nested Assembly level and repeated Part occurrence.
Mirrored/patterned components use persisted Mirror/Pattern definitions and
exact paths; their annotations move with the derived occurrences.

`zima_cpp_show_erase_ui_contract` covers a real Sketch, nested rotated Assembly,
View clicks, OK/Cancel, MMB, text movement, `.drwz` and identical print rendering
when guides toggle. PDF uses the same drawing path.

## Dimension orientation and view isolation

Parametric dimensions remain available in oblique/side views. Spatial measuring
points, arms and stored text positions are projected; numerical values are not
recomputed from paper lengths. Camera changes refresh projection. Paper handles
from a different orientation are not reused.

After selecting a dimension, RMB → **Dimension Properties…** edits its plane
around the measurement direction, envelope side, offset and text movement. The
two measured arms determine an angular dimension's plane, so its plane selector
is disabled. It stays at the measuring axis/pivot and changes arc radius.

Default presentation comes from the Part/Assembly. A Drawing saves changes as
that view's own override, affecting neither source nor other views. Regeneration
updates value/geometry and preserves local presentation. Properties uses the
shared internal OK/Cancel window.

During the command, candidates are restricted to the edited Drawing view.
Clicking the same source dimension in another view cannot add it. Regressions
cover this isolation, front/reverse/oblique/side views, rotated subassembly,
normal persistence and identical filtering in View and PDF print rendering.

Assembly annotation collection separates actual source items from auxiliary
references in the conversion scene. It avoids duplicate axes and preserves
transformed dimension text positions. Tests cover full packet projection, not
only individual source coordinates.

## Interaction and exports (2026-09-10)

Show/Erase has an eye-and-dimension icon. Both orders work: select view →
Show/Erase, or Show/Erase → select view. The window opens immediately. Its top
reference field identifies the target view; clicking arms the green outline,
and selecting another view replaces the target. Short MMB ends reference entry
without deleting its value or confirming. SHOW/ERASE buttons mark the active
mode green. Initially, changing targets discarded the previous view's pending
selection and OK committed only the current view; the later multi-view update
below supersedes that behavior. OK is disabled without a target; the tool is
unavailable when no views exist.

Part Drawings offer original Part dimensions. Assembly Drawings collect axes
from inserted Parts and Assembly-owned dimensions, such as placement angle;
inserted Parts' Sketch dimensions are not propagated. Transforms respect exact
occurrence, nesting, Mirror and Pattern; collection performs no geometry calculation.

After confirmed selection, purple handles mark movable view/section labels and
dimensions. Fixed title-block items, axes and auxiliary geometry retain ordinary
highlighting without purple handles. An axis center point is a printed mark,
not a drag handle.

**Fit View** centers the paper and fits its height with a 24 px margin. In a
narrow window, paper width may extend beyond the View.

**File → Save As → DXF – current sheet** saves the active sheet in mm, including
format, title block, text, geometry and visible annotations. It uses shared
print rendering without selection, previews or working guides. Text stays text;
curves become segments, fills become HATCH/SOLID. Embedded images and shading
become individual color fills, requiring no image sidecars. Export changes
neither the document nor its path.

**File → Save As → JPEG – current view** saves the current View in Part,
Assembly and Drawing, retaining crop, orientation, zoom and visible state,
including selection. Resolution matches framebuffer/canvas; surrounding panels
are excluded. Drawing JPEG preserves the working screen appearance, while
DXF/PDF uses print rendering. JPEG quality is 95.

## Shared spatial frame (2026-09-10)

`ModelEnvelope` carries a local origin, three axes and geometric bounds in that
basis. Orientation follows solved object placement, independent of the camera.
Frames come from persisted viewer/reference packets without OCCT traversal.
They apply to history features, construction objects, Sketches and bodies.
A Sketch may have zero thickness, a point zero size and an axis only length;
a frame does not require volume. Without calculated geometry, a local basis
exists but dimensions are not invented.

Calculated component snapshots retain frames; the Assembly transforms origins
and directions through exact occurrence paths. Copy frames follow Mirror/Pattern.
This establishes data for large Assembly hierarchies; it introduces neither a
new spatial index nor rendering optimization.

In Part/Assembly, **View → Dimension spatial frame** shows the selected object's
working box. Parametric dimensions default to an 8 model-mm offset. Their
context menu includes **Dimension Properties…**. Purple text handles move text,
linear handles change offset and angular handles change radius. Dragging saves
presentation on release; Esc cancels. Measuring references, values, constraints
and model geometry remain unchanged.

Drawing purple handles belong to movable annotations, including section arrow
ends. Geometry fixed to a view receives no independent handle.

`zima_cpp_dimension_layout_contract_tests` checks local dimensions of a rotated
box, repeated occurrences, mirroring, measurement/presentation separation,
Part/Assembly persistence and independent Drawing view overrides. It also checks
real 3D drag events, cancellation and shared Properties.

## Plane, handle and axis-size fixes (2026-09-10)

Radius/diameter retain the actual circle plane, including Sketches in a rotated
Body and fillet previews. Perpendicular projection-plane switching applies to
linear dimensions; radial dimensions cannot tilt out of their circle plane.

Sketcher uses the same purple handles as Part/Assembly: at arrows and the middle
of the text shelf. Handles draw above geometry points and remain visible when
coincident. Presentation is stored directly in the Sketch. In an embedded
profile draft it belongs to that draft; standalone Sketch editing saves it with
Sketch changes. Canceling subsequently opened Properties does not undo already
saved Sketch changes. Sketch serialization preserves presentation for Drawings.

Axis display length does not enlarge its owner's existing geometric box. Only
a standalone axis without a geometric envelope derives bounds from its length.
Drawing axes use actual projected minima/maxima of their auxiliary box, extended
2 mm on paper at each end. Extents are not reflected about the origin: 0–40 mm
at 1:1 gives −2–42 mm, not −42–42 mm. This also applies to main axes. An axial
view produces a cross spanning those bounds; other directions produce a segment.

### Free shortened-radius text position (2026-09-10)

In radius mode without a line to the center, the leader starts at the measured
arc's arrow and ends at the text shelf. The purple shelf-center handle moves
continuously between arc and center, through the center beyond the axis, or
outside the arc. Text position changes neither arrow measurement point nor
radius value. In isometric views, text/shelf remain horizontal. Sketcher, Part,
Assembly and Drawing share this rendering.

Switching shortened-radius mode preserves circle plane and radial direction of
the arrow/leader. Any stored text component perpendicular to the plane is
removed. Dragging gives a signed position along the projected radius without
inside/outside restrictions; the horizontal isometric shelf attaches to it.

### Top text layer for dimensions (2026-09-10)

Dimension values draw in a separate layer above geometry, axes, extension lines
and hatching. A borderless mask surrounds the entire value, including R/Ø and
unit, with 0.5 mm padding on each side in Drawings. It uses background color on
screen and paper color in print/PDF. 3D View converts padding using screen DPI.

Text follows stable stored dimension order; later text/masks cover earlier ones.
Purple selection handles remain above the text layer.

### Multiple views, hole crosses and dimension interaction (2026-09-10)

Short MMB in Show/Erase ends item selection for the current view and arms the
next-view field. Pending visibility remains in preview when switching views.
Clicking the field also changes the target directly. If the field is already
armed, short MMB ends reference entry and restores item selection in that view.

OK or MMB double-click commits all pending views together and closes. Cancel
discards every view's changes. Switching views, short clicks and MMB dragging
never commit intermediate changes. There is no Apply. The same double-click
over the Drawing canvas confirms View/Sheet Properties through shared
`PropertiesSubWindow` behavior.

An axial cylinder marker has four arms 90° apart and a center point. All arms
belong to one reference: selection, SHOW and ERASE affect the whole cross.
Two holes have separate crosses. Circular Extrusion profile axes use the
individual profile radius, not the full Extrusion width. For a Ø10mm hole at
1:1, each arm extends 7mm from center: 5mm radius plus 2mm paper extension.
Envelope/directions follow the exact Assembly occurrence, Mirror and Pattern.

A Sketch axis inherits the actual Sketch curve/point envelope before Drawing
annotation selection; its working 100mm display length does not enlarge it.
The list distinguishes Sketch, Origin and cylinder axes. **Regenerate** loads
changed source axis/plane data into an existing saved Drawing.

Dimension text uses `10mm`, `R10mm`, `Ø10mm`. Explicit custom text is preserved.
View, Drawing and export share this format.

Dragging shares screen-to-dimension-plane conversion. For an edge-on plane,
movement along its visible direction remains possible. A dimension hides only
when its measuring line itself projects to a point. Shortened-radius handles
remain draggable after switching mode with RMB. Drawing handles modify only
that view's presentation; model dimensions remain unchanged.

Regressions cover individual arms of two hole crosses, XZ Sketch normals,
repeated text/arrow-handle dragging, RMB cycling, combined OK/Cancel across two
views and MMB confirmation of Sheet/View Properties. Optional
`ZIMA_TEST_ANNOTATION_PART` checks a reference Part with two Ø10mm holes in XZ
and its neighboring `.drwz`; files are read only and interaction uses a working
copy of the document.

## Shared properties and radius (2026-09-10)

Dimension Properties combines text, tolerances and placement. Drawing overrides
remain view-local. The existing text point moves the shelf; the radius arrow
point rotates presentation along the circle in its plane. Three RMB modes
follow `koty.bmp` and share the Sketcher/3D View implementation.

## Driving value editing (2026-09-20)

The Czech command label is **Zobrazit / skrýt kóty**. Its visibility dialog
continues to offer dimensions, axes and construction geometry.

Double-click the numeric text of a visible model dimension to open the same
inline numeric field used in Part and Assembly. Enter or finishing editing
confirms; Escape cancels. There is no additional OK button. Arithmetic and
comma decimal separators use the shared numeric-expression parser. Invalid
expressions remain available for correction. Context-menu Properties continues
to edit Drawing presentation only.

The edit resolves the original source document and exact occurrence path.
Already open source documents are authoritative; an unopened native source is
loaded without switching the displayed Drawing. The current source binding
must still be writable. Measured, unresolved, locked and nonnumeric catalog
annotations cannot be edited by this numeric field.

An explicit confirmation calculates a private source draft and reprojects all
views in the current Drawing. Only a successful calculation and projection
publish the source and Drawing together. Rejection leaves their previous data
and histories intact. Source edits remain unsaved model changes until Save. Saving the Drawing also
writes the source models edited through it, before writing the Drawing itself.
The complete current source state is saved, including any other pending edits
in those models. Unrelated dirty documents are excluded. Source and Drawing
retain their existing document-owned Undo histories.

## Annotation tree

Each view below a sheet exposes nonempty **Kóty**, **Osy** and
**Konstrukční geometrie** groups. Visible model annotations and manual Drawing
dimensions appear as leaves. Hidden Show/Erase candidates do not appear as
inserted elements. Leaf identity includes its owning view and the complete
model reference, so repeated occurrences and repeated projections remain
independent. Tree selection highlights that annotation; View confirmation
selects its Tree leaf, and an empty View click clears both selections.

## Value-editing verification

The Drawing Show/Erase GUI contract exercises a real extruded Sketch: double
click, Escape, arithmetic input, source dimension and solid recalculation,
refresh in two views, invalid values, stale source locks, failed-projection
rollback, and native source/Drawing persistence. The main-workspace Drawing
verification checks annotation groups and bidirectional Tree/View selection.

## View-scoped annotation snapping

View Properties exposes a strictly positive **Odsazení přichytávání [mm]**
value and a positive guide spacing, both in paper millimetres. Existing guide
settings remain the sole persisted values. During dimension text/grip movement,
manual dimension placement and balloon placement/center dragging, the owning
view's 2D rectangular guides appear automatically. The visibility checkbox
controls whether guides also remain visible while idle.

One shared geometry function produces both rendered guide segments and snap
candidates. Snapping uses a six-logical-pixel screen tolerance and stores the
resulting view-relative presentation position. A green highlighted segment
identifies an actual snap without an extra diamond marker; releasing or canceling removes this feedback.
Candidates always come from the exact owning view. Other views, sheets and free
text objects do not contribute snap candidates. Source geometry and parameter
values are unaffected. Every view uses its displayed projection bounds for model
dimensions, manual dimensions and balloons; projected 3D envelopes are not used.
The Part/Assembly spatial frame remains unchanged.

The model dimension and balloon GUI contracts check the feedback during dragging
and verify that the final grip matches the indicated snap position.
