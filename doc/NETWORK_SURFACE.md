# Surface modeling commands

The 2026-10-04 implementation adds a Modeling surface group immediately after
Drill Point: General Surface, Fill Surface, Sewing, Surface Intersection and
Surface Trim. Existing surface result modes in profile commands remain available.
The previous empty Surfaces application entry is hidden. Windows packaging and
final acceptance are recorded separately; development checks do not constitute
a published release.

## General Surface

Create ordinary Sketches and ordinary 3D Curves inside the surface container.
Their native parent relation is explicit. Sketch plane and offset are local to
the container Origin; its placement carries all boundary definitions together.
Open a boundary field with one click to use the ordinary editor, including its
external-reference capabilities. Pending nested edits remain inside the outer
transaction. Cancel discards them; General Surface OK commits the complete
definition. Add/remove boundary rows and Up/Down controls preserve object identity.

General Surface consumes the complete existing container-placement contract,
including whole-Origin references, orientation, correction and inspection. It
does not change the protected shared solver. Empty boundaries are drafts; their
exact authored curves must form a connected closed perimeter before calculation.

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

## Sewing and Fillet

Sewing selects current calculated surface faces and produces one connected
manifold shell. It preserves selected surface geometry and area, including
partial shared boundaries, and retains native source/endpoint ancestry. Unselected
solids and surface faces remain unchanged. Disconnected or nonmanifold results
are rejected; Sewing does not automatically cap a shell or create material.

Use the existing Fillet command on the real input edges of a sewn shell. Feasible
radii produce surface fillets without first converting the shell to a solid.
Excessive radii remain calculation errors. The ordinary Fillet selection,
rollback, persistence and Undo/Redo contracts apply.

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
