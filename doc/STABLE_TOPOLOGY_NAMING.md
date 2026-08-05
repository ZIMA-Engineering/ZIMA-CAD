# Stable Topology Naming

## Performance boundary

Stable topology is not a rendering requirement. Opening a document, inserting
a component, switching tabs, triangulating a shape and drawing visible edge
curves must use the evaluated BREP/mesh directly and must not enumerate a full
`FaceRef`/`EdgeRef`/`VertexRef` registry.

The preferred persistent sources are the original parametric solid, its own
declared faces/edges and Boolean intersection curves with known operands.
Fillet selection first maps a click on the result Body back to an original
history solid and names that solid's edge. Whole-result topology is permitted
only when the newly selected edge is a genuine Boolean intersection with no
original-solid owner. Legacy result-body references are not restored.

Assembly face and circular-edge picks follow the same rule. The picker tests
the original Part history solids through the component transform and registers
only the selected plane/axis frame in the mate solver. Reopening a component
restores only descriptors actually stored in its mate rows; it never enumerates
all source faces and edges. Original-solid references are resolved directly,
and highlighting does not rebuild whole-result topology merely to obtain a
temporary result-body index.

Large imported STEP sources follow the same lazy rule. When eager topology is
disabled for a source with more than 10,000 faces, selecting one face creates
only its `FaceRef(feature_id, "imported", source_index)` and resolves that one
subshape directly from the original imported solid. A confirmed Assembly face
keeps a transient `(component_id, displayed_face_index)` solely for cyan
viewport highlighting; this runtime pair is never serialized.

Format 10 is the clean boundary for this model. Earlier experimental document
versions and their result-body references are deliberately unsupported.

`PartDocument` caches a requested topology registry by the cumulative geometry
signature. Repeated consumers therefore share one result, while editing a
feature produces a different signature and cannot reuse stale topology. This
cache limits transitional cost; it is not permission to restore eager registry
creation in the viewer or Assembly insertion path.

## Current implementation status (August 2026)

The central implementation is active, not only a design proposal:

- `FaceRef`, `EdgeRef`, `VertexRef`, `AssemblyFaceRef`, explicit resolution
  states and `TopologyRegistry` are implemented;
- `AssemblyEdgeRef` has canonical instance-plus-edge descriptors and
  per-instance `RESOLVED`/`MISSING`/`AMBIGUOUS` resolution; runtime edge indices
  and legacy edge descriptors are rejected. Circular component edges use these
  descriptors in concentric mate choices, view picking and highlighting;
- a concentric mate between two instances remains `RESOLVED` after a source
  diameter edit, changes to `MISSING` during feature suppression and recovers
  without changing its payload or component placement;
- `AssemblyFaceRef` has canonical serialization and resolution through a
  per-instance registry map, so two occurrences of the same Part remain
  distinct; the UI stores only this format and rejects legacy descriptors;
- a saved planar mate between two instances of a Boolean Part resolves both
  faces after source dimension regeneration; suppressing the referenced source
  feature produces `MISSING` instead of rebinding to a runtime index;
- Assembly mate rows with `MISSING` or `AMBIGUOUS` faces are retained, shown as
  unresolved in the editor and excluded from solving until they resolve again;
- a tested mate completes `RESOLVED → AMBIGUOUS → RESOLVED` across a temporary
  Boolean face split without changing its stored `AssemblyFaceRef` or selecting
  a generated fragment;
- Box and Wedge faces have semantic roles; their boundary edges and vertices
  are derived from canonical sets of incident semantic faces rather than OCCT
  traversal order;
- Segment-, center-arc-, ellipse-, elliptical-arc- and supported spline-profile feature faces, edges and
  start/end vertices are named from persistent Sketch entity IDs; circular
  and elliptical Extrusion faces and cap edges use the persistent Sketch ID.
  Closed ellipses deliberately do not expose the kernel-created seam edge or
  seam vertex as stable user topology; elliptical arcs expose their real
  endpoint vertices. Complete 360-degree Revolves expose only generated
  topology because they have no seam caps;
- supported external Sketch references persist semantic dictionaries instead
  of OCCT enumeration indices;
- newly selected container face/orientation constraints persist semantic
  `FaceRef` dictionaries; current development files with one uniquely matching
  stored plane equation are upgraded without trusting their numerical index;
- supported Part `Fuse`/`Cut` history propagates existing semantic face, edge
  and vertex ancestry; splits expose deterministic fragment references while
  the pre-split reference remains explicitly ambiguous, and merges never bind
  silently to one ancestry;
- new Boolean section edges and their vertices receive kernel-independent
  ZIMA identities from canonical sets of adjacent/incident semantic `FaceRef`
  values; OCCT section history only locates the transient runtime shapes;
