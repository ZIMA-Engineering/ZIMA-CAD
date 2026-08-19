# ZIMA-CAD C++ migration

This directory contains the parallel C++ implementation. The Python
application remains the behavioural reference until an explicitly verified
cutover.

The first vertical slice exercises:

```text
text Part document -> Document Core -> OCCT adapter -> ViewerMesh -> Qt viewer
```

The shared `PropertiesSubWindow` already enforces the first application UI
contract: internal `Qt::SubWindow` ownership, bounded placement, only OK/Cancel,
no commit on Cancel and middle-button double-click OK even over the parent
window. `BoxPropertiesDialog` is the same class for creation and later editing.
OCCT calculation starts only after a successful OK commit.

The Part implementation is no longer a one-object shortcut. Its text model owns an
ordered history of containers with stable unique IDs and explicit Add/Subtract
semantics. The application evaluates the complete history in one explicit
kernel request, and only the final ZIMA viewer packet crosses back into UI.

Box and Cylinder history containers persist an independent placement:
translation in millimetres and rotations around local X, then Y, then Z in
degrees. The OCCT adapter transforms each operand before its Boolean operation.
Placement is therefore model data, not a viewer-only offset. Extrusion is
instead positioned solely by its persisted source Sketch plane and offset, so
it neither serializes nor exposes a second ignored placement.

`DocumentSession` is the only transaction owner. Every accepted Properties
change creates exactly one revision, Cancel creates none, Undo/Redo move whole
persisted document states, and a savepoint drives the dirty marker. A new
commit after Undo discards the old redo branch. Document replacement and
application close require an explicit Save/Discard/Cancel decision.

The first real viewer path uses an OpenGL 3.3 core `QOpenGLWidget`. Positions,
normals, triangle indices and edge indices are uploaded to GPU buffers; camera
movement changes matrices rather than CPU-transforming mesh vertices. It has
orthographic fit, wheel zoom, MMB orbit and Shift+MMB pan. One CPU ray query
produces the ordered triangle candidate list shared by orange hover, LMB cyan
confirmation and pre-confirmation RMB cycling. Triangles are grouped by their
persisted `container_id + semantic_face_key`; only the exact candidate face is
highlighted.

Primitive Box faces use representation-independent semantic keys (`x_min`,
`x_max`, `y_min`, `y_max`, `z_min`, `z_max`). The OCCT adapter propagates
these owners through Boolean `Modified/Generated` history. Ambiguous ancestry
is deliberately left unselectable instead of being silently attached to the
wrong face.

The same packet now carries original semantic Box edges and vertices. A vertex
key is its `x_min/x_max : y_min/y_max : z_min/z_max` position; an edge key is
the canonical sorted pair of its endpoint keys. A Box therefore exposes 12
stable edges and 8 stable vertices independent of OCCT traversal order. OCCT
Boolean history propagates surviving/split original edges and vertices. Pure
ray-edge and ray-vertex candidate builders consume only this packet and order
hits front-to-back without querying OCCT.

Selection now starts with one unified ordered `ViewerCandidate` list containing
Container, Face, Edge and Vertex entries. A `SelectionContract`-style allowed
kind list filters that existing list; it never launches another picker. The
ordinary Part default allows Container only. Commands can switch to Face, Edge
or Vertex while hover, LMB confirmation and RMB cycling continue consuming the
same filtered ordering. Highlighting follows the exact level: all owned faces
for a container, one semantic face, one persisted edge polyline or one vertex.

Opening Properties for an existing history container now evaluates the real
input immediately before that stable container ID. The complete tree remains
visible, the active item is green, and downstream items are temporarily
suppressed until OK or Cancel restores normal full-history display. Editing
the first operation correctly presents an empty input rather than a cached
final body.

Confirmed container selection is synchronized both ways between the viewer
and history tree through the stable container owner ID. Viewer LMB forwards
the already confirmed common-list candidate; selecting a tree item resolves
only against the current persisted viewer packet and does not run another ray
picker or query OCCT.

