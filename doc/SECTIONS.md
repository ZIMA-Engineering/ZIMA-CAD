# Part, Assembly and Drawing sections

## Creation and editing

**Sections…** above the View opens Section Properties for a Part/Assembly in its
own tab. It uses the shared **Container Placement** panel: coordinates, orientation,
references and offsets. Shared placement calculation is unchanged. References are
saved original model objects.

1. Set section-container placement. The document Origin is visible during entry.
   Clicking the complete tree Origin fills three placement planes; **Origin** also
   makes Body/container Origins available.
2. Choose its local sketch plane **XY, XZ or YZ**.
3. **Sketch…** opens normal Sketcher. Draw one open segment or connected polyline,
   using constraints, dimensions and point dragging as needed.
4. **Finish sketch** returns the pending sketch to Section Properties.
   The Sketcher has no separate **Cancel section sketch** action. Use Properties
   **Cancel** to discard the entire pending Section edit.
5. Set cut side, section-plane visibility and component modes. **OK** saves the
   whole section in one reversible transaction.

The line extends infinitely at both ends. The cutting surface runs perpendicular
to the sketch plane through the whole model; a polyline produces a corresponding
bent surface. Default retained side is left when traversing from the first drawn
segment. **Reverse cut side** keeps the other portion. Closed, branched or disconnected
profiles and crossings of the line or extended ends are invalid. Section geometry
consists of straight segments.

The **Sections** group immediately follows the document Origin. Its first item,
**No section**, restores the full model. Neither group nor default item is removable.
A–A, B–B, etc. follow and can be renamed. Context menus offer **Active**, Properties,
Sketch editing and removal. Despite appearing near the tree top, sections are outside
body-creation history.

The context menu separates **Properties**, **Rename…**, and **Edit**. Rename changes
only the stored name without evaluating the cut. Edit shows the Section sketch
and editable dimensions directly in View, without opening Properties or Sketcher.
Each accepted inline dimension change is one undoable Section transaction and
does not recalculate body geometry. Sketch geometry editing remains available
through **Sketch…** in Properties.
An incomplete sketch may also return to Properties, so the draft can be cancelled.

The group starts collapsed when creating or opening a Part or Assembly. Manual
expansion is retained across scene refreshes in the current document session.
Starting Section Properties clears prior object inspection. A new Section Sketch
can project original geometry from the complete model before the Section is saved;
these references remain in the dialog draft until OK.

Creation/editing share one internal **OK / Cancel** dialog. Preview and Sketcher
changes stay transient until Section Properties OK. Cancelling Properties also
discards already finished sketch edits. Sketching Undo/Redo affects only the pending
sketch. MMB double-click over View confirms Properties; short click/navigation drag
does not.

## Display and Assemblies

Default document state is **No section**. The tree has no checkboxes. Opening an
section leaves the whole body visible, even when that Section is active.
**Show section plane** draws the cutting sketch, intersection outline and configured hatching as overlays visible
through solids, without auxiliary axes, references, midpoint markers or clipping.
The cutting sketch is white, matching Sketch Properties; hatching retains its
existing presentation.
The Tree menu toggles between **Show section plane** and **Hide section plane**;
it shares the persisted visibility flag with Properties and supports Undo/Redo.
**Active** on A–A or **Active
section** in Properties enables clipped display after confirming and closing the
dialog. At most one is active per document. Properties and inline Section editing
temporarily suspend clipping and always show the sketch. Inline editing preserves
the saved plane-visibility flag; leaving editing restores active clipping.
The plane-visibility checkbox also works while the section is active, adding or
removing the sketch, outline and hatching independently of the clipped body. Reference geometry remains
available while explicitly entering placement references.
Modeling Properties and ordinary sketch editing use full geometry.

Section display results are cached from the source mesh and Section definition.
Repeated dimension display and plane-visibility changes reuse unchanged results;
changed source geometry or Section geometry invalidates the cached result.

Placement references refresh on explicit source-document regeneration. Missing
references can be repaired in Properties; invalid sections are not calculated.
Tree checks use saved references and support pending Parts without calculated bodies.
Drawing the tree does not calculate; tab switching does not recalculate parent
Assemblies.

Sections are presentation settings. They change neither result bodies, volume, mass,
component source files nor BOM quantities.

Component modes are **Cut + hatch**, **Cut without hatch**, **Uncut**. Part uses
Body identities; Assembly uses complete occurrence paths, including repeated Parts
in nested Assemblies. Selecting a Part in View selects its table row. Multi-row
selection supports common mode, angle, spacing, offset and pattern edits. Direct
editing sets that Body's style without a separate custom-style checkbox. **Flip**
rotates hatching 90°; on multiple rows it rotates each from its own current angle,
preserving alternation. The table ends immediately after its last row.

## Drawing and printing

Select a saved source section under **View Properties → Section**. Drawing view
orientation always controls the camera. Selecting, tilting, reversing or disabling
a section does not change it. Drawing automatically selects the retained side from
view orientation at initial selection, rotation and regeneration. For bent sections,
the dominant projected cut area decides, not line length outside the body. There
is no separate automatic-side toggle. Camera changes do not alter source-section
side in Part/Assembly.

Cut surfaces appear and hatch where visible in that orientation. Exactly edge-on
surfaces have no projected area to hatch. Base and isometric views share the rule.
Bent sections project in their actual spatial positions; segments are not
automatically unfolded into one plane.

