# Stable Topology Naming

## Problem

ZIMA-CAD currently persists general face references as an entity or instance ID
combined with a temporary OpenCascade face index (`Face N`). The owner ID is
stable, but the face index is not. Recomputing a feature or changing a boolean
operation may reorder or replace `TopoDS_Face` instances, causing a reference to
resolve to the wrong face.

Box and Wedge already provide a limited semantic precedent (`x_min`, `x_max`,
`slope`, and similar roles), but there is no continuous naming system covering
general Part features and Assembly mates.

The essential rule is:

> A persistent face identity is determined by its modeling origin and semantic
> role, not by its position in an OpenCascade face enumeration.

## Ownership of identity

OpenCascade creates and evaluates geometry, but ZIMA-CAD owns persistent
identity. A `TopoDS_Face` is a runtime result and must not itself be treated as a
persistent model object.

Inputs must have stable IDs before an operation is evaluated:

- Part, Body/container and feature IDs;
- Sketch and sketch-entity IDs, including individual edges;
- stable references to input topology where required.

After OpenCascade evaluates an operation, ZIMA-CAD assigns semantic identities
to the resulting faces from their role and provenance. For example:

```text
Extrusion_1 / start
Extrusion_1 / end
Extrusion_1 / generated / SketchEdge_B
```

## Persistent reference model

A logical face reference should contain data equivalent to:

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
    refs_by_face: dict[RuntimeShapeKey, FaceRef]
```

The two directions support different workflows:

- `FaceRef -> TopoDS_Face` resolves persisted Part, Assembly, attachment and
  Drawing references after recomputation;
- `TopoDS_Face -> FaceRef` converts a face selected in the viewer into a
  persistent reference.

Runtime shape keys and `TopoDS_Face` instances are never persisted as stable
identity.

## Operation contract

Geometry builders should eventually return `EvaluatedShape`, not only a naked
`TopoDS_Shape`. Each builder is responsible for describing the provenance of
the topology it creates.

For Extrusion and Revolve, the first supported roles should be:

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

1. Introduce `FaceRef`, `AssemblyFaceRef`, resolution states and a central
   `TopologyRegistry` without changing persisted behavior.
2. Move the existing semantic Box and Wedge roles into the registry.
3. Ensure Sketch edges have stable persistent IDs and implement semantic output
   faces for Extrusion.
4. Implement the equivalent mapping for Revolve.
5. Change Assembly mates from `instance/entity ID + Face N` to
   `instance ID + FaceRef`.
6. Add missing/ambiguous-reference diagnostics, UI indication and manual repair.
7. Migrate legacy `Face N` references when possible, retaining a clear broken
   state when migration is uncertain.
8. Extend history propagation to cuts and Boolean operations.
9. Add Fillet and Chamfer only after split/merge behavior is covered by tests.
10. Reuse the same registry for Part attachments, external Sketch references and
    Drawing associations instead of creating separate identity systems.

## Required regression scenarios

- Changing Extrusion length preserves references to its start, end and lateral
  faces.
- Editing a Sketch without deleting the referenced edge preserves the lateral
  face reference.
- Deleting the source edge marks the reference missing.
- Reordering unrelated features does not change references.
- Two instances of one `.prtz` resolve the same Part `FaceRef` independently.
- Split and ambiguous results never bind silently to a different face.
- Saving and reopening `.prtz` and `.asmz` preserves semantic references.
- Legacy `Face N` documents either migrate deterministically or show a repairable
  broken reference.

## Scope and expectation

This is a model-kernel feature spanning Part evaluation, topology selection,
storage, Assembly mates and later Drawings. It must be implemented centrally
and covered by regression tests rather than patched only in the mate dialog.

The practical goal is a highly reliable naming system for supported modeling
operations, not a promise to infer user intent after every arbitrary topology
change.
