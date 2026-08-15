# Viewer Selection and Assembly Tree Identity

## Scope

This document records the ordinary selection contract for Part and Assembly
documents, its transition to confirmed selection, and the mapping between a
nested viewer occurrence and the Assembly Tree. It is intentionally separate
from mate/reference picking, where faces, edges, axes, planes and points are
valid candidates.

## Ordinary selection contract

- Part mode selects a complete historical container.
- Assembly mode selects a complete component occurrence.
- Hover, left-click confirmation and right-click cycling use the same ordered
  viewer candidate list.
- A confirmed candidate is highlighted as the exact selected object; result
  body topology is not converted into a persisted reference.
- Origin graphics and datum geometry are not ordinary component identities.
- Before confirmation, RMB changes only the active index in that same candidate
  list. After LMB confirmation, RMB opens the selected object's context menu.
- An active command may filter the viewer list through its explicit selection
  contract. It must not replace the list with a parallel picker.

## Assembly occurrence identities

An occurrence record contains:

- `top_component_id`: the component entity in the displayed Assembly;
- `instance_path`: nested component IDs below that top component;
- `key`: the stable viewer identity.

For a direct Part component, `instance_path` is empty and `key` is the top
component ID. For a Part inside a nested Assembly, `key` is encoded as
`assembly-occurrence:<top>/<child>/...`.

The viewer sends this key to `MainWindow._on_native_object_selected()`, which
passes occurrence selections to `_select_assembly_occurrence()`. The selected
record supplies both the exact overlay mesh and the Tree path; neither is
reconstructed from a source name or a result-body topology index.

`parent_key` records one hierarchy step upward. After an occurrence is LMB
confirmed, its context menu exposes **Select Parent** whenever `parent_key` is
present. The action selects that record through the same occurrence path.
Repeated actions therefore walk one level at a time through arbitrarily nested
Assemblies while keeping the Tree and viewport synchronized.

## Tree mapping rule

The Assembly Tree contains both the top-level component row and projected
source rows below it. Projected rows carry the component instance role and an
instance path.

`_find_component_instance_path_item()` therefore applies two different rules:

- a non-empty path may match a projected nested component row;
- an empty path may match only the actual top-level component row, whose
  `UserRole` is the top component ID.

Without the second restriction, a direct component selection recursively found
the first projected child with the same component role. In `00.asmz` that
child was `Počátek dílu`, so the Tree highlighted the Origin instead of the
selected Part. The fix is a Tree identity correction only; it does not alter
mesh ownership, reference picking, or nested occurrence generation.

## Verification case

The `Projects/00.asmz` data set verifies all three paths:

| View candidate | Expected Tree row |
|---|---|
| direct `11.prtz` component | `11.prtz` |
| direct `part.prtz` component | `part.prtz` |
| Part inside `10.asmz` | the corresponding projected Part row |

The test also confirms that the rendered Assembly mesh already uses the
top-level component IDs for direct Parts. The defect was therefore in Tree
path matching, not in the mesh cache or geometry owner assignment.

Also verify the interaction boundary for every row above:

1. hover the candidate and confirm that only its exact wire is orange;
2. use RMB before LMB and confirm that cycling changes the offered candidate;
3. confirm with LMB and check the exact Tree row and cyan overlay;
4. use RMB after confirmation and check that the object menu opens;
5. for the nested Part, invoke **Select Parent** repeatedly and verify one
   hierarchy step per invocation in both the Tree and viewport;
6. repeat with two occurrences of the same source document and confirm that
   their selections, activation paths and highlights remain distinct.

## Change control

Selection changes must be verified with the same direct-component and nested-
component cases before adding picker fallbacks or alternate owner mappings.
Fallbacks that recompute a different candidate violate the common viewer
selection contract and must not be introduced as a workaround.

Ordinary selection must remain a viewer-data path. It must not traverse OCCT,
regenerate a body, or infer an occurrence from names. Topology selection for
Fillet and Chamfer remains the explicit operational exception documented in
the repository rules; it does not widen ordinary Assembly selection.