- point, edge and face references survive tested parent dimension/profile
  edits, save/reload and automatic descendant regeneration;
- unresolved or ambiguous registry entries do not silently select another
  runtime shape.

Existing supported ancestry and newly created section edges/vertices now have
ZIMA-owned provenance through the initial additive/subtractive Part subset.
The next supported operation is wider curved-profile and multi-intersection
coverage. Circular, closed-spline and mixed nested segment/arc/spline Extrusion,
full closed-spline Revolve and repeated full center-arc Revolve cuts are covered
through dimension changes and save/reload. Three-level Extrusion nesting keeps
outer/hole/island lateral provenance and deterministically fragments multiple
cap faces. Partial Revolve now applies the same rule to its repeated start/end
faces, edges and vertices. General multi-body Booleans and Chamfer are not yet
implemented. The first history/UI Fillet resolves its input through a stable
edge reference, propagates existing ancestry and names its generated face from
that edge. Edges and vertices of the Fillet result are derived from their
semantic face incidence, so every result edge remains selectable as a stable
input for a following feature. Repeated cuts across a
two-solid source now have
regression coverage for face, edge and vertex ancestry. An additive bridge can
join that source into one solid while preserving supported source/bridge
ancestry through a following cut. Crossing cylindrical Extrusion and toroidal
center-arc Revolve cuts also preserve the first curved tool's ancestry in the
second tool's intersection references. A closed-spline Extrusion used after the
cylindrical cut has equivalent curved-to-spline coverage. Legacy numerical
topology references and Assembly descriptors are intentionally not
migrated from an index alone; development files may be recreated.

## Problem

Unsupported ZIMA-CAD operations may still expose a general reference as an
entity or instance ID combined with a temporary OpenCascade topology index.
The owner ID is stable, but the numerical index is not. Recomputing a feature
or changing a Boolean operation may reorder or replace OCCT subshapes.

Box, Wedge, Extrusion and Revolve provide the continuous semantic feature
subset. Supported Part Boolean history and Assembly face/edge workflows also
propagate these identities, but general Part operations, all Boolean
combinations and all Assembly reference types are not yet covered.

The implemented rule is:

> A persistent topology identity is determined by its modeling origin and
> semantic role, not by its position in an OpenCascade enumeration.

## Ownership of identity

OpenCascade creates and evaluates geometry, but ZIMA-CAD owns persistent
identity. A `TopoDS_Face` is a runtime result and must not itself be treated as a
persistent model object.

Inputs must have stable IDs before an operation is evaluated:

- Part, Body/container and feature IDs;
- Sketch and sketch-entity IDs, including individual edges;
- stable references to input topology where required.

After OpenCascade evaluates a supported operation, ZIMA-CAD assigns semantic
identities to the result from role and provenance. For example:

```text
Extrusion_1 / start
Extrusion_1 / end
Extrusion_1 / generated / SketchEdge_B
Extrusion_1 / generated-edge / SketchPoint_C
Extrusion_1 / end-vertex / SketchPoint_C
```

## Persistent reference model

A logical topology reference contains data equivalent to:

```python
@dataclass(frozen=True)
class FaceRef:
    feature_id: str
    role: str
    source_id: str | None = None
    fragment: int | None = None
```

Example serialized value:

```json
{
  "feature_id": "extrusion-uuid",
  "role": "generated",
  "source_id": "sketch-edge-uuid",
  "fragment": null
}
```

`fragment` distinguishes multiple faces produced from one logical source when
an operation splits topology. It must be assigned deterministically; a plain
OpenCascade enumeration index is not sufficient.

An Assembly reference additionally identifies the component instance:

```python
@dataclass(frozen=True)
class AssemblyFaceRef:
    instance_id: str
    part_face_ref: FaceRef
```

Multiple instances may therefore use the same face definition from the source
`.prtz`, while the Assembly still identifies the exact placed component.

## Runtime topology registry

Each evaluated model result should carry both its OpenCascade shape and a
bidirectional runtime mapping:

```python
@dataclass
class EvaluatedShape:
    shape: TopoDS_Shape
    topology: TopologyRegistry

@dataclass
class TopologyRegistry:
    faces_by_ref: dict[FaceRef, TopoDS_Face]
    edges_by_ref: dict[EdgeRef, TopoDS_Edge]
    vertices_by_ref: dict[VertexRef, TopoDS_Vertex]
    refs_by_face: dict[RuntimeShapeKey, FaceRef]
```

The two directions support different workflows:

- semantic reference to OCCT subshape resolves persisted Part, Assembly,
  attachment and Drawing references after recomputation;
- runtime index to semantic reference converts topology selected in the viewer
  into a persistent identity. Face, edge and vertex maps are separate so the
  same runtime index cannot collide across topology kinds.