RMB keeps cycling the common candidate list only before LMB confirmation.
Over a confirmed container it opens the ordinary object menu instead, with
Properties and one-level Select Parent actions. Selecting the Part parent in
the tree clears the container highlight, keeping tree and view ownership in
sync.

Ordinary confirmed selection is retained as a stable container ID across tree
rebuilds, explicit calculations, and rollback exit. It is resolved again
against each new viewer packet rather than retaining a tree pointer or
triangle index. During rollback the active container can remain selected in
the tree without inventing a highlight when it is absent from the input body.

The history tree exposes the same context actions as a confirmed viewer
container. Tree RMB first establishes that stable selection and then opens the
shared menu, so Properties enters the identical rollback editing path from
either surface.

UI rebuild and body calculation are now separate operations. Explicit OK and
Regenerate calculate all history-boundary viewer packets in one OCCT pass and
cache them by document revision. Tree refresh, selection, Properties opening,
rollback, Undo/Redo, and dialog close consume that cache without invoking
OCCT.

The text document persists every calculated history-boundary viewer packet,
including mesh, stable face/edge/vertex references, volume, and area. Loading
a calculated file restores its last display without OCCT. Triangle indices,
reference alignment, finite coordinates, and boundary/history counts are
validated before the packet is accepted. A document intentionally saved
without calculation remains valid and asks for explicit Regenerate.

Each boundary also carries a deterministic fingerprint of its exact operation
prefix: stable IDs, Boolean modes, dimensions, translations, and rotations.
Save and Open reject otherwise valid viewer data when that fingerprint belongs
to different parameters, preventing stale geometry from becoming selectable.

Calculated history boundaries are owned by `DocumentSession`, not by the main
window. Undo and Redo therefore move model parameters and their matching
viewer packets as one state. Regenerate updates derived data without creating
a fake model revision, while still marking the document dirty until that new
calculated state is saved.

History and kernel requests are no longer Box-only. A common variant-backed
`HistoryOperation` evaluates mixed Box and Cylinder Boolean sequences. A
Cylinder persists radius, height, and placement, exposes stable original
`z_min`, `z_max`, and `side` faces plus semantic circular/seam edges. One
`PrimitivePropertiesDialog` handles creation and rollback editing for both Box
and Cylinder; primitive type changes only the parameter fields, not placement,
transaction, validation, or confirmation code paths.

The first Assembly data slice introduces immediate `PartOccurrence` ownership
and a separate stable `InstancePath`. Each occurrence keeps its source Part
identity/path, assembly-owned placement, and the last explicitly acquired ZIMA
viewer packet. Scene composition transforms only persisted viewer data and
does not call OCCT. Repeated insertions of one source retain the same feature
owners but carry distinct length-prefixed occurrence paths through picking,
deduplication, and exact highlighting.

The Assembly implementation uses the current `.asmz` text format. It persists
Assembly identity, stable occurrence IDs, source document IDs/paths,
Assembly-owned placement, and each Part's last explicitly acquired
`BodyResult`. Part and Assembly share one viewer-packet serializer and
validator. Opening composes the persisted scene without OCCT. A one-way
dependency graph rejects self-reference and arbitrarily deep indirect cycles
before an insertion mutates the document.

`Workspace` owns multiple open Part/Assembly documents and keeps writable
activation separate from the displayed top-level document. Each Assembly has
revisioned `AssemblySession` state. Part edits do not implicitly update parent
Assemblies; insertion and Regenerate explicitly consume authoritative
in-memory calculated Part results, including unsaved ones.

The `zima-cad-workspace-cpp` target provides the common Part, Assembly and Drawing GUI:
document tabs, new/open/save Assembly, open Part, insert active Part, explicit
Regenerate, occurrence tree, leaf occurrence picking, and Assembly-owned
component Properties. The older standalone Part and Drawing executables remain
available as behavioural comparison targets.

