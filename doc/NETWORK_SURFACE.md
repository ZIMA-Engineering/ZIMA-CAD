# Surface modeling commands

The 2026-10-04 implementation adds a Modeling surface group immediately after
Drill Point: General Surface, Fill Surface, Sewing, Surface Intersection and
Surface Trim. Existing surface result modes in profile commands remain available.
The previous empty Surfaces application entry is hidden. Windows packaging and
final acceptance are recorded separately; development checks do not constitute
a published release.

## General Surface

Create ordinary Sketches and ordinary 3D Curves inside the surface container.
Their native parent relation is explicit. Each Sketch is a complete ordinary
Sketch Feature with its own Origin and placement relative to the General
Surface Origin. Its properties retain ordinary placement references, numeric
correction, plane, offset and Sketch editing; conversion to another Feature
type is disabled. Each 3D Curve directly inherits the General Surface frame
and has no independent container-placement controls. Its child points retain
their ordinary editing capabilities. Moving the General Surface carries its
unanchored definitions together; external placement references remain anchored.
Choose Sketch or 3D Curve in the row type dropdown, then click its boundary
field to open the ordinary properties window. Sketch properties own the local
plane and plane offset and open the ordinary Sketch editor, including its
external-reference capabilities. Pending nested edits remain inside the outer
transaction. Cancel discards them; General Surface OK commits the complete
definition. Add a boundary through the shared entry arrow or field in the trailing row.
There are no separate bottom add actions or plane/offset columns. Row numbers
use the same vertical header as container placement. All authored boundaries
remain visible while editing the surface or another owned Sketch, Curve or Point.
The independent eye toggles azure inspection of the exact boundary wire;
turning it off leaves the boundary visible as context. It does not open an
editor, reorder the row, recalculate geometry or republish the base scene. Empty definitions can
change type; a drawn definition keeps its type and native identity. Remove it
and add a new definition when a different type is required. All rows may be
removed, including the last one. The trailing entry row remains available.
Add/remove rows and Up/Down controls preserve the identities of retained objects.

After General Surface OK, its owned Sketches, 3D Curves and defining points are
hidden in the ordinary View; the calculated surface remains visible. Their Tree
rows and native definitions remain available. Selecting a hidden boundary in
the Tree temporarily highlights that exact wire. **Show/Hide** on the boundary
row controls its ordinary visibility and survives native save/reopen and
Undo/Redo. Opening the Surface or its owned geometry restores the definitions
as editing context; closing Properties restores the ordinary display state.
Wire Show/Hide is offered for standalone Sketches, Curves, Intersection wires
and owned Surface boundaries. It is not offered on Extrusion or Revolution
containers: hiding their internal Sketch would not hide the calculated body.

General Surface consumes the complete existing container-placement contract,
including whole-Origin references, orientation, correction and inspection. It
does not change the protected shared solver. Empty boundaries are drafts; their
exact authored curves must form a connected closed perimeter of at least two
boundaries before OK can calculate and commit. Native Sketch boundaries persist
their own Feature definition, placement and explicit General Surface parent;
the stored resolved Sketch frame remains local to that parent. Display and
calculation compose the frames without changing Sketch coordinates or references.

## Fill Surface

The former Boundary Surface command is now Fill Surface (`Zaplnit plochu`).
It retains references to preceding Sketch curves/chains, 3D Curve chains or
original native surface edges in the active editable Body. Two closed arcs,
triangular perimeters and general N-sided contours are supported; two separate
rails alone are not a closed perimeter. Automatic orientation does not modify
the inputs or invent connecting curves.

Each boundary independently requests G0, G1 or G2. G0 means positional contact;
G1 adds a common tangent plane; G2 adds curvature continuity. G1/G2 require a
native edge and its explicit support face. Support-side reversal is retained.
Calculation independently checks fitting distance, normal and curvature errors;
it rejects an unsatisfied constraint instead of silently lowering continuity.

Explicit calculation uses OCCT filling and verifies perimeter closure, crossings,
positive area and shape validity. The result has no material volume. Boundaries
constrain the interior but do not uniquely prescribe every possible spanning
surface. Highly twisted or incompatible definitions may be rejected.

The final B-spline boundary is checked independently of OCCT's intermediate
plate error. The usual approximation is retained when it meets document
precision. Otherwise explicit calculation allows at most two finer fits,
then rejects an inaccurate result. Edge representation tolerances may only
cover measured errors within the document tolerance; authored 3D curves and
their native identities are retained. This additional work happens on OK or
Regenerate, never on opening Properties, hovering or unchanged confirmation.

## Sewing and Fillet

Sewing selects current calculated surface faces and produces one connected
manifold shell. It preserves selected surface geometry and area, including
partial shared boundaries, and retains native source/endpoint ancestry. Unselected
solids and surface faces remain unchanged. Disconnected or nonmanifold results
are rejected; Sewing does not automatically cap a shell or create material.

Sewing Properties keeps a two-row reference viewport with fixed row heights.
Longer lists scroll without spreading the upper fields when the window grows.
Adding or removing a face scrolls the trailing input row into view; independent
inspection leaves the entry and scroll state unchanged.

Use the existing Fillet command on the real input edges of a sewn shell. Feasible
radii produce surface fillets without first converting the shell to a solid.
Excessive radii remain calculation errors. The ordinary Fillet selection,
rollback, persistence and Undo/Redo contracts apply.