Runtime shape keys and `TopoDS_Face` instances are never persisted as stable
identity.

## Command selection

Interactive commands use one kernel-independent `SelectionController` above
the viewer. A `SelectionRequest` declares allowed object/face/edge/point/axis/
plane kinds, minimum and maximum count, a command validator/resolver and
completion/cancellation callbacks. The viewer only reports transient picked
candidates; the resolver converts supported topology to stable references.
Fillet is the first migrated client and requests one stable `EdgeRef` after
command activation. Chamfer, Assembly and Sketch reference workflows should be
migrated to the same controller instead of adding command-specific pick flags.

## Operation contract

Geometry builders should eventually return `EvaluatedShape`, not only a naked
`TopoDS_Shape`. The current implementation builds a registry alongside the
supported history snapshot; each builder remains responsible for provenance.

Extrusion and Revolve implement these initial face roles with the same
provenance rule where applicable:

- `start`;
- `end`;
- `generated`, linked to a stable sketch-edge ID.

Where the selected OCCT algorithm exposes operation history, use its generated,
modified and deleted mappings. Subsequent operations must propagate identity:

- an unchanged or modified replacement face retains its logical reference;
- a newly generated face receives a reference owned by the current feature;
- a deleted face becomes unresolved;
- a split face produces deterministic fragments;
- merged or otherwise ambiguous ancestry must be represented explicitly rather
  than guessed silently.

Boolean operations, Fillet and Chamfer are intentionally later stages because
they introduce the hardest split, merge and deletion cases.

## Geometric signatures

A face may also carry a diagnostic signature containing suitable values such
as:

- surface type;
- area and centroid;
- normal or axis;
- radius where applicable;
- boundary and adjacency information.

The signature is a validation and recovery aid, not the primary identity. Model
dimensions and positions can legitimately change, and multiple faces can have
the same geometry. It may support migration from old `Face N` data or present
ranked repair candidates, but it must not silently bind an ambiguous reference.

## Resolution states and failure behavior

Reference resolution must return an explicit state, at least:

```text
resolved
missing
ambiguous
incompatible
```

When a reference cannot be resolved uniquely, ZIMA-CAD must:

- not substitute the current face with the same `Face N`;
- preserve the last valid Assembly placement or dependent transform where
  possible;
- mark the mate or attachment as broken;
- let the user select a replacement face;
- report ambiguity instead of choosing a candidate silently.

Some references are legitimately unrecoverable after the modeling source is
deleted or fundamentally changed. Reporting a lost reference is then the
correct result.

## Implementation stages

1. **Done:** introduce semantic reference types, resolution states and a central
   `TopologyRegistry`.
2. **Done:** move semantic Box and Wedge face roles and face-adjacency-derived
   boundary edge/vertex identities into the registry.
3. **Done:** use stable Sketch entity IDs for Extrusion faces, edges and
   vertices.
4. **Done for the supported subset:** store and resolve semantic faces for
   Assembly selection and semantic faces/edges/vertices for external Sketch
   references.
5. **Done:** implement equivalent face, edge and vertex mapping for Revolve.
6. Add complete missing/ambiguous diagnostics, UI indication and manual repair.
7. **Initial supported subset done:** propagate existing semantic ancestry
   through additive/subtractive Part results, including explicit split and
   merge ambiguity, and assign ZIMA-owned intersection-edge/vertex identities.
   Next expand curved-profile, repeated-intersection and general Boolean
   coverage.
8. Reuse the registry for remaining Part attachments and Drawing associations.
9. **Fillet subset done:** a single selected stable edge is stored in Part
   history, its radius is editable, ancestry is propagated, the generated face
   is named and all result edges receive stable incidence-derived identities
   for following features. Next add multi-edge selection and apply the same
   contract to Chamfer.
10. Consider legacy numerical-reference migration only if it becomes a product
    requirement; current development intentionally requires recreation.

## Required regression scenarios

- Changing Extrusion length preserves its semantic faces, edges and vertices.
- Editing a Sketch without deleting referenced entities preserves external
  point, edge and face references.
- Deleting the source edge marks the reference missing.
- Reordering unrelated features does not change references.
- Two instances of one `.prtz` resolve the same Part `FaceRef` independently.
- Split and ambiguous results never bind silently to a different face.
- Saving and reopening `.prtz` preserves tested Extrusion and Revolve semantic
  references; Assembly coverage must expand with Boolean propagation.
- A legacy numerical reference is rejected rather than silently rebound.

## Scope and expectation

This is a model-kernel feature spanning Part evaluation, topology selection,
storage, Assembly mates and later Drawings. It must be implemented centrally
and covered by regression tests rather than patched only in the mate dialog.

The practical goal is a highly reliable naming system for supported modeling
operations, not a promise to infer user intent after every arbitrary topology
change.