Drawing dimensions render witness lines, a dimension line, and arrowheads.
They can be selected independently of views, moved by dragging their label,
removed with Delete, and deselected or cancelled with Escape.

The Workspace target now also owns the verified Part commands: new/open Part,
Box, Cylinder, shared Properties, explicit Part Regenerate, Save, and
revisioned Undo/Redo. Editing an active Part while an Assembly remains
displayed does not mutate the parent snapshot; only explicit Assembly
Regenerate pulls the new in-memory result. The older Part-only executable is
retained temporarily as a behavioural comparison target.

Part rollback also works inside a still-displayed Assembly. The Workspace
requires an exact occurrence context and never guesses among repeated sources.
Only that occurrence is transiently replaced by the persisted boundary packet
before the edited container; other occurrences and the complete Assembly stay
visible as passive context. The first operation correctly contributes an empty
input.

Assembly occurrences persist suppression and visibility as separate states.
Suppression removes a component from the active Assembly scene; visibility is
display-only. Neither deletes source packets, placement, or occurrence
identity. The shared tree/viewer context menu exposes Properties, Hide/Show,
and Suppress/Restore. Dependencies are never guessed from geometry.

Assembly files now persist that explicit directed graph for placement and
external-sketch references. Manual suppression propagates only to transitive
dependants; visibility never propagates. Restoring a prerequisite restores
dependency-suppressed components while preserving their own manual
suppression. Cyclic edges are rejected.

An occurrence may also source an Assembly snapshot. It persists both the scene
and a recursive structural tree of stable IDs, names, source kinds, and
component states. Scene composition prefixes the parent occurrence segment to
existing leaf instance paths instead of flattening or replacing them, while
placement remains owned by the immediate parent. Explicit top-level Regenerate
recursively consumes open in-memory subassemblies and Parts and atomically
refreshes both snapshots. Insertion rejects direct and indirect document cycles.

The Workspace strictly decodes the full length-prefixed instance path to
resolve a leaf occurrence and its immediate owning Assembly. The GUI tree uses
the persisted snapshot and those same paths recursively, so viewer confirmation
and context actions stay exact for repeated nested sources. Only the exact
activated subassembly occurrence substitutes its branch with live editable
state; all other occurrences remain passive snapshots. Properties, visibility,
and suppression are committed to the immediate owner. Nested Part rollback
replaces only the exact leaf geometry in the cached top-level scene, preserving
passive siblings and avoiding parent regeneration.

The first Assembly-mate slice persists two exact endpoints (instance path,
container owner, and semantic key), mate kind, signed offset, calculation
status, and its directed dependency edge. A planar face is resolved and fully
checked from persisted ZIMA viewer triangles only; selection and mate
calculation never query OCCT.

Plane-to-plane solves the deliberately bounded deterministic plane alignment
and signed offset of the dependent immediate component. A component may own
multiple Plane mates and multiple Axis mates. They are applied in deterministic
order and then all active constraints are re-evaluated from persisted viewer
geometry; any incompatible set restores the component's complete original
placement rather than retaining a partial solution. Missing and unsupported references turn the mate
red and suppress its dependent branch; explicit Regenerate clears the old
error state before retrying, so repaired references recover.

The GUI command selects both faces through the common viewer candidate list,
then uses the shared internal Properties SubWindow with OK/Cancel to edit name
and offset. OK calculates and commits one Assembly revision.

ZIMA viewer packets now own stable axes as a point, unit direction, display
length, and `AxisReference`. Explicit body calculation emits local `axis:x/y/z`
for a Box and the center `axis` for a Cylinder. Persistence, Assembly placement,
nested instance-path prefixing, rollback, picking, and highlighting then consume
only ZIMA data. Axis candidates join the common ordered list and become visibly
selectable only under an explicit Axis selection contract.

