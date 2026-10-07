# ZIMA-CAD user manual

This manual describes the native C++ application. UI labels follow the selected
application language; documentation is maintained in English.

## Initial setup

A new portable installation offers **System Setup**. Choose a metric or inch
preset, review the working directory and matching Part/Assembly templates, and
optionally register the application for your user account. **OK** saves;
**Cancel** skips the initial offer. Open it again through **Tools > System
Setup**. Updates preserve completion and existing shared configuration. Paper
format and title blocks are selected separately in drawings. See
[System Setup](SYSTEM_SETUP.md).

## Sketch slots and dimension grips

**Slot**, below **Rectangle**, uses three clicks: first arc centre, second arc
centre, then the radius point on the side of the slot. The radius is the
perpendicular distance from the line between the centres. The resulting two
equal semicircles join the straight sides tangentially. Snapping can make the
centres symmetric about a Sketch axis or an oblique construction line.
**Escape** cancels an unfinished slot.

Use the dimension text grip to place radius/diameter labels and their arrows.
Angular labels in Sketches and drawings follow the dimension circle; moving
the label beyond the measured sector extends the arc to the text. Dimension
value edits remain subject to the Sketch's driving constraints. An incompatible
edit or a drag against fixed geometry is rejected without changing the Sketch.

## Document compatibility

Part, Assembly and Drawing use `.prtz`, `.asmz` and `.drwz`. Each native type has
its own format version. Unsupported experimental versions are rejected rather
than silently converted. Backward compatibility with old Part and Assembly
formats is not maintained during development. Required geometry and reference
data live inside native documents; see [cache storage](CACHE_STORAGE.md).

## New documents

**New / F1** offers a contextual chooser beside the document type. For a Part,
choose **Modeling** or **Sheet Metal**. Both create the same ordinary Part from
the configured start template; the choice only selects the working environment
in the application dropdown. **Skeleton** uses Modeling, appends `_skeleton`
to the filename and loads `START_SKELETON.prtz` from the template directory.
It starts with one active empty Body and black surfaces with approximately 70%
transparency. It contains no modeling geometry, material or manufacturing
parameters. Skeleton identity is determined only by the case-insensitive
`*_skeleton.prtz` filename; there is no additional document flag. Creating a
Part with that filename also selects the Skeleton template through the shared
GUI/CLI creation path. Existing Parts are not recolored when opened or renamed.

For a Drawing, choose a frame from the native `.frmz` files in `Paths/Formats`
(normally `config/formats`). Labels include the sheet size and template name;
numeric backup files are excluded. The supported sheet sizes are A4 through A0.
The dialog still asks for the source Part or Assembly before creating the tab.

Creating a Drawing with the Tree's Drawing button on a Part or Assembly starts
with A4. Both creation paths insert the matching frame and a company title block.
The current-language Family row in `ZE-TITLE-BLOCK.tblz` is selected automatically.
The shared `ZE-DRAWING-FRAME.frmz` provides A4 through A0 Family rows.
Missing optional library resources leave the corresponding frame or title block
empty; a missing A4 frame leaves a blank A4 sheet. A corrupt selected template
reports an error before creating a document. Existing drawings are not modified.
Frame, title-block geometry and embedded images are stored inside the new `.drwz`.

## Open and Save dialogs

The selected file-type filter shows the corresponding documents, such as `.prtz`
for Parts. Directories remain available and appear before files; both groups
are initially sorted by ascending name. The reserved application directory
`0000-index` is hidden, case-insensitively. Hiding it does not delete its content.

## Default Part and Assembly templates

New document names can be normalized by the optional global
[document naming policy](DOCUMENT_NAMING.md): uppercase letters, removal of
diacritics and replacement of whitespace with underscores. Existing files and
free text are unchanged unless the user explicitly renames a document.

New documents load real native templates selected in the main configuration:

```ini
[Templates]
Part = START_PART.prtz
Assembly = START_ASSEMBLY.asmz
```

The files normally live in `config/templates`. A new document inherits units,
precision, material, user parameters and relations, and receives a new unique
document ID and the entered name. Start templates intentionally contain no
modeling containers or components, preventing copied internal IDs. A project
`config.ini` may override `[Templates]`; relative names resolve under
`Paths/Templates`.

The Skeleton template is `START_SKELETON.prtz` in the same directory. It stores
no Body IDs: creation assigns a fresh document ID and creates an active Body
using the same origin attachment contract as an ordinary Part. The document
appearance is inherited by subsequently created geometry and can be edited.

## Numeric fields and tables

Numeric fields use the document's **File Settings** precision, including trailing
zeros. Their width accommodates the sign, full value, unit and font. Reference
tables allocate sufficient value width by reducing the reference column.

In **Parameters**, Enter commits the cell and starts editing the next row in the
same column; it does not accept the window. Parameters, Relations, Material and
Family Table variant lists offer one empty row with an entry arrow. Filling it
creates another empty row; a red cross deletes a filled entry. The empty offer is
not saved. The Family Table base row selects original elements or dimensions. Variant rows
override dimensions or presence; double-click a variant name to generate its
own tab. See [Family Table](FAMILY_TABLE.md).

In **Parameters**, select one row with its ordering checkbox and use the bottom
**Up** or **Down** button to move it by one position, as in 3D Curve. The checkbox
stays with the parameter; editing a value does not change this selection.
Moves beyond the ends and selection of the empty offer row are disabled.
Values and language variants follow the parameter. **OK** saves the pending
order; **Cancel** discards it. Saved order supports Undo/Redo and reopening.

3D Curve points, Sweep profiles, thread references and individual face colors use
the same row controls. Removing a station's owned profile leaves its station in
the path, inheriting the preceding profile. The first station needs a replacement.
Overview and point-order tables do not delete geometry.

## Parameters, relations and mass

**Tools → Parameters** shows stored results. **Tools → Relations** belongs to the
source Part or Assembly. Its multiline editor stores a plain-text program inside
the native document. OK validates and stores the source; only **Regenerate / F5**
evaluates it. Opening, saving or changing material does not run relations.
The standard start templates include:

```text
mass = model.mass
```

`model.mass` uses the file's mass unit (`kg`, `g`, `t` or `lb`), calculated body
volume and material `MASS_DENSITY`. Supported density units are `kg/mm^3`,
`kg/m^3`, `g/cm^3` and `lb/in^3`. Without valid density, mass is unavailable.
The result is stored as the ordinary `mass` parameter; title blocks can use
`&document.mass_unit` for its unit. Existing documents retain their own settings;
global defaults initialize new ones. See [physical properties](PHYSICAL_PROPERTIES.md).

The supplied start Part contains S235JR material properties from the library.
Parameter keys are stable English identifiers such as `name`, `standard`,
`drawn_by`, `revision` and `mass`; displayed labels and values may be localized.
For example, `Název` and `Name` refer to the same `name` key.

Write one assignment per line; spaces do not separate commands. For example,
`d6 = 10` assigns a dimension in the document's units, without an `mm` suffix.
Quoted Unicode text is literal; `stock = "⌀" & d1 & "x" & d2` joins text and
formatted values. Operators include `+ - * / % ^`, comparisons and `and/or/not`.
Use `if / elseif / else / endif` blocks for conditions. Functions include ordinary
mathematics and trigonometry; `sin` uses radians and `sind` uses degrees.
Dependencies determine evaluation order. Invalid names, cycles, incompatible
quantities and invalid arithmetic report their source line.