Surface Fillet and Chamfer faces retain the yellow surface style and participate
in surface visibility. The classification is per face, so a Part containing
both a sewn shell and unrelated solids keeps ordinary solid appearance and
mass properties. Existing calculated files require explicit Regenerate to
replace their stored approximation or surface classification.

## Surface Intersection

Select two original bounded faces. The command creates exact reusable native
curves and isolated contact points while leaving both input surfaces unchanged.
Disjoint faces give an empty result; coincident areas are rejected because they
do not define a unique curve. Periodic section pieces are joined without fitting
a smoother substitute. A closed branch has no invented point at its parameter
seam. Exchanging face fields preserves branch and endpoint references.

Both faces and native endpoint parents define branch identity. A changed source
invalidates calculation reuse. A removed or merged branch does not silently bind
a dependent reference to another branch. Fill and Trim can use the exact edge;
a compatible Sweep can consume it through ordinary Sketch External Geometry
in its owned path. Direct arbitrary 3D Curve path conversion is not introduced.

## Surface Trim

Select one current calculated surface face, preceding cutting faces or native
edges, then click inside the region to retain. For face tools, Trim calculates
their bounded intersection internally; no separate Intersection feature is needed.
A curve tool must already lie on the bounded target and separate it. Trim does
not project arbitrary tools or extend either input.

Native curve tools include Surface Intersection branches and existing surface
edges. Standalone Sketch or 3D Curve definitions are not accepted directly as
Trim edge tools by the current calculation contract.

After Trim OK, used authored cutting curves and Surface Intersection wires,
including their points, are hidden. Face tools remain visible. Opening Trim
Properties temporarily restores its stored cutting wires, including manually
hidden wires; OK or Cancel restores ordinary visibility. Their Tree **Show/Hide**
actions remain available. A manual visibility choice takes precedence over
automatic hiding; it is presentation data, not suppression or a geometry edit.
Undoing a Trim restores the previously visible, unconsumed cutting wire unless
it was separately hidden. Native references and calculation inputs are retained.

The selected fragment retains the same underlying surface and orientation, with
real native trim boundaries. Unselected solids/surface faces and tool geometry
remain unchanged. A closed cutting loop can retain its interior or its exterior
with a real hole. A seed on the separating boundary is rejected as ambiguous.
The seed is obtained from the already offered face's viewer triangles; explicit
calculation resolves it on the exact surface. Picking never invokes OCCT.

The native Part stores target/tools, seed and the selected boundary ancestry.
Regeneration rejects a changed region signature instead of silently keeping the
opposite fragment. Reopen Properties and explicitly select a region to repair
that intent. The signature is validation data, excluded from the geometric
fingerprint; cached reuse still verifies it. Capturing it after calculation
therefore does not require another split calculation.

## Shared interaction, persistence and limits

Each command uses one internal Properties window for creation and editing, with
OK and Cancel, shared reference controls and independent inspection. Opening
Properties publishes the actual rollback input once. Draft entry, hover and
inspection use persisted viewer geometry. Unchanged OK creates no calculation
or Undo transaction; Cancel restores the normal scene. Native documents own all
required definitions and references; no required sidecar is introduced.

General Surface Properties initially provides room for at least five boundary
rows on a sufficiently tall application window. Boundary definition points use
their ordinary Sketch/datum colors; only inspected definitions use azure. The
expanded Tree permits selecting an owned Sketch or 3D Curve independently and
highlights only that boundary's native wire, including hidden definitions,
without recalculating geometry.
The existing child Properties routes remain available. Surface Intersection
curves also offer their owning history feature through ordinary View hover,
candidate cycling and confirmation. Fill Surface consumes source boundaries
and has no unrelated Origin action.

Hidden geometry has gray Tree names, including descendants of a hidden Body or
component; visibility does not add an eye badge or a hidden-name suffix. Showing
an item restores its normal name appearance. Reference errors retain their error
color, and suppression retains its separate history state. Body visibility also
hides its construction wires and their points. Solid-producing feature rows do
not offer wire Show/Hide; use suppression to change their contribution to history.
Section visibility controls its plane, independently of whether the cut is active.
Saved centroid visibility controls its Origin and shades both record and Origin.

Suppression cascades to dependent features. Restoring a feature restores its
earlier prerequisites; later dependents remain suppressed until explicitly
restored. Deleted inputs retain repairable unresolved history features.

Native identity must be unambiguous from source parents. Multiple branches or
trim corners with indistinguishable parent sets are rejected; there is no
enumeration or coordinate-based identity fallback. The tests cover representative
planar, curved and B-spline cases, not every possible kernel input. Grid/Mesh
editing and G3 are explicitly deferred.

Verification details and measured timings are in
[the development record](SURFACE_OPERATIONS_DEVELOPMENT.md).
Primary kernel references: [Filling](https://occt3d.com/dev/doc/refman/html/class_b_rep_offset_a_p_i___make_filling.html),
[Section](https://occt3d.com/dev/doc/refman/html/class_b_rep_algo_a_p_i___section.html),
[Splitter](https://occt3d.com/dev/doc/refman/html/class_b_rep_algo_a_p_i___splitter.html),
and [Sewing](https://occt3d.com/dev/doc/refman/html/class_b_rep_builder_a_p_i___sewing.html).