Axis-coincident rotates an arbitrarily oriented dependent axis onto the nearest
equivalent direction of the prerequisite axis and then removes perpendicular
line distance. Translation and rotation along the common axis deliberately
remain free. Plane-coincident uses the same shortest world-space rotation about
the selected dependent point before applying its signed offset. Parallel and
antiparallel references are therefore both stable and do not cause an arbitrary
180-degree flip.

Every calculated mate is verified again from persisted ZIMA geometry. Compatible
axis and plane mates may drive one component together. If their final orientation
or position conflicts, the component placement is restored to its exact
pre-calculation state and all affected mates remain explicitly unsupported rather
than leaving a partial transform behind.

Mates now have a complete editing lifecycle. Creation and double-click editing
reuse the same internal `MatePropertiesDialog`; OK replaces the mate, rebuilds
its dependency edge, recalculates, and commits one revision. The RMB menu offers
Properties, Suppress/Restore, and confirmed Delete. A suppressed mate remains
persisted but neither drives placement nor propagates suppression. Delete
atomically removes the mate and its edge, and Undo/Redo restores the complete
mate, placement, status, and graph state.

Only the exact active Assembly exposes editable mate items. When a subassembly
occurrence is activated, its live mates appear below that green branch while
top-level and repeated passive occurrences remain read-only snapshots. Broken
mates remain editable without forcing a successful calculation in the dialog;
their references may become valid only after a later model change and explicit
Regenerate.

The first C++ Sketcher slice is a standalone Qt- and OCCT-free module. A Sketch
owns stable point, segment, constraint, and dimension IDs on an XY, XZ, or YZ
plane with a signed plane offset. Segment construction state and point fixed/
construction state are model data. Horizontal, vertical, coincident, distance,
signed X-distance, and signed Y-distance equations are supported without live
kernel geometry.

Driving dimension values may carry independent absolute lower and upper limits.
An edit outside either enabled limit is rejected before solving. The iterative
solver commits point motion only when the complete system converges; an
immovable or non-converging conflict restores the exact input coordinates. A
numerical Jacobian reports remaining degrees of freedom without counting
redundant equations twice.

Part documents now persist their Sketch graphs directly, and DocumentSession
Undo/Redo therefore moves a Sketch edit as one normal Part revision. A Sketch
also projects stable point and segment references into a ZIMA ViewerMesh on its
persisted plane. This display and common picker data require no OCCT call. The
Workspace exposes one shared internal Sketch Properties SubWindow for creation
and editing of name, plane, and offset.

The first interactive command now creates a Sketch segment from two LMB points.
Both clicks use the viewer's existing camera ray, projected deterministically
into the active persisted Sketch plane. The first point and pointer motion draw
only a transient dashed preview; the second point atomically appends the segment
as one Part revision. Escape discards the preview. Consecutive endpoints inside
the model tolerance reuse one stable point ID, producing an actually connected
profile rather than two visually coincident points. Camera state is retained
while drawing consecutive segments.

Sketch edges and points now have dedicated `SketchSegment` and `SketchPoint`
viewer candidate kinds. Activating a Sketch exposes only these exact persisted
objects; ordinary Part mode still exposes containers and never leaks solid
result topology. A confirmed segment can receive Horizontal or Vertical through
model-owned commands. The constraint and its solved coordinates commit as one
Part revision. Duplicate constraints and combinations that collapse a segment
are rejected transactionally.

A confirmed Sketch segment can now create its first driving length dimension.
The same internal `SketchDimensionPropertiesDialog` serves creation and later
tree/context editing. It exposes the nominal value and independent absolute
lower/upper limits. On first enable, the lower limit defaults to zero and the
upper limit to the current nominal length. OK validates limits, solves the
Sketch, and commits one Part revision; Cancel, an out-of-range value, or a
solver conflict commits nothing. Editing retains the stable dimension ID, and
the dimension remains visible below its owning Sketch in the Part tree.