Hatching is controlled only in the Body/component table: angle, spacing, offset,
pattern (parallel, cross, dashed) and **Flip**. These belong to the source Part/
Assembly section. Drawing reads them from the model and **OK** commits changes to
its open document; there is no independent view style override. **Cancel** changes
neither model nor Drawing. Save the source model normally afterward. Uncut also
belongs to the model-section definition. Table changes calculate no solid and do
not regenerate parent Assemblies.

Drawing **Cut without hatch** hides only that Body's hatches in that view, preserving
cut faces, edges and 3D settings. Part/Assembly uses the same style for green hatching
on actual 3D cut surfaces, with independent 3D visibility. Drawing spacing/offset
are paper mm regardless of scale; 3D uses model mm. Adjacent components without
custom styles alternate angle by 90°.

Saved projections contain the source section's last calculated state. Later model
changes reach Drawing through explicit **Regenerate**; old local style overrides
cannot supersede them.

If a sheet has a normal view of the same source, the section initially connects to
the first such view with a path, arrows and designation. Hatching respects cavities
and occlusion by other Parts. Linked-path arrows follow the section view's actual
side in preview and PDF. Hatching can remain on in shaded-without-edges mode.

Working hatches are green. PDF exports them as black vector strokes, default
**0.25 mm**. Section paths use yellow thin chain-dashed **0.25 mm** strokes; end/bend
segments, arrows and designations use white **0.5 mm** strokes. Lineweight preview
does not change physical print widths. Export retains saved projection without OCCT.

Drawing **Regenerate** rereads section definitions and calculated source geometry.
Open documents supply current unsaved state. A deleted source section causes an
error without partially replacing Drawing results; select another section or No
section in Properties.

## Calculation and verification

Inputs: saved sketch, container placement, calculated display mesh and component
modes. Outputs: clipped mesh, closed cut faces, projected edges and paper-unit
hatching. Deterministic clipping/hatching uses saved triangulation, so curved surfaces
have display-tessellation accuracy rather than a new analytical B-Rep. Sections
call no OCCT and create no persistent model-face/edge references.

Checks cover a box section area 100 mm², hollow-profile area 84 mm², an L-sectioned
box with volume 250/750 mm³, an offset hollow-profile section with area 108 mm² and
volume 420 mm³, reversed retained side, tangent cuts without false faces, exact
omission of repeated occurrences, and constant hatch spacing across scale/oblique
projection. GUI tests cover placement/local sketch plane, normal Sketcher, point
dragging with local Undo/Redo, both cancellation levels, MMB, reopening, tree,
Part/Assembly persistence, Drawing and PDF.

View name and section designation (A–A, for example) have independent **Show view
name** and **Show section designation** settings. Defaults are above the outline,
on two lines when both shown. Drag each label independently by its LMB handle.
Positions are paper mm relative to the view, saved across reopen/regeneration and
used in PDF. Moving a view moves labels; label dragging does not change the model.

**Orientation** chooses a fixed standard view. Adjacent **Left / Right / Up / Down
90°** quarter-turns rotate the current camera, including isometric. Derived projected
views inherit the actual parent camera and sheet projection method. Custom cameras
are saved. The parent controls derived orientation; sections affect neither camera.
Explicit regeneration evaluates parent-before-child even when file order is reversed.
Document switching alone does not recalculate Drawing projections.

Every View Properties dialog has **Show section paths**. Check which paths appear
on that view, independently of section clipping and A–A designation. New sections
automatically offer their path on the first source view; this table controls later
hiding. A planar section trace also appears in side views where its sketch line
projects to a point. There the whole infinitely extruded cutting plane projects,
with the same arrows, handles, outline clearance and print widths as top/bottom views.

The path is thin chain-dashed across view bounds. Ends/significant bends have short
thick segments; ends have filled arrows and individual letters (A, A). Marker lengths
and font sizes are scale-independent paper sizes. A letters and A–A are white,
**5 mm** high in View; normal view names are green. Text prints black on white
paper/PDF. Thick/thin widths follow sheet settings, default **0.5 / 0.25 mm**.
Explicit regeneration updates arrow position/direction from current source sections.

Each arrow letter stays outside its endpoint. Path rotation changes its position,
but text stays horizontal/upright. Placement respects clearance from model outlines,
paths, arrows and previously placed letters. Collisions move it farther on the same
side. Thick bend segments are at most 3 mm on each branch and stop before the next bend.

## Drawing handles

View names, section designations, dimensions and section-path ends have handles.
Hover highlights entity/point orange; confirmation makes the entity azure and point
purple. Handles are filled dots like normal View points, radius 4.5 logical pixels.
Drag text/dimensions by handles, not arbitrary text positions. Views retain rectangular-
region interaction. Before confirmation, RMB cycles the common annotation candidate
list; afterward it opens context menus.

A section-end handle sits at the arrow/line junction and moves the end marker along
the path; edit source sketches to change the actual cut. Marker positions are paper
mm, constrained not to pass adjacent bends. Letters move with markers and stay
horizontal/readable. View paths are yellow (0.25 mm); thick portions/designations
white (0.5 mm). Handles are working aids and never print.

Sketcher segments, circles, arcs, ellipses, elliptical arcs and B-splines can become
construction geometry through context menus. They display chain-dashed and do not
enter body profiles, retaining original extent, points, dimensions and constraints.

## Section window and tree controls

**New section…** appears only on the main **Sections** folder context menu.
**No section** offers activation; individual sections offer activation, Properties,
Sketch editing and removal. The larger Properties window uses up to 1040 pixels
height, bounded by the main window. **Sketch…** uses the shared green Sketcher-entry
style.

Section arrows share Sketcher dimension arrows' slender shape. Half-base-width to
length ratio is shared (about 0.1763); paper arrow size is independent of model scale.
