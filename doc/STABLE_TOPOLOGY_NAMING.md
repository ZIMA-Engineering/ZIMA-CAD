# Stable Topology Naming

## Current implementation status (August 2026)

The central implementation is active, not only a design proposal:

- `FaceRef`, `EdgeRef`, `VertexRef`, `AssemblyFaceRef`, explicit resolution
  states and `TopologyRegistry` are implemented;
- Box and Wedge faces have semantic roles;
- Extrusion cap/lateral faces, cap/generated edges and start/end vertices are
  named from persistent Sketch entity IDs;
- supported external Sketch references persist semantic dictionaries instead
  of OCCT enumeration indices;
- point, edge and face references survive tested parent dimension/profile
  edits, save/reload and automatic descendant regeneration;
- unresolved or ambiguous registry entries do not silently select another
  runtime shape.

The next supported operation is Revolve. Identity propagation through additive
and subtractive results, general Booleans, Fillet and Chamfer is not yet
implemented. Legacy numerical topology references are intentionally not
migrated at this stage; development files may be recreated.

## Problem

Unsupported ZIMA-CAD operations may still expose a general reference as an
entity or instance ID combined with a temporary OpenCascade topology index.
The owner ID is stable, but the numerical index is not. Recomputing a feature
or changing a Boolean operation may reorder or replace OCCT subshapes.

Box, Wedge and Extrusion now provide the first continuous semantic subset, but
the system does not yet cover general Part history, Revolve, Boolean results or
all Assembly references.

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

## Operation contract

Geometry builders should eventually return `EvaluatedShape`, not only a naked
`TopoDS_Shape`. The current implementation builds a registry alongside the
supported history snapshot; each builder remains responsible for provenance.

Extrusion implements these initial face roles, and Revolve should follow the
same provenance rule where applicable:

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
2. **Done:** move semantic Box and Wedge face roles into the registry.
3. **Done:** use stable Sketch entity IDs for Extrusion faces, edges and
   vertices.
4. **Done for the supported subset:** store and resolve semantic faces for
   Assembly selection and semantic faces/edges/vertices for external Sketch
   references.
5. **Next:** implement equivalent face, edge and vertex mapping for Revolve.
6. Add complete missing/ambiguous diagnostics, UI indication and manual repair.
7. Extend history propagation to additive/subtractive results and general
   Boolean operations.
8. Reuse the registry for remaining Part attachments and Drawing associations.
9. Add Fillet and Chamfer only after split/merge behavior is covered by tests.
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
- Saving and reopening `.prtz` preserves tested Extrusion semantic references;
  Assembly coverage must expand with Boolean propagation.
- A legacy numerical reference is rejected rather than silently rebound.

## Scope and expectation

This is a model-kernel feature spanning Part evaluation, topology selection,
storage, Assembly mates and later Drawings. It must be implemented centrally
and covered by regression tests rather than patched only in the mate dialog.

The practical goal is a highly reliable naming system for supported modeling
operations, not a promise to infer user intent after every arbitrary topology
change.