Sketch dimensions are now first-class ZIMA viewer data rather than tree-only UI
rows. A dimension packet carries its two witness points, offset dimension line,
numeric value, stable Sketch owner, semantic dimension key, and occurrence path.
The OpenGL viewer draws witness/measurement lines and a three-decimal millimetre
label. Its line contributes a dedicated `Dimension` candidate to the same
ordered picker; double-clicking it opens the identical Properties class used by
the tree. Viewer-packet JSON validates and persists dimensions, while Assembly
composition transforms every dimension point and prefixes exact nested instance
identity without invoking OCCT.

The second interactive geometry command creates a rectangle from two opposite
corners. Pointer motion draws four transient preview edges. The confirming click
atomically creates four shared Sketch points, four closed segments, two
Horizontal constraints, and two Vertical constraints as one Part revision.
Zero width/height is rejected without mutation, Escape cancels the preview, and
switching tools removes the previous transient state. The command stays active
after a successful rectangle so multiple profiles can be drawn without reopening
the action.

Circle is the next native Sketch geometry. It owns a stable ID, one stable
centre point, a positive radius, and construction state. The two-click command
selects centre then rim, draws a 96-segment transient preview, and commits one
Part revision only after a non-zero radius is confirmed. Persisted viewer data
uses the dedicated `SketchCurve` candidate and a stable `circle:<id>` key, so it
cannot be confused with a solid edge.

A confirmed Circle can create a driving Radius dimension through the same
dimension Properties class and absolute limit contract as segment length. The
dimension points to the Circle ID rather than inventing a rim point, drives the
stored radius transactionally, contributes radius DOF to the Jacobian, and is
rendered/picked with an `R` prefix. Circle geometry and its Radius dimension
round-trip in the current Sketch format without OCCT.

Arc is entered by three clicks: centre, start and end. It owns a stable curve ID
and references stable centre, start, and end points together with its radius and
counter-clockwise angular interval. Dragging the centre is strictly a translation:
it moves the complete Arc without changing radius or sweep, matching Circle and
the future Ellipse contract. Dragging an endpoint changes the angular interval;
an undimensioned Arc may also change radius, while a driving Radius keeps it fixed.
The adaptive
viewer polyline owns a stable `arc:<id>` `SketchCurve` reference, so both the
creation preview and later picking stay within ZIMA-CAD data.
Selecting the Arc exposes the same internal Radius-dimension Properties window
as Circle. The dimension drives the persisted arc radius, obeys absolute limits,
participates in solver DOF, and is drawn radially through the middle of the arc.

Ellipse is persisted as a native ZIMA Sketch curve with its own stable ID and
stable centre, major-axis, and minor-axis point references. Moving its centre
translates the complete curve without changing either semiaxis. Moving the major
point changes its radius and rotation while keeping the minor axis perpendicular;
moving the minor point changes only its radius. The viewer uses a stable
`ellipse:<id>` curve reference, and the complete geometry round-trips in Sketch
format version 3. One standalone Ellipse is also an exact shared Extrusion and
Revolution profile. The kernel boundary carries centre, normalized major-axis
direction, and both semiaxes; OCCT constructs a true analytic ellipse and never
promotes the viewer polyline into body geometry. Mixed and nested Ellipse loops
remain explicitly unsupported until their containment contract is defined.
The Modeling menu exposes a three-click Ellipse command: centre, major-axis rim,
then minor-axis size. Both intermediate states use transient ZIMA viewer edges;
only the third valid click commits one Part revision. Escape clears the complete
pending command, and successful creation keeps the tool active for another Ellipse.
Confirmed Ellipses use the same `SketchCurve` selection and Delete transaction as
the other persisted Sketch geometry.
The selected Ellipse exposes two independent internal dimension Properties actions:
major semiaxis `a` and minor semiaxis `b`. Both dimensions own the stable Ellipse
ID, support driving/measured state and absolute lower/upper limits, update their
axis point transactionally, persist in the current Sketch format, and render as
pickable `a=` and `b=` dimensions. Editing the tree or viewer dimension reuses the
same Properties class as every other Sketch dimension.
The generic `set_dimension_value()` path is transactional as well: a successful
value change immediately solves and updates the owned points or geometry, while
invalid values, limits, or solver conflicts leave both dimension and geometry
unchanged. Negative angular values are valid and are never classified as negative
lengths.