Relations can drive supported editable feature/Sketch dimensions and Pattern
spacing, angles and counts. The editor's arrow selects a View dimension and
inserts its persistent `dN` identifier at the cursor. Driven dimensions display
an `fx` marker. Locked or derived dimensions remain read-only. Regeneration
publishes dimensions, output parameters and optional whole-Part `color` together;
a failed calculation does not publish partial relation results. Geometry-derived
mass, area and volume cannot feed back into driving dimensions.
See [Relations](RELATIONS.md) for complete syntax, limits, colour examples and
transaction behavior, and [Family Table](FAMILY_TABLE.md) for variants.

Drawings do not own these relations. Their **Parameters** action edits the
source Part or Assembly and refreshes relevant displayed parameter/title-block
data. It does not implicitly regenerate parent Assemblies.

## Interface theme

**Settings > General** explicitly selects Light or Dark. Qt controls use Fusion;
the OS theme does not automatically select the application theme. The shared
setting is `Application/Theme=light|dark`, with Light as the default. Confirming
Settings updates the widgets, including toolbars, without restarting. View,
Sketch and Drawing retain their technical ISO font. Portable preferences belong
to installation-root configuration, not immutable version directories.

## About the application

**Help > About ZIMA-CAD** shows the installed version, the embedded
ZIMA-Engineering company logo, and the concept/development credit for
Ing. Vladimír Zima. The email link opens `kontakt@zima-engineering.cz` in the
default mail application; the website link opens
[www.zima-engineering.cz](https://www.zima-engineering.cz). The introductory
text describes ZIMA-CAD as open-source parametric 3D CAD based on the OCCT
geometry kernel and written in C++. It follows the selected application language
(Czech, English, German, French or Russian).

## 3D View controls

| Input | Action |
| --- | --- |
| Middle mouse button (MMB) + movement | Orbit |
| MMB + right mouse button (RMB) + movement | Pan |
| Wheel forward / backward | Zoom out / in |
| Short MMB click | End reference entry or the documented command-local step; never accept a property dialog |
| MMB double-click | Invoke enabled OK in the active internal dialog, including over View |
| F1 / Ctrl+N | New document |
| F2 / Ctrl+O | Open document |
| F3 | Import |
| F4 | Export |
| F5 | Regenerate |
| F6 / Ctrl+S | Save document |
| F7 / Ctrl+Shift+S | Save document as |
| F8 | Rename file |
| F9 / Ctrl+W | Close the active document tab |
| F12 | Set the working directory |
| Ctrl+Shift+C | Toggle the CAD command console |

Panning does not open the context menu. Reset View and standard views animate
the camera transition. Part, Assembly and Drawing use the same wheel direction.
Each document tab retains its own camera; switching tabs does not fit or reset it.

The toolbar above View starts with **Regenerate**, followed by the terminal icon
for the existing CAD console. This order is shared by Part, Assembly and Drawing.
See [CAD console](CAD_CONSOLE.md) for commands and scripting.

## Normal View

1. Activate **Normal View** in the View toolbar.
2. Select a planar original face or reference plane.
3. The camera aligns with it and the command ends.

Press Esc or the tool again to cancel before choosing a reference.

## Reference selection in Properties

All reference-entry controls share two independent states:

- A **green outline** marks the one field receiving input. Click reference text
  to arm it for assignment or replacement.
- An **azure background** marks a stored reference being inspected. Its eye
  control toggles inspection without removing the reference.
- The red **×** removes a reference. Empty rows offer a green arrow.
- A short MMB click ends reference entry and clears temporary inspection.
  Stored references remain unchanged.

Hover offers one exact green candidate; confirmation uses azure. RMB cycles the common ordered list,
including supported obscured geometry; LMB confirms the displayed candidate.
Only the exact face boundary, edge, point, axis or plane is highlighted. A whole
body is not tinted when selecting a topology reference. The Tree confirms the
same types and identities as View. Geometry comes from original persisted
objects, including STEP imports; result-body topology is not a general placement
source. For a face reference, both hover and confirmation show the actual
visible face fragment, including subsequent trimming and fillets. The stored
reference resolves that fragment to its persisted original ancestor. Coplanar
merged faces retain the identity of the oldest contributing feature; their
internal joining lines disappear. Inspection retains the selected visible
fragment during entry and resolves visible descendants when reopened.
See [viewer selection](VIEWER_SELECTION.md).

A new reference is checked before it changes the definition. Conflicting,
redundant or invalid references are rejected. At zero remaining degrees of
freedom (DOF), remove or replace a reference before adding another constraint.
A missing stable reference never silently switches to another object by index,
name or proximity; its retained independent values preserve meaningful state.

A red Tree background reports a missing or invalid reference, not remaining
degrees of freedom. Partially constrained 3D Curve points are valid. Their
persisted Origin planes and axes remain valid reference owners even when those
datums are hidden outside point editing. Repairing a missing reference clears
the red Tree state.

Every container has a complete local frame, including Point, Axis and Plane.
Position uses up to three references, while two separate orientation slots define
the base orientation. A vertex can constrain three translations. A straight axis
constrains two perpendicular translations and a plane constrains one normal
translation. Independent equations determine DOF, not the number of rows.

For an **Axis**, an original circular edge or cylindrical face defines a center
line. A subsequent planar face can place its origin at the line/plane intersection.
A cone or arbitrary Fillet face is not treated as a cylinder; use an available
original circular edge when appropriate.

The first orientation slot accepts a planar face/plane with **FRONT/BACK**. The
second accepts an independent plane, face, straight edge or axis with
**TOP/BOTTOM/LEFT/RIGHT**. A parallel second direction is rejected. Planar face
entry for Sketch/Extrusion/Revolution initially uses **BACK** from the outward
normal; a later manual FRONT choice remains until the reference is replaced.
Both slots are optional: the container's local frame and corrections form a
valid definition on their own. `RX/RY/RZ` are angular corrections to that base
frame and remain editable independently of its constrained DOF.

### Base plane and plane offset

Sketch, Holes, Extrusion, Revolution and construction Plane share **Base plane**:

- **Automatic** follows the first planar placement reference.
- **XY / XZ / YZ** stores a manual plane in the resolved local frame.
- **Plane offset** moves the working/profile plane perpendicular to that plane;
  it does not move the container Origin or its placement references.
- A manual selection survives reference changes, regeneration and reopening.
  Returning to Automatic resumes following the first reference.
- Revolution still takes its rotation axis from its Sketch.

Changing the plane, references, position, angular corrections or offset immediately
moves the cyan plane border and preview to the resulting frame. Sketch geometry,
dimensions and manipulators use that same frame. Pending changes are committed
only by OK. See [work planes](WORK_PLANES.md) for local-plane interpretation.

An external profile Sketch supplies parametric 2D geometry; the consuming feature
owns the profile's world placement, orientation and offset.

## Measurement

**Measurement** above View opens two reference fields for supported points,
edges, faces, bodies, components, axes or planes. The first selection shows its
properties; the second adds shortest distance. Short MMB ends reference entry;
MMB double-click closes the measurement window. **Save** creates a named
informational history item. Units, approximate results marked **≈**, and reference
repair are described in [Measurement](MEASUREMENT.md).

Body properties can save a centroid Origin for the measured history boundary.
Its point, axes and planes support measurement and later feature placement when
the record is valid and precedes the dependent feature. Missing or invalid
centroids remain unresolved references. See [Body properties](BODY_PROPERTIES.md).

## Deleting a history source

Deleting a Part history source previews dependent objects in red and asks for
confirmation. Cancel restores the original state. OK removes the selected source,
retains dependent containers and their parameters, and detaches broken references.
They stay red until repaired through Properties; no substitute geometry is created.
The operation supports Undo/Redo. Assembly occurrence deletion and Body Boolean
restrictions retain their separate rules. See [History deletion](HISTORY_DELETION.md).

## Ordinary selection in 3D View

Without an active command, Part offers history containers and Assembly offers
the lowest concrete Part occurrence under the pointer, including deeply nested
occurrences. System Origin geometry is not an alternative component identity.
Hover is orange; LMB confirms the exact object in cyan and synchronizes the Tree.
Clicking empty View clears both selections and inspection overlays.

Before confirmation, RMB cycles candidates. Over a confirmed object, RMB opens
its context menu. **Select Parent** moves exactly one ownership level upward;
repeat it to traverse nested Assemblies. Each repeated source occurrence has its
own instance path and remains separately selectable. **Open** can resolve a nested
Part's source from the stored hierarchy without regenerating the Assembly.

Single selection and dimension inspection are separate. Double-click a component
to show its placement dimensions. Click a dimension value to select it and
double-click to edit its value; dimension presentation has its own context action.
During an active selection command, its filter and RMB cycling remain in force
until the command finishes or is canceled.

## Live edits in Properties

New container definitions immediately show a local Origin and a transient preview.
Position, reference and numeric changes update it. Enter commits the focused numeric
field without closing the dialog. **OK** validates, calculates, commits one
transaction and closes. **Cancel** discards pending edits. Short MMB never accepts
the dialog; MMB drag navigates and MMB double-click invokes OK.

Parametric dimensions follow the resolved local frame, including offsets,
FRONT/BACK, quarter-turn orientation and `RX/RY/RZ`. Their text is the ordinary
hover/edit target, so extension lines do not steal geometry selection. Purple
grips provide presentation or value manipulation according to the active tool.

### Rollback while editing a container

Opening Properties temporarily displays the real calculated input immediately
before the edited history container. The container remains green in the Tree,
while later operations are suppressed only for the edit session:

```text
Body → preceding containers → green edited container
     → Insert here → temporarily suppressed later containers
```

The preview derives from this input, not from a previous preview or the final
body. Opening the dialog consumes stored boundary geometry without hidden body
calculation. OK replaces the existing definition and evaluates following history;
Cancel restores the unchanged state. Both remove transient overlays and return
normal selection/full-history display while preserving the camera.

### Solid operations

Feature Properties expose **+ Add** and **− Subtract** where the feature supports
them. The operation is shared with the Tree context action. Assembly Extrusion
and Revolution always subtract from selected immediate Part occurrences.
Bodies themselves have their own histories; Boolean is a separate operation.
See [multibody modeling](MULTIBODY_AND_BOOLEANS.md).

### Extrusion, Revolution and Thin

Extrusion supports numeric length, **Up to Face**, and **Through All** for
subtraction. Up to Face accepts an original planar face/datum plane or a supported
curved face. Extrusion remains normal to the profile plane. Each sampled profile
ray must reach an unambiguous forward target; a missing, crossing or ambiguous
target is rejected rather than replaced with a different extent.

The cyan preview uses stored Sketch/reference geometry. OK or Regenerate performs
the solid calculation against the exact target surface. A planar target uses its
infinite supporting plane, not the finite boundary of the selected face.

**Draft angle** also works with Up To a general surface. Feature **Revolution →
Up To** accepts an original general surface or an inclined/offset plane. The
profile's circular trajectories must all reach a unique first forward contact;
a target outside their reach or tangent to them is rejected. The exact surface
defines the calculated end; the cyan preview uses stored reference geometry.
Planes through the axis retain their ordinary uniform-angle behavior.

A closed profile can create a solid or **Thin** walls. An open profile uses Thin.
Choose first side, second side or symmetric thickness. The supported Thin input
is one continuous unbranched chain or one closed loop of segments, arcs, ellipses,
elliptic arcs, splines and evaluated corner radii. Branches, disconnected chains,
collapsed offsets or self-intersections are rejected. **Surface** creates uncapped
zero-volume geometry through the profile Feature's existing result selection.

The Thin preview shows both offset boundaries, longitudinal corner edges and open
end caps. Direction, extent and wall-side changes update the same pending preview.
Length/angle manipulators and dimensions start at the actual offset profile plane,
including negative offsets and rotated local frames. Negative direct length/angle
input reverses direction while retaining a positive magnitude. Two-sided Flip
swaps Start/End values; symmetric mode retains both end identities.

A standalone Sketch can be converted through **Extrusion** or **Revolution** in
its Tree/View context menu. The normal creation/edit dialog opens. OK replaces the
Sketch at its history position while preserving identity, placement and profile;
Cancel leaves it intact. **Sketch** permits profile edits before confirmation.

### Holes from a Sketch

Use **Part → Holes** for drilled hydraulic channels. Each non-construction
segment defines one finite cylinder from its start to its end. **Hole diameter**
is shared by all channels; segment length is the drilling length. Intersecting
channels are united before subtraction. Ends are flat, with no automatic tip,
thread or extension.

A selected standalone Sketch converts in place; without one, define a new Sketch
through the shared Sketch Properties dialog. Construction segments are ignored;
non-construction circles, arcs, splines and text are unsupported here. OK commits
one subtraction; Cancel also discards pending Sketch changes. This is a Part
feature, including a Part activated inside an Assembly. See [Holes](HOLES.md).

### Fillet and Chamfer

**Fillet** opens the same Properties dialog used for later editing. Select actual
input-body edges; Ctrl adds/removes them and the list can remove entries. All
selected edges share the radius. A tangent route may contain multiple edges;
removing a member removes only that member, while removing the parent removes
the route. **Restore continuous route** refreshes its current membership.

Editing shows the original sharp input before the treatment. OK commits one
feature or replaces the existing one; Cancel preserves the model. An impossible
radius or edge combination leaves the dialog open and the last valid body intact.
Inspection highlights only the treatment's boundary edges. Double-click exposes
the radius; context **Properties** opens the editor. Circular treatments display
the dimension in a stable normal section between the two boundary circles.

**Chamfer** has its own container/dialog and cannot be switched to Fillet inside
Properties. It shares selection, tangent routes and transaction behavior. Current
modes include symmetric, two distances and length/angle; use the reference side
for asymmetric definitions. Circular edges use the same command. See
[edge treatments](EDGE_TREATMENT_COMMANDS.md) for parameter and side rules.

## Sketch mode

**Sketch** in feature Properties opens the owned drawing workspace while retaining
the pending feature definition. The camera aligns with the complete local Sketch
frame, including its offset and rotation. Finishing returns to the same Properties
window; the feature's OK commits it. Cancel discards pending changes.

Local X/Y axes are thin brown dashed lines across View; profile geometry is blue,
points yellow and construction lines yellow centerlines. On leaving Sketcher,
the camera animates back to its previous 3D position. Initial model-Sketch zoom
uses logical display DPI for an approximate 1:1 physical scale; it is not a
measuring instrument. Frame/title-block editing instead fits the whole sheet.

### Drawing geometry

The toolbar provides construction lines, points, segments, polylines, rectangles,
polygons, circles, arcs, ellipses, elliptic arcs, splines and text. Constraints and
dimensions are grouped into menus using the same actions and selection state.

Confirm defining points with LMB. Short MMB and MMB drag do not add points. A quick
MMB double-click finishes the active tool at the last confirmed point; it does
not add a point at the double-click position. Esc first cancels pending geometry,
then ends the tool when nothing is pending. RMB may cycle candidates or perform
the active tool's documented mode switch.

A circle uses center then circumference. Only the center remains its defining
point; the second click supplies radius. An ellipse uses center, major-axis end
and perpendicular minor-axis size. An elliptic arc adds start/end parameters;
both endpoints lie exactly on the ellipse. Pending steps remain a preview until
the last valid click.

**B-spline — control points** uses control vertices; **Interpolating spline**
passes through the entered points. Both need at least three points. MMB
double-click completes them at the last confirmed point. Three points do not
turn a spline into a circular arc. Existing spline Properties edit degree,
coordinates and closed periodic state. A later endpoint tangent constraint
changes the spline's end handle without moving the attached segment.

In **Polyline**, RMB after the first segment switches the next section between
a segment and a tangent arc. The preview shows the derived arc center, which can
snap to X/Y. Endpoints can snap to characteristic points `K` or align horizontally/
vertically with another point. Confirmed suggestions become actual constraints.

A **construction line** is a two-point line supporting constraints/dimensions but
excluded from the solid profile. Revolution uses the Sketch's first construction
line as its extended rotation axis. Other geometry can also be marked construction;
that state excludes it from the profile without changing its shape.

Sketch points store current X/Y coordinates; these are not automatically driving
dimensions. Drawing near an existing point reuses it. Snapping a segment endpoint
to X/Y can combine point-on-axis with H/V alignment, giving exact constraints
rather than approximate visual agreement.

### Text

Choose **Text**, place its anchor, then enter content, height, alignment, color,
rotation and optional horizontal flip in the shared internal Properties window.
OK commits; Cancel discards the preview. Double-click or context Properties edits
the same entity. Delete removes the complete text.

Native text retains its semantic content and calculated font contours. It can
participate in Extrusion/Revolution profiles, including multiple letters and
nested holes. Opening or selecting it consumes stored data without reconstructing
font or body geometry. The specified ISO text height in millimeters is capital
letter height, consistent in Sketcher and Drawing.

### External references

**External Reference** offers original persisted faces, edges, vertices and axes
from valid source objects. The source must exist before the consuming feature's
history boundary; a Sketch cannot depend on its own result. Construction geometry
is also available subject to the same dependency checks.

Hover/RMB/LMB use the common candidate list. A valid confirmation stores a
read-only projection and the command remains active for further references.
External curves and face boundaries are brown dashed lines; reference points are
brown crosses. A face may contain several contours. Select/Delete removes the
reference through the shared transaction.

References may come from other Assembly components while editing an exact active
Part occurrence. They retain source and consumer identities, occurrence paths
and Assembly context. This creates a read-only dependency, not ownership of the
source. Cycles and invalid forward references are rejected.

At an explicit calculation/refresh boundary, the exact stored identity resolves
again and updates the projection. Missing, ambiguous or degenerate sources are
marked broken while retaining their last valid projection; they do not bind to
nearby geometry. An axis perpendicular to the Sketch plane is a degenerate line
projection and is rejected. A curved face can supply finite section branches;
an axial plane through a cylinder, for example, yields two mantle boundaries.

Ordinary drawing, hover, tab changes and Properties opening do not trigger body
calculation or a hidden dependency refresh. See [external reference geometry](CONTEXT_REFERENCE_GEOMETRY.md),
[exact projection](SKETCH_EXACT_PROJECTION.md) and [refresh](CONTEXT_REFERENCE_REFRESH.md).

### Offsets

**Offset** creates an editable parallel curve with a distance and side. For STEP
or other external geometry, first project the source into an owned Sketch curve, which may retain its
external dependency. An offset references that curve and creates no separate
external reference.

Trimming retains the complete supporting curve and the selected interval. Existing
offsets keep their supporting shape; a new offset adopts the selected trimmed
interval. Distance and Flip remain editable together. Offsets do not automatically
build corner connectors or choose closed-loop branches; add connectors as needed.
See [Sketch offsets](SKETCH_OFFSET.md) for exact/approximated curve support,
reference release and validity limits.

### Constraints

Selection order normally distinguishes the reference from the driven geometry.
A conflicting operation leaves the Sketch unchanged. The following table gives
the principal workflows; [Sketcher](SKETCHER.md) contains full solver and inference
rules.

| Constraint | Selection and behavior |
| --- | --- |
| Coincident | Two native points merge into one stable point, without a separate `C` marker. A point and axis/segment/curve instead create a point-on-geometry `C` relation; X/Y may be selected first |
| Horizontal / Vertical | Select a segment, or reference point then driven point. Horizontal shares Y, vertical shares X; a green H/V marks the relation |
| Equal | First segment length drives the second. Connected circular equalities share one radius across the complete component; circular arcs/circles may be mixed, while ellipses are excluded |
| Midpoint | Select a separate point, then a segment/construction line. The point follows the average of both endpoints; an endpoint of that same segment is not a valid target |
| Symmetric | Reference point, driven point, then construction-line axis. Points must differ; later source/axis changes update the reflected point |
| Concentric | Two circles, circular arcs, ellipses or elliptic arcs. The second center and its dependent points move rigidly onto the first; B-splines are excluded |
| Tangent | Select supported segment/curve or curve/curve pairs. Contact must lie within finite segments and arc domains; stored branch/contact data preserves the chosen relationship |

Moving a circle/arc center translates its dependent geometry without changing
radius or arc interval. Fixed or externally anchored conflicts reject the entire
change. Equal radius keeps the driven center and arc endpoint angles while moving
points on the circumference consistently. A driving radius or diameter on any
member updates all connected equal-radius members, including four rounded
rectangle corners. Successive dimension edits retain the whole equality graph;
conflicting driving dimensions or impossible corner radii reject the edit
without changing the document. External sources remain read-only.

When placing a straight segment, the cursor stays at the offered **C + T**
contact while that contact is captured. Moving far enough to release tangency
offers **C** alone; there is one preview endpoint and one contact state.

A normal tangent contact uses **C + T**: point-on-curve and tangency. **K + T**
means an explicitly selected characteristic point and tangency; circular geometry
alone does not create K. Dragging an unlocked circular contact can change radius
while retaining the tangent segment's length/direction. A locked radius constrains
motion to the circumference. Arc endpoints act as radial/angle handles according
to active dimensions. A known remaining case is resistance when returning a free
segment endpoint along the same arc-tangent branch; see Sketcher's open solver
work before assuming every `A → B → A` motion is covered.

**Common Tangent** creates a new segment between two selected circles, arcs,
ellipses, elliptic arcs or B-splines. Click near the desired contact on each curve
to choose the branch. The resulting ordinary segment keeps its endpoints on the
curves and remains tangent through stored constraints. A nonexistent, degenerate
or conflicting branch leaves no partial geometry.

A reversible shared-corner radius is created by selecting two connected segments
in Select mode and dragging their common point. The radius is retained as a Sketch parameter; its actual arc trims
the two sides for profile calculation. See [Sketcher](SKETCHER.md)
for the detailed corner workflow.

### Dimensions and selection

Dimensions use stable identities, shared Properties and numeric editing. Their
locked/driving/measured states determine which values the solver can change.
Numeric input accepts `+`, `-`, `*`, `/` and parentheses, including a decimal
comma; `5+4*4` evaluates to `21`. Division by zero and nonnumeric expressions are
rejected. Use context **Lock/Unlock** for the selected value; dimension appearance
and value locking are separate properties.

Dimension value entry also accepts a trailing length unit (`mm`, `cm`, `m`,
`in`, `inch`, `inches` or `"`) or angular unit (`deg`, `°`, `rad`). The suffix
applies to the whole expression: `(1/2 + .125)in` means 15.875 mm. Without a
suffix, the current document unit applies. For example, entering `0,254inch`
in a millimetre document gives 6.4516 mm; entering `25,4mm` in an inch document
gives 1 inch. The document's unit setting does not change. Length units cannot
be used for angles or dimensionless counts. Mixed-unit terms inside one
expression are not supported. Confirming an unchanged displayed value retains
the exact stored value, even if the display shows fewer decimal places.

**Normal View** realigns the camera with the active Sketch. **Select** ends the
drawing command. Hover is orange, confirmed selection cyan. Delete removes selected
geometry and its applicable dependencies; deleting a defining point also removes
geometry that depends on it. A rectangle dragged through empty space selects a
set; Ctrl adds to the selection and Delete removes the set in one reversible step.
Dragging a point or dimension displays only the active Sketch's pending geometry.

**Finish Sketch** retains the pending Sketch and returns to the enclosing feature
Properties when entered there. **Cancel edits** discards changes since entry.
The enclosing feature is committed only through its OK transaction.

## Container auxiliary geometry

Use **Hide/Show** on a container's **Origin** in the Tree to persist auxiliary
geometry visibility. In Properties, **Origin** temporarily selects another
container's construction context. Clicking its View/Tree item reveals the context;
clicking again hides it. Leaving Origin selection resumes reference entry while
retaining revealed contexts until the dialog closes.

The context includes the local point, X/Y/Z axes and XY/YZ/XZ planes, plus a
feature axis or actual working/profile plane where supported. A work plane uses
the local Origin's constant display size and can be picked as a stable reference.
It is derived from stored frames rather than live solid traversal.

The edited container shows its own construction frame. When selecting external
Origins inside an Assembly, only the top-level Assembly Origin is offered by
default; reveal another Origin explicitly for its exact occurrence. Hidden
contexts are absent from both painting and picking. Closing Properties retires
the temporary visibility state without changing saved references.

A preceding container's Origin can locate a later container. Tree Origin entry
uses its corresponding planes. History order and cycle checks still apply;
returning from an owned Sketch preserves pending references and correct DOF.

## Basic Assembly workflow

Create an **Assembly** (`.asmz`) and use **Insert** for a Part (`.prtz`) or nested
Assembly. The first component starts at the Origin; subsequent components are
initially placed beside the current geometry. Each occurrence has a separate
identity and a Tree branch for its source content and local Origin.

Only the immediate owning Assembly positions a component. A parent treats an
inserted subassembly as one component; activate that subassembly before editing
its internal placements. Names and repeated source files do not identify an
occurrence by themselves.

**Hide/Show** changes display only. **Suppress/Restore** changes participation in
the active Assembly and propagates along explicit dependent mate/reference chains.
Derived suppression is separate from manual suppression; restoring a source does
not restore unrelated or manually suppressed components. Missing references remain
visible as errors for repair rather than silently disappearing.

### Component Properties and mates

Open **Part Properties** or **Assembly Properties** from the occurrence's context
menu. Placement rows pair a source and target reference:

```text
component reference ↔ owning Assembly reference | mate type | value | Flip
```

Arm a reference field, select original geometry of the component being placed,
then the corresponding geometry of another immediate component or its owning
Assembly. The next available pair becomes active. Fields share green input and
azure inspection states with other Properties dialogs.

Supported geometry determines the offered mate: point–point, axis–axis, plane
coincidence/offset or plane angle. Values use millimeters or degrees as applicable.
Curved faces are not plane references. Selecting the owning Assembly's whole
Origin pairs XY–XY, YZ–YZ and XZ–XZ as a pending change; OK commits it.

Coaxiality retains axial translation and rotation. An end-plane mate can fix
translation; a subsequent plane angle controls rotation. The solver satisfies all
active rows together, preserves the nearest valid orientation and reports a
conflict when no valid placement exists. DOF counts and field editability follow
independent equations, not row count. Missing or ambiguous identities never bind
to a different face by position/index.

Moving a source component through an explicit placement edit also updates its
dependent chain within that transaction. Cancel restores the full pending chain.
See [Assembly references](ASSEMBLY_REFERENCES.md) for direction, Flip, limits,
source geometry and nested ownership rules.

### Translation and rotation handles

A selected immediate Part/subassembly shows a purple Origin point. Drag it through
remaining translational freedoms. A free component moves in the View plane;
a coaxial component moves along the axis. Grounded or fully constrained components
do not move. Rotational freedom alone does not enable Origin translation.

With Properties closed, release commits one reversible move and Esc restores the
pre-drag placement. With Properties open, movement remains pending until OK;
Cancel discards it.

For rotation, define an **axis-to-axis mate and an angle mate** for that component.
Double-click the component to show its dimensions and purple radial arm. Drag
the endpoint to change the existing angle. The arm stays 70 screen pixels long,
starts on the common axis and follows its perpendicular plane. It respects angle
limits, grounded state and value locks. Release commits one revision; Esc cancels.

Angles use the existing -180° to +180° interval. Crossing 180° stores the equivalent
signed value, such as 190° becoming -170°, while preserving geometric rotation.
The control does not automatically create an angle mate from coaxiality alone.
See [Assembly rotation arm](ASSEMBLY_ROTATION_HANDLE.md).

### Activating a Part or subassembly

**Active** selects the exact source editing context. A Part exposes Modeling tools;
a subassembly exposes Assembly tools for its own immediate components. The full
top-level Assembly stays visible as passive context. Source geometry outside the
active document can only be used through explicit read-only external references.

A Part Sketch uses its local frame transformed through the active occurrence, not
the top-level Assembly Origin. Edits belong to the source `.prtz`, shared with any
separate tab for that Part. **Back to Assembly** ends contextual editing. Parts
are edited before Assembly-owned cuts.

Current calculated Part geometry becomes visible in its Assembly occurrences
without an Assembly regeneration merely to refresh display. **Regenerate** is
still required to solve mates and calculate Assembly-owned cuts. It uses open,
unsaved sources as authoritative and loads closed sources from native documents.
Switching tabs does not invoke these calculations. Regeneration preserves camera
orientation, pan and zoom. See [geometry sharing](ASSEMBLY_GEOMETRY_SHARING.md).

### Assembly cuts

Assembly **Extrusion** and **Revolution** subtract material from selected immediate
Part occurrences. They never add material, including creation, editing or conversion
of an Assembly Sketch. Their parameters/results belong to `.asmz`; source Parts
remain unchanged. Use the explicit target list and the owning Assembly context.

Components retain independent identity and are not fused merely for display.
Native documents retain required source/calculated geometry internally; there is
no required geometry sidecar or old-format migration step. See
[Assembly cut commands](ASSEMBLY_CUT_HISTORY_COMMANDS.md).

## Import, export and appearance

**File → Import** supports STEP, IGES and text DXF. IGES imports geometry into a
Part body. DXF creates a Sketch with an imported block in the active body, or a
new body when required; in active Sketcher it inserts into that Sketch. Assembly
import creates normal source Parts/components in the exact editing context.
STEP Assembly import creates source Parts/subassemblies with their hierarchy.
A new DXF Sketch starts in local XY and uses ordinary placement Properties.

**File → Export → STEP** exports visible calculated Part bodies or the Assembly
hierarchy with names, placements and shared repeated sources. It does not export
ZIMA history, constraints or Assembly mates. Regenerate first when updated
Assembly operations are required. A Drawing exports model geometry through its
source Part/Assembly. Supported entity details are in
[STEP import/export](STEP_IMPORT_EXPORT.md) and [IGES/DXF import](IGES_DXF_IMPORT.md).

**Colors and Appearance** above View provides palette classes, named appearances,
gloss/metallic settings, base body appearance and named face groups. An occurrence
override belongs only to that exact Assembly occurrence. See [Appearance](APPEARANCE.md).

**File → Rename File** preserves the native extension and updates supported native
dependency paths. A same-named linked Drawing is renamed with its model. See
[native rename](NATIVE_FILE_RENAME.md) for scope and validation.

## Basic Drawing workflow

Create a `.drwz` through **File → New → Drawing** and select its source `.prtz`
Part or `.asmz` Assembly. Canceling source selection creates no Drawing tab or
file. Alternatively, use **Drawing** in the source Part/Assembly Tree header:
the source is already known, so no file picker opens. That shortcut opens an
existing same-named Drawing before creating a new one. The source link exists
even before the first view. The opposite
**Part/Assembly** header action opens the exact source. Renaming the model updates
its stored Drawing link.

Drawings use the common bottom status bar for tool hints and save messages.
The canvas is black with an unfilled white sheet boundary and white geometry.

### Sheets, frames and title blocks

Bottom sheet tabs provide **+** to add a sheet and **−** to remove the active one;
the last sheet cannot be removed. **Format** changes only the active sheet.
A4 is portrait; A3–A0 are landscape, with real paper dimensions in millimeters.
The sheet Origin is bottom right: positive X points left and positive Y up.
Changing format extends the sheet left/up.

Open `.frmz` frame and `.tblz` title-block files through **File → Open** to edit
them in Sketcher. The template remains a 2D orthogonal workspace with the same
sheet axes; it cannot orbit. Sketch tools, dimensions, constraints, pan/zoom,
text and supported geometry colors are shared with model Sketches.

Automatic text uses `&` tokens mixed with ordinary text, for example:

```text
Number: &document.file_stem.&model.revision / &drawing.edition
```

`&model.revision` addresses the model's English parameter key. Localized tokens
such as `&Verze` and `&Version` resolve language-specific values. `&drawing.edition`
is a sheet-local field. `&document.file_stem`, `&sheet.format`, `&sheet.scale` and
`&sheet.position` are automatic values. Editing/saving the text retains tokens
rather than replacing them with static captions.

Frames/title blocks share the configured **Formats** path, normally
`config/formats`. **Add Frame/Add Title Block** starts there. Saving archives the
previous template as `.frmz.1`, `.frmz.2` or corresponding `.tblz` revisions.
Inserting a template embeds its definition into that sheet; later library deletion,
renaming or edits do not alter existing Drawings. Each sheet owns its copy.

Template `(0, 0)` maps exactly to sheet `(0, 0)` without automatic normalization or
hidden offsets. Design title-block placement in the template. Text retains
alignment, rotation, flip, font, color and capital height. BOM Repeat Regions
support item number, quantity and source parameters. See [Drawings](DRAWINGS.md)
for template images, region direction and language behavior.

### Canvas controls

The Drawing Tree and canvas share entity selection. Selecting a Tree leaf
highlights only that entity in cyan; Ctrl-click adds or removes individual
entities, including axes, within the displayed sheet. Selecting an entity on
another sheet displays that sheet. An empty canvas click clears both selections.
The Tree includes visible model annotations, measured dimensions, view captions,
section labels and traces, texts, and balloons.

Right-click a selected Tree leaf to keep the current selection and open its
available actions. Dimensions, texts, balloons and views expose their existing
properties editors. Delete removes selected drawing annotations or hides model
annotations in that view; one Undo restores the complete operation. Whole views
are protected from ordinary Delete and mixed-selection deletion. Remove a view
only with its explicit **Delete View** command.

| Input | Action |
| --- | --- |
| Mouse wheel | Zoom around cursor |
| MMB + RMB + movement | Pan |
| Reset View | Animate to centered sheet and fit its height |
| LMB drag on a view | Move the inserted view |
| Delete | Remove the selected view |
| Esc | Cancel pending placement |

The Drawing canvas is two-dimensional and does not orbit.

### Inserting and editing views

Choose **Insert View** and click the sheet position. The first view is isometric;
the shared **View Properties** opens for source, name/label, orientation,
visible/hidden/shaded edge style, sheet or custom scale and position. Scale ratios
use `M1:1`, `M1:2` or `M2:1` in Properties, view captions and vector output. It is also
the later edit dialog. The preview is pending until OK; Cancel discards it.
MMB double-click over the canvas invokes OK; a short MMB click does not.

The whole rectangular view region is selectable. Its normally hidden border
turns orange on hover and cyan on confirmation, synchronized with the Tree.
Empty canvas clears selection. **Projected View** in the context menu snaps to
eight 45° directions and respects the sheet's projection method. Parent movement
moves its children; an individual child stays on its projection ray.

A sheet-scale view follows later sheet scale changes; a custom-scale view stays
independent. Tab changes display the stored projection. **Regenerate** explicitly
loads current source data and updates the view. The **Variant** control offers the generic and every Family Table variant, including closed variants.
Selecting a closed variant is the explicit action that calculates it. Save the owning family before selecting a variant: the Drawing stores its stable
row identity and the common parent file path. Renaming the variant preserves this
link. Changing the variant reprojects the Drawing in one Undo transaction. An empty
Drawing offers open Part/Assembly variants before Insert View.

### Text

**Text**, below Dimension, creates multiline sheet text using the shared text
properties window. Click the sheet for its position and confirm with OK. Select
and drag existing text, double-click to edit, or press Delete to remove it.
Native saving and PDF/DXF exports preserve text. See [Drawing text](DRAWING_TEXT.md).

### Dimensions and Show/Erase

Drawing supports associative measured dimensions and model annotations. For a
linear dimension between parallel edges, activate **Dimension**, confirm the two
original projected edges with LMB, position the yellow preview and use the tool's
short-MMB placement step. MMB double-click ends the tool. References remain stable
through regeneration; missing geometry becomes unresolved rather than rebinding.
See [dimension design](DRAWING_DIMENSIONS_DESIGN.md) for the expanded dimension
set and current limits.

Select a view and open **Show/Erase**, or open the tool and then select the view.
It offers original dimensions, axes and auxiliary geometry. Assembly views include
supported component axes and Assembly dimensions, not every inserted Part's Sketch
dimension. **Show** offers hidden items; **Erase** offers visible ones. Select
items in View or the list.

Short MMB ends item selection and arms the next-view field. Pending changes remain
visible while moving to another view. OK or MMB double-click commits all edited
views in one transaction; Cancel discards them all. There is no intermediate Apply.
Regenerate loads newly available source annotations.

Dimension Properties controls text, tolerances and presentation. **Nominal and
tolerance units** identifies the numerical unit of that annotation. **Keep
trailing zeros** retains the selected number of decimal places, for example
`1.0000` instead of `1`; it does not change geometry or add a tolerance. Sketch
properties also support an explicit annotation decimal setting. An annotation
with its own unit retains its original manufacturing specification when View
units change. Where the complete specification converts exactly, the View shows
that exact equivalent. Otherwise it shows the original specification and an
approximate secondary nominal marked **≈**. The approximate value is outside a
basic-dimension frame. Display decimal settings do not round manufacturing limits.
New model/Sketch tolerances use the document's units; existing specifications
keep the units shown in their properties. Unchanged confirmation preserves the
original annotation. Model dimension-label offsets and radial placement angles
also follow document units; drawing placement continues to use paper mm/degrees.
Complete document-unit conversion is still being finalized; see the
[unit audit](DOCUMENT_UNITS_AUDIT.md) for its current scope.

Drawings store
view-local overrides without changing the source model or other views. Purple
text/arrow grips change placement; RMB while dragging switches supported arrow/
radius modes. Esc discards the drag. Model Part/Assembly presentation uses the same
controls and can show its oriented dimension box through the View menu.

**Dimension working guides** in View Properties use an initial offset and spacing
of 8 mm. They follow the oriented object envelope, help align dimensions and do
not appear in PDF. A dimension remains visible in oblique views unless its measuring
line itself projects to a point. See [Show/Erase](DRAWING_SHOW_ERASE.md).

An axis viewed along its direction becomes a cross with a center point. Each hole
owns its own cross; its arms follow the profile radius plus the paper-space
extension. Side views show an extended axial line. Purple points also identify
movable view/section labels; fixed objects have no active grips.

### Drawing and image export

PDF, current-sheet DXF and PNG/JPEG sheet/region exports are available through the
shared export operations. DXF uses millimeters and includes visible axes and
dimensions without working guides. **JPEG — Current View** captures the actual
visible View at its screen resolution in Part, Assembly or Drawing, excluding
application panels and retaining camera/highlight state. Exports do not change
the open document's path. See [Drawing commands](DRAWING_COMMANDS.md) and
[View export](VIEW_EXPORT_COMMAND.md).

## Surface modeling

Activate an editable Body and use the surface group after Drill Point in Modeling.
**General Surface** owns ordinary Sketches and 3D Curves inside its container.
Click a boundary field to edit it, choose Sketch plane/offset, and add, remove or
reorder definitions. The container's ordinary Origin placement carries the
complete perimeter. Nested edits commit only with the outer OK.

**Fill Surface** uses preceding curves or native edges. Enter a closed perimeter
in boundary order, including triangular or two-arc contours. Each boundary can
request G0, G1 or G2; G1/G2 also need its original supporting face. Incompatible
geometry or unsatisfied continuity is reported rather than approximated silently.

**Sewing** joins contiguous calculated faces into one surface shell. Use
**Fillet** on that shell's real input edges with a feasible radius. Neither
operation automatically creates a solid.

**Surfaces from solid** converts one calculated solid to exact yellow surface
faces with zero material volume. Select faces to remove, or leave the list empty
to retain its complete closed skin. There is no thickness field. Properties,
Cancel, Undo/Redo and input-face selection follow Shell. The last remaining face
cannot be removed; disconnected retained pieces are permitted. Unrelated surfaces
in the Body retain their ownership.

**Thicken Surface** converts one selected calculated surface to a solid with a
positive thickness measured normal to the surface. Choose First side, Second
side or Symmetric; symmetric applies half the total thickness on each side.
Only the selected free face is consumed. Unselected surfaces remain and touching
preceding solids are joined through Add. The source eye is independent of input;
OK commits, unchanged OK adds no Undo step, and Cancel restores the result.
See [Thicken Surface](SURFACE_THICKEN.md).

New Fillet and Chamfer faces on a surface shell are yellow, like other surfaces.
Unrelated solids keep their ordinary appearance. If an older calculated model
fails at a radius that should fit, use **Regenerate** once before retrying:
General Surface and Fill now check the final boundary at document precision
and perform a bounded finer fit when needed. Opening the file alone retains
its saved geometry.

**Surface Intersection** selects two original bounded faces and creates reusable
curves or isolated points. It leaves the input surfaces unchanged. Fill and Trim
can reference the resulting edges; compatible Sweeps can use them through
ordinary Sketch External Geometry. Coincident areas report an error.

**Surface Trim** selects a current surface, cutting faces or native edges, then
**Select retained region** lets you click inside the part to keep. A curve tool
must lie on the bounded surface and divide it. A closed loop can keep its interior
or the surrounding region with a hole. Avoid clicking directly on the cut.
If a source change invalidates the retained region, reopen Properties and select
it again. Tools and unselected surfaces/solids remain unchanged.

The eye inspects an exact stored reference independently of entry. Click a field
to replace its reference; removable list entries have a red cross. A short middle
click ends entry and inspection without deleting values. OK (also a middle-button
double-click over the View) calculates and commits; Cancel discards the draft.
Properties shows the real model immediately before the feature. Unchanged OK
adds no Undo step. Save/reopen and regeneration retain native source identities.

All surface results have zero material volume. Grid editing and G3 continuity
are deferred. See [Surface commands](NETWORK_SURFACE.md) for exact capabilities,
identity limits and verification scope.

## Tangent profile editing and external references

Radius/diameter and line-angle dimensions can drive a tangent arc profile even
when its lower support comes from an external face. Unlocked driving dimensions
follow permitted grip movement. Locked dimensions and external source points
retain their equations; a blocked gesture makes no document change.

A new segment may start at an external reference point and finish on a native
circle with C+T. The captured contact stays at one endpoint. Leaving the tangent
capture area offers ordinary C; subsequent dimension edits preserve the
confirmed reference and tangent equations.

Changing an Extrusion/Revolution container's placement in Properties reprojects
its owned Sketch references before returning to Sketcher. A reference that can
no longer be used is removed with its dependent relations in the pending edit.
Cancel restores the original references; OK commits them together with the
placement. Removing a linked External Geometry dependency retains its native
owned profile geometry under the ordinary removal rule. Source geometry remains
read-only. Ordinary regeneration still keeps broken identities available for
repair when the source is missing.

After **Surfaces from solid**, automatic axes and path helpers of the preceding
solid disappear from ordinary Part display. Their original references remain
available, and editing the conversion shows the solid input with its axes.

## 2D Sweep and profile stations

2D, 3D and Helical Sweep Properties offer **Custom precision**, followed by
the approximation tolerance in mm. Smaller values generally take longer and
produce a closer approximation. Factory defaults are 0.001 mm for 2D/3D Sweep
and 0.1 mm for Helical Sweep. Each feature saves its own default and optional
override; clearing the checkbox restores that feature's saved default.
The `SweepPrecision` configuration section sets defaults for new features.
This affects the calculated body, unlike the display-only mesh deflection
used for STEP import. [Measured examples](benchmarks/SWEEP_PRECISION_20260926.md)
show the timing and geometry tradeoff.

2D Sweep, 3D Sweep and Helical Sweep are separate tools. Loft is a profile-transition
option inside 2D/3D Sweep.

For 2D Sweep, place the container, choose its own **XY / XZ / YZ** path plane
(default XY) and open **Path Sketch**. This dropdown does not select another
Body face or move the container. The adjacent eye inspects the selected plane.
Draw an open path starting at the Sketch Origin, with any initial direction.

After returning, define the first station's profile. Profiles lie perpendicular
to the local path tangent. An empty later station inherits the preceding profile;
assigning a different profile creates a Loft transition. Owned Sketches are also
available in the Tree.

2D, 3D and Helical Sweep offer **Solid / Thin / Surface**. Solid and Thin support
Add/Subtract; Surface is uncapped and Add-only. **Thin** supports
inward/outward/symmetric wall thickness; symmetric uses
half the total on each side. An open contour forms a band and a closed contour a
hollow section. Variable-Loft thickness is measured in profile planes. A hollow
Solid section may instead use inner profile loops. H-Sweep selects its own base
plane and signed offset, displays the winding axis with endpoint markers during
Sketch editing, and retains previous Sketch geometry as passive context. Its
pitch is available as an offset View dimension for Relations. See
[2D Sweep](SWEEP_2D.md) and [Helical Sweep](HELICAL_SWEEP.md).

## 3D Sweep profile matching

Properties shows the trajectory and active station Sketches. An empty station
inherits the last defined profile and its point order; the first station therefore
requires its own profile.

Use **Point Order** beside the owned profile's Sketch button to choose its first
boundary point. Remaining points follow counterclockwise when viewed against the
Sketch normal. View labels the first as **1 — start** and numbers the others.
Adjacent profiles match 1→1, 2→2, and so on. Point identity/order is stored.
Cancel restores the original choice; Sweep OK calculates and commits the feature.

Two circles without matching points connect without arbitrary seam rotation.
For controlled twist, add point-on-circle `C` or characteristic `K` points. For a
circle-to-rectangle transition, add four points corresponding to the rectangle's
corners and choose matching start points. Neighboring point counts must agree or
calculation reports an error. Edit inherited profiles at their source station.
See [3D Curve and Sweep](3D_CURVE_AND_SWEEP.md).

## Modeling Twist

Choose **Twist** in Modeling with an active editable Body. Its shared Feature
editor uses one closed profile Sketch, an axial length, an angle, a direction
and a linear or smooth transition. The Sketch defines one connected filled
profile, optionally with holes. The axis follows the Sketch normal through the area
centroid of the filled profile. OK calculates and commits the solid; Cancel
discards pending changes. Restore shape returns a straightened Twist to its
authored geometry. Disconnected regions, separate Sketch stations and surface
results are not supported.

## Straighten and Restore shape

These Modeling commands follow the Sweep commands and require an active editable
Body. They operate on additive solid Revolutions, combined profile Features
with a curved side, Modeling Twist and 2D/3D/Helical Sweeps with a constant
cross-section. These commands are included from build 2026100301; they
are not present in Windows 2026100201 and earlier.

1. Choose **Straighten** and keep **All eligible elements**, or select individual
   elements in the reference table and View. The eye inspects a selected element;
   removing a table row removes it from this selection, not from the model.
2. Enter a positive **Length coefficient**. Straight length is the path length of
   the filled section's centroid multiplied by this value: `1` retains that
   length, `0.9` shortens it by ten percent. Section dimensions remain unchanged.
3. Confirm with **OK**. The operation is inserted into history. **Cancel** leaves
   the model unchanged. Opening Properties rolls back to the operation's input;
   confirming unchanged values does not recalculate or create an Undo step.
4. Add **Restore shape** to return eligible straight sources to their authored
   curved trajectories, retaining supported intervening modifications.

Clicking the calculated model selects its original feature in the Tree.
Double-click displays the entire original feature as a blue wire with its
original dimensions at the authored location. The calculated current body stays
visible. Escape closes this inspection. Properties and Sketch editing use the
feature's normal creation boundary; the authored Origin is never moved.

Sheet Cut axes follow the calculated sheet state. After Unbend, the folded axis
disappears and a separate axis is displayed at the unfolded cut. Bend Back
restores its folded location. Old cached calculations require explicit
Regenerate once after installing this correction.

For a following profile, use the preceding end face, optionally with a point,
two points, or a straight edge and point on that section. The filled-profile
centroids must meet and their directed tangents must agree. Finite Extrusions
and constant-section Revolutions or Sweeps can continue this chain. A curved
continuation retains its own formed/straight state, including individual source
selection. The elements move and rotate with the preceding section while their
authored placement, reference sides and corrections stay intact. An offset or
tilt that disconnects this join cannot be straightened by this rule.

Supported Fillets and holes entirely within a straight portion are retained.
Holes crossing a curved portion or transition, variable sections, surface results,
and unsupported downstream modifications are rejected without committing a
partial state. This is separate from Sheet Metal unfolding. See the
[scope and verification record](SOLID_STRAIGHTENING.md) for current limitations.

## Mirror and Pattern

These tools create referenced copies of Part bodies or immediate Assembly
components. Preselect a source or choose it through the **Source** field after
placing the new container.

- **Mirror:** select a planar face/plane, or use local XY/YZ/XZ.
- **Linear Pattern:** choose one to three distinct local X/Y/Z axes. Each direction
  has spacing, count and forward/backward/two-sided/symmetric distribution.
  Two-sided adds a backward count; symmetric uses an odd total with the source
  in the middle. Two directions form a grid and three a spatial array.
- **Circular Pattern:** select an axis, count and step angle or full-circle
  distribution. Supported references include axes and straight/circular edges.

Double-clicking a Pattern copy displays the owning Pattern dimensions, including
spacing, count and angle, rather than the source feature dimensions. The Relations
picker can insert their identifiers; derived full-circle angles are read-only.

Pattern count includes the source: 4 means three additional copies. Copied geometry
is edited at the source; container Properties edits placement, source and copy
parameters. OK commits and Cancel discards changes. Switching pattern mode retains
its other saved axis settings.

Activating an earlier source body shows its calculated geometry before downstream
operations; later bodies, Booleans, Mirrors and Patterns become passive in the
Tree. **Back to Part** restores full display. This inspection does not change
persistent suppression/visibility or calculate geometry.

In Part, Mirror forms a separate body and Pattern groups its generated copies into
one result usable by Booleans. Assembly copies have distinct occurrence identities.
Use explicit Regenerate for dependent copy/mate calculations after source edits.
See [Mirror and Pattern](MIRROR_AND_PATTERN.md).

## Further references

- [Sketcher](SKETCHER.md): inference, constraints, dimensions and known solver limits.
- [Dimension identities](DIMENSION_IDENTIFIERS.md): document-wide stable d1, d2, … labels.
- [Command coverage](CAD_COMMAND_COVERAGE.md): supported GUI/CLI operations and boundaries.
- [Release policy](PORTABLE_RELEASE.md): native packaging strategy and platform status.


### Assembly visibility and origins

Hidden components use muted gray in the Tree. Hiding a component does not delete
its references; red reference warnings indicate genuinely unresolved references.
Component and subassembly origins are hidden by default, including while an
occurrence is active. During insertion or component placement Properties, the
edited Part or subassembly exposes its own origin automatically; all other
component origins remain hidden. Use the shared **Origin** command in a properties window
to reveal the exact occurrence needed for a reference. Closing that command
removes its temporary origin display. In the Tree, a component origin and its
axes/planes expose only **Show origin / Hide origin**, never Part modeling
operations. This explicit display choice lasts for the workspace session. The
origin of a component currently being positioned remains visible.
The selected component's purple point remains available as a drag handle without
revealing its axes or planes. The displayed Assembly origin remains
available through the normal Origins visibility control.

### Appearance transparency

The Colors and Appearance window initially opens at the right edge with compact
palette spacing. Every color supports 0–100% transparency through a slider and
numeric entry. Custom palette entries retain transparency; Cancel restores the
previous appearance. The basic Skeleton preset is muted purple and 70% transparent.

## Smooth fillet display

Fillet shading uses consistent angle-weighted normals at each shared vertex
within one persisted CAD face. Separate faces and occurrences retain their own
normals. Feature highlighting follows semantic ancestry: a cutter shows its
own contribution, and a fillet shows its treatment boundaries. Display edges
and triangle shading consume already calculated viewer data. These rules apply
to other Parts and interactive Drawing views as well as the FORM example;
exact modeling and measurement geometry is unchanged.
