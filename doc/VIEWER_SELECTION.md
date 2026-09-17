# Viewer Selection and Assembly Tree Identity

## User selection filter (2026-09-17)

The View toolbar filter is a persistent, independent restriction on the common
3D candidate list. Every command intersects its own accepted kinds and ownership
rules with this restriction. Opening, changing or closing a command never resets
or broadens the user's filter. An incompatible choice deliberately produces no
candidate; the user selects another type or **All**.

Available types are All, Faces, Points, Axes, Planes, Edges/curves and Origins.
Faces are solid/reference faces; Planes are explicit datum and Origin planes.
Origins accepts only the origin point, not its axes or planes. Points also accepts
origin points. Sketch points, axes, curves and projected references follow their
corresponding types. All retains the existing ordinary leaf/container selection
contract; it does not expose otherwise forbidden result-body references.

The final user gate runs after command filtering, priorities and injected component
origin handles. Hover, LMB confirmation and RMB cycling consume that same result.
Changing the user filter clears stale View/Tree confirmation. Sketch rectangle
selection also respects the user type restriction. Geometry visibility is separate:
filtering a type out does not hide the model or show hidden Origins.

The audit covers the shared picker used by Part/Assembly reference entry,
component placement, construction and primitive properties, edge treatment,
Shell/Drill Point, measurement, appearance, orientation, Family Table references,
derived copies, sections, and Sketch selection/reference commands. Explicit Tree
navigation and free coordinate construction are not hover candidate queries.
The toolbar filter is disabled in Drawing, whose canvas has a separate 2D
interaction contract.

A separate Bodies filter is intentionally deferred: ordinary Part selection owns
history containers and Assembly selection owns exact component occurrences.
Neither is interchangeable with selecting an entire calculated result Body. Such
an option needs its own explicit ownership/selection contract.

Previously the toolbar index affected only ordinary scene selection; commands
could replace that contract and bypass it. Planes also used the Faces branch.
Regression coverage includes classification of every candidate kind, persistent
filtering across command replacement, hover/LMB/RMB, transient origin handles,
Sketch rectangle selection, and the actual toolbar and placement dialogs.

Verification: the focused UI regression initially failed with `User Axes filter
was bypassed by an active Face command`. After the repair, all ten targeted
contracts passed: viewer core, shared UI, translations, toolbar/placement filter,
Sketch origin picking, measurement inspector, component properties, edge treatment,
Sketch Coincident and Sketch endpoint priority. The actual workspace test includes
repeated Assembly occurrences and component-reference entry. Evidence:
`build/selection-filter-repro-test.log`, `build/selection-filter-tests.log` and
`build/selection-filter-regression-tests.log`. This follow-up is a development
change made after publication of Windows 2026091702; that immutable archive does
not contain this filter repair.

## Scope

This document records the ordinary selection contract for Part and Assembly
documents, its transition to confirmed selection, and the mapping between a
nested viewer occurrence and the Assembly Tree. It is intentionally separate
from mate/reference picking, where faces, edges, axes, planes and points are
valid candidates.

## Ordinary selection contract

### Assembly Origin depth (2026-09-17)

The displayed top-level Assembly Origin remains eligible for display. Ordinary
Origin visibility otherwise follows the exact active occurrence: the active
component's Origin and the Origins of an active Assembly's immediate components
are eligible. Deeper descendants and other branches remain hidden. Activating a
Part does not reveal sibling component Origins. The global Origin visibility
switch still applies.

Painting and the common picker consume the same visibility policy. Repeated
sources are distinguished by occurrence path. Feature dialogs using the shared
Origin action retain their stricter policy: only the top-level Origin is exposed
automatically, with other Origins revealed through explicit selection. Closing
the dialog restores the active-level policy.

Windows verification passed the component-properties GUI, Assembly refresh,
Assembly import and common UI contracts. The GUI regression checks the root,
active subassembly and active Part levels, rejects deeper and other-branch
origins, and exercises the explicit Origin exception. The captures
`Projects/test/origin-depth-top.png` and `Projects/test/origin-depth-active.png`
were visually inspected. Logs: `build/origin-depth-import-tabs-tests.log` and
`build/origin-depth-ui-retest.log` (after updating two synthetic dimension
fixtures to supply their explicit measurement directions).

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

## Context-menu ownership

In an Assembly, a confirmed component occurrence is an Assembly-owned object.
Its context menu may expose **Edit** and **Properties** for the component's
placement, mates and Assembly-level metadata. It must not expose source-Part
history commands such as creating or editing a Sketch, datum geometry, or
deleting a Part feature.

When the viewer hit is a solid or other internal geometry below an inactive
Part occurrence, the menu offers only occurrence actions such as **Activate
Part**, **Open component** and **Select Parent**. Part-history actions become
available only after activation changes the editable document to that exact
source Part. A nested occurrence resolves these actions against its immediate
owning Assembly; a parent Assembly never edits the internal history of a child
Part or nested Assembly.

The Tree and viewport use this same ownership rule. A displayed solid may be
mapped to its occurrence for selection, but that mapping must not be treated as
permission to edit the source document. The occurrence path remains the stable
identity for activation, properties, selection and parent traversal.

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