Stable modeling references belong to persisted ZIMA Sketches, containers and
their original solids. Result-body OCCT faces, vertices, axes and traversal
indices are not reference owners. `ViewerMesh::original_references` persists a
separate hidden selection mesh for every original history solid, including its
faces, edges, vertices and axes. Part and nested Assembly transforms preserve
the exact occurrence path of that layer; ordinary mates and modeling commands
pick it and highlight it only on demand. The shaded calculated result packet
retains every OCCT edge needed to draw the final silhouette and Boolean
intersections, but its faces and edges have invalid references and are never
offered by the ordinary picker. Fillet and Chamfer are the only operational exception: while
explicitly calculating, they may select an edge of the real input body and map
generated faces back to that operation; the resulting stable meaning is still
owned and persisted by the ZIMA container.

B-spline is native ZIMA Sketch geometry with an interactive control-point tool,
stable curve/control-point IDs, Delete, and one internal Properties editor for
degree, closure, and coordinates. Sketch format version 5 stores open clamped
and closed periodic curves. The viewer evaluates both forms with de Boor solely
from persisted ZIMA data. Extrusion and Revolution pass exact poles, degree and
periodicity through the kernel API to OCCT; mixed line/arc/open-spline loops and
a standalone periodic outer loop with circular holes remain exact. Viewer
polylines are never used as model geometry.
Ellipse rotation is a third independent driving/measured dimension in degrees.
Changing it rotates both persisted axis points about the centre without changing
either semiaxis; dragging the major point respects an active driving rotation.
The viewer and tree expose the same stable dimension ID, degree suffix, absolute
limits, and shared internal Properties editing contract.

The Coincident command owns an explicit point-only viewer selection contract.
Two confirmed stable `point:<id>` candidates create one transactional constraint;
the solver moves only unfixed coordinates, rejects duplicate or conflicting
relations, and Escape restores the ordinary Sketch candidate contract.

A normally confirmed Sketch point can be fixed or released through one model
operation. Fixation is persisted on the stable point, removes its two coordinate
variables from solver DOF, and is committed as one Part revision. Segment
selection also exposes signed X and Y projection dimensions. They reuse the
shared dimension Properties window and render as axis-aligned lines labelled
`X` or `Y`; they remain distinct from true length and from drawing tolerances.

Dimension Properties can switch a dimension between driving and measured state.
Dragging a confirmed unfixed point uses the same ordered viewer candidate that
was hovered and clicked. Motion is evaluated on a transient Part document;
constraints are solved, measured dimensions are refreshed, and absolute limits
reject invalid positions. Releasing LMB commits at most one Part revision.

Segment selection also creates an orientation Angle dimension relative to the
positive Sketch X axis. Its signed value and absolute limits use degrees in the
closed `[-180, 180]` interval. The solver rotates the segment without changing
its current length, measured angles update during point drag, and viewer packet
units explicitly distinguish `°` from the default `mm` dimension suffix.

A confirmed Circle can alternatively own a Diameter dimension. Diameter is a
distinct persisted kind with the solver relation `D = 2R`, absolute limits in
millimetres, a full line through the centre, and a `Ø` viewer/tree label. One
Circle cannot own simultaneous driving Radius and Diameter dimensions. Arcs do
not offer Diameter because their engineering convention remains Radius.

Segment dimensions and segment-owned Horizontal/Vertical constraints persist
the exact source geometry ID. Delete on a confirmed Segment, Circle, or Arc
removes that geometry and only dependencies with the same stable owner. A
post-pass removes truly orphaned points while preserving shared corners and
centres. The whole deletion is one undoable Part revision; no point-pair or
result-topology heuristic is used.

Parallel and Perpendicular are two-Segment constraints with two explicit stable
geometry owners. The first confirmed Segment is the reference and the second is
the driven Segment. The deterministic solver rotates the driven Segment toward
the nearest valid direction while preserving its length and respecting fixed
endpoints. The active command uses a Segment-only common viewer contract; either
owner deletion removes the relation.

Equal Length reuses the same two-Segment ownership and command contract. The
first Segment supplies the target length; the solver scales the second about
its available endpoints without changing its direction. Fixed endpoints are
respected, and an immovable mismatch rejects the complete transaction.

Extrusion is the first Sketch-to-solid vertical slice. The Modeling command is
enabled only for a selected persisted Sketch and reuses one internal Properties
window for creation and editing. OK explicitly evaluates the body; Cancel and
history rollback remain transient. The same Properties window selects Forward,
Reverse, or Symmetric direction. Height is always the total extrusion length:
Forward spans `0…H`, Reverse `−H…0`, and Symmetric `−H/2…H/2` along the persisted
Sketch normal. Direction is persisted and included in the calculated-history
fingerprint. The current profile contract deliberately
accepts one connected closed loop of non-construction straight Segments or one
outer Circle. A polygon may contain non-overlapping circular holes; an outer
Circle may contain non-overlapping inner Circles. Document Core explicitly
classifies outer and inner loops, normalizes polygon orientation, and rejects
crossing, touching, disjoint, overlapping, or self-intersecting profiles before
OCCT. Circles cross the kernel boundary as exact centre/radius loops and remain
true cylindrical faces rather than tessellated polygons. A single outer loop
may also combine straight Segments and exact Arcs and may contain circular inner
loops. An Arc owns its stable geometry ID and explicitly references stable
centre, start, and end Sketch points. Connected geometry therefore shares the
same persisted endpoint ID instead of relying on coordinate coincidence.
Document Core orders those curves through the persisted endpoint geometry; the
kernel boundary independently
checks connectivity, closure, finite points, and non-collinear Arc definition.
OCCT additionally validates the exact face and final solid before returning
viewer data. OCCT returns a
prism with stable start, end, outer and inner side, edge and vertex references
owned by the Extrusion history container.

Revolution is the next complete Sketch-to-solid command. It reuses the same
internal Properties window and transaction/rollback path, with a persisted
Sketch X or Sketch Y axis and an angle in `(0, 360]°`. Revolution and Extrusion
share one exact profile-loop contract: polygons, Circles, mixed Segment/Arc
outer loops, and the currently supported inner circular loops cross the same
kernel boundary without tessellating curves. Document Core maps the local
Sketch axis into XY, XZ, or YZ world coordinates; OCCT receives only the
ordered profile loops, explicit profile normal, axis, and angle during
calculation. Partial
revolutions persist distinct start/end profile faces, all generated side faces
retain the Revolution owner, and angle/axis participate in the history
fingerprint.

It uses the current `.prtz` suffix. Legacy Python document compatibility is
not provided; `.prtz` identifies the current data model rather than guaranteeing
that older documents can be loaded.

## Modules

- `document_core`: Qt- and OCCT-free persisted document model;
- `kernel_api`: ZIMA-owned requests and results, with no OCCT types;
- `kernel_occt`: the only module that knows live OCCT shapes;
- `viewer`: renders ZIMA viewer mesh data;
- `assembly`: immediate component ownership, instance paths, and scene composition;
- `sketcher`: OCCT-free point graph, constraints, dimensions, and solver;
- `ui`: shared in-application Properties window behaviour;
- `app`: Qt application and commands.

## Linux build

Qt and OCCT are consumed from the existing bundled runtime and are not rebuilt:

```bash
cd cpp
cmake --preset linux-runtime-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
../build/cpp-debug/zima-cad-cpp
../build/cpp-debug/zima-cad-workspace-cpp
```

CMake and Ninja are build prerequisites. They are deliberately not embedded
into the application runtime.
