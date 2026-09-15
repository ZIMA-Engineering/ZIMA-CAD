# Native behavioral contract

## Purpose

This document records the user-approved model and interaction invariants for
ZIMA-CAD's native C++ application. Python is retired and is no longer a runtime,
reference implementation, or compatibility requirement.

A feature is evaluated as one complete flow:

```text
persisted model and ownership
→ Tree and normal View presentation
→ active-command selection contract
→ hover / RMB cycle / LMB and Tree confirmation
→ constraint solving and remaining degrees of freedom (DOF)
→ transient preview and dimensions
→ OK / Cancel / edit lifecycle
→ save / load / explicit regeneration
```

Compilation or a similar-looking dialog alone does not establish correctness.
Current explicit user requirements and [AGENTS.md](../AGENTS.md) take precedence.
Use the focused documents below and native behavioral tests to verify a change;
old migration checklists are not a statement of current implementation status.

## Documentation routing

| Area | Contract and implementation guide |
| --- | --- |
| Engineering decisions | [Engineering reasoning](AI/ZIMA_ENGINEERING_REASONING.md) |
| Native modules | [C++ architecture](CXX_ARCHITECTURE.md), [workspace source map](WORKSPACE_SOURCE_MAP.md) |
| Selection and identities | [Viewer selection](VIEWER_SELECTION.md), [stable topology](STABLE_TOPOLOGY_NAMING.md) |
| Containers and rollback | [History editing](HISTORY_EDITING.md), [history reordering](HISTORY_TREE_REORDER.md) |
| Placement and reference removal | [Placement](PLACEMENT_COMMANDS.md), [reference removal](PLACEMENT_REFERENCE_REMOVAL.md) |
| Assembly ownership and regeneration | [Assembly references](ASSEMBLY_REFERENCES.md), [geometry sharing](ASSEMBLY_GEOMETRY_SHARING.md) |
| Sketcher | [Sketcher](SKETCHER.md), [Sketch model](SKETCH_MODEL.md), [terminology](SKETCHER-TERMINOLOGY.md) |
| Drawing | [Drawings](DRAWINGS.md), [dimension symbols](CAD-DIMENSION-SYMBOLS.md), [dimension ranges](DIMENSION_RANGES.md) |
| GUI and CLI | [Command coverage](CAD_COMMAND_COVERAGE.md), [GUI audit](CLI_GUI_AUDIT.md) |
| Distribution | [Release policy](PORTABLE_RELEASE.md), [Windows dependencies](WINDOWS_RUNTIME_AND_BUILD.md) |

## Global behavioural invariants

### Confirmed hierarchical history model

Confirmed with the user on 2026-08-20:

- History is hierarchical and is evaluated independently at every ownership
  level; it is not one flat global list spanning the complete document tree.
- A Part level has one chronological history of its direct user-created
  Containers. A Container level has its own chronological history of its
  direct children, including nested Containers and ordinary owned objects.
- The same rule applies recursively to Assemblies, nested Assemblies, Parts
  and Containers: Tree presents the history belonging to the exact displayed
  ownership level.
- Each local history is heterogeneous. Point, Axis, Plane, primitive, Sketch,
  Extrusion, Revolution, Fillet, Chamfer and every other direct child retain
  their real order at that level; they are never grouped or reordered by type.
- The Part Origin and calculated result Body are system objects outside this
  user Container history.
- Separate implementation collections may exist only as indexes or typed
  storage. They must not become competing histories or lose the authoritative
  local ordering and parent-child ownership at any hierarchy level.

#### Reference example: 3D Curve Container

Confirmed with the user on 2026-08-20:

```text
3D Curve Container
├── Container Origin
├── Point Container 1
├── Point Container 2
├── ...
├── Point Container N
└── Curve object
```

- The 3D Curve is a Container with its own mandatory Origin.
- It owns multiple nested Point Containers and one Curve object.
- The Point Containers have an authoritative local order inside the 3D Curve
  Container.
- That Point-Container order is engineering input: it determines the sequence
  of points through which the owned Curve passes. Reordering the nested Point
  Containers therefore changes the Curve calculation; it is not merely a Tree
  presentation change.
- The Curve object and the nested Point Containers retain distinct identities
  and the same parent owner; neither is flattened into the other.
- This example does not define a universal rule that ordinary objects are
  always derived results outside local order, or that only nested Containers
  may participate in it. Another Container type may assign different roles
  and ordering semantics to its children.
- Input/result/history meaning is defined by the owning Container's explicit
  model and child relationships, never inferred globally from the child's
  object type.

### Ownership, order and dependency are separate relations

Confirmed with the user on 2026-08-20:

- The model distinguishes at least three independent concepts:
  1. ownership/parent-child hierarchy;
  2. authoritative local order within an ownership level;
  3. cross-object dependency/reference edges.
- A Container may consume a Sketch owned by another Container. This creates a
  dependency/reference; it does not move, copy or re-parent the source Sketch.
- Tree placement follows ownership. Calculation order and invalidation also
  respect explicit dependencies, which may cross ownership branches.
- A reference identifies the exact persisted source object (and, where
  applicable, its exact sub-entity and Assembly instance path). A display
  name is never reference identity.
- Ownership, local order and dependency must not be collapsed into one field
  or inferred from one another.
- When a source object changes inside the same Part, every transitive dependent
  object is invalidated. At the next permitted history calculation boundary,
  dependents are evaluated in dependency-correct order using the changed
  source.
- Invalidation is not permission for hidden OCCT work. Hover, Tree refresh,
  selection and ordinary View rebuild remain non-calculating operations.
- Changes across a Part/Assembly document boundary continue to follow the
  explicit parent-Assembly Regenerate contract; an open parent Assembly is not
  recalculated merely because its dependency changed.
- Dependency cycles are forbidden. Creating or editing a reference must be
  rejected transactionally if it would introduce a direct self-cycle or any
  indirect cycle in the dependency graph. The unchanged valid model remains
  committed; cycle detection is not deferred until calculation.
- A consumer may reference only historical data that already exists at the
  consumer's exact evaluation boundary. It must never read an object,
  topology or calculated state produced later in the relevant local history.
- This causal-history rule is validated both when a reference is created or
  edited and when history items are reordered. Absence of a dependency cycle
  alone is not sufficient to make a forward reference valid.
- Reordering a history source also moves its complete transitive dependent
  chain as one dependency-valid group when required. The operation preserves
  causal order instead of merely rejecting the user's move or leaving forward
  references behind.
- Automatic dependency-chain movement changes order only. It must not change
  ownership, duplicate objects or weaken cycle validation.
- Automatic dependency-chain movement is strictly local to the ordered direct
  children of one owner. It never crosses a parent-Container boundary, moves
  the parent itself, reorders a sibling ownership branch or changes another
  document's history.

### Reference removal and persistent fallback state

Confirmed with the user on 2026-08-20:

- Deleting a referenced source removes the affected reference edge; it does
  not cascade-delete the consuming object or its dependent object chain.
- Objects have their own persistent state independently of references. In
  particular, a Container placement retains its resolved absolute X/Y/Z
  values, so the Container remains located and continues to exist after a
  placement reference disappears.
- A reference constrains or derives object state; it is not the sole storage
  of that state and is never the ownership/lifetime mechanism for the
  consumer.
- After reference removal, the consumer continues from its retained values and
  exposes the newly free parameters/DOF for later editing or new references.
- This fallback-state contract applies to every reference-driven property, not
  only Container placement and orientation. Every consumer persists enough of
  the last successfully resolved engineering/parametric input to remain
  existing, editable and calculable after its source reference is removed.
- A cached final OCCT body alone is not sufficient fallback state. The retained
  data must preserve the consumer's own meaningful current inputs (for example
  resolved coordinates, directions, profiles or other feature-specific data)
  so later editing and calculation do not require the deleted source.
- Every persisted thing has its own globally unique stable ID across all
  documents, not merely an ID unique inside one Part or Assembly. Names, type,
  position and geometric equality are presentation/properties, never identity.
- Deleting an object retires its ID. A subsequently created object receives a
  different ID even if it has the same name, parameters and geometry.
- Removed references never automatically reattach by matching a name or
  geometry. Reattachment is an explicit user operation that stores the new
  source ID.

### Persisted topology ancestry

- Generated topology receives new globally unique IDs but also persists its
  exact parent-source relation (the `Face Name`/topology lineage). It does not
  reuse the source object's ID.
- For an Extrusion or Revolution, a source Sketch point is the parent of the
  generated longitudinal/sweep edge and generated start/end vertices. A source
  Sketch segment or curve is the parent of its generated side face and
  start/end rim edges. Start/end cap faces are children of the selected profile
  region.
- Lineage continues transitively through later features, allowing a resulting
  Face, Edge or Vertex to be traced back through its persisted parents without
  relying on OCCT traversal order or display names.

### Objects, ownership and identity

- A Container, its mandatory Origin and every Origin child are different
  persisted objects with different stable identities.
- Parent/child topology is data, not a label convention and not a semantic
  string appended to one reused owner ID.
- Every Container has a mandatory system Origin and may own any number of
  additional objects. Its contents are not limited to one user entity.
- Container ownership is recursive. A Container may contain ordinary objects
  such as a Sketch, Extrusion or Revolution and may also contain further
  Containers such as Point Containers. No arbitrary depth or fixed list of
  permitted child counts is part of the document model.
- Owned children retain their own stable identities and explicit parent-child
  relations. Nesting must not be flattened into labels, type-specific arrays
  or duplicated top-level objects.
- View and Tree resolve the same persisted identity. Repeated Assembly
  occurrences additionally require the complete stable instance path.
- UI code consumes persisted ZIMA viewer/reference packets. It does not ask
  OCCT to rediscover topology.

### Tree

- A single click selects/synchronizes; it never opens Properties.
- A double click invokes the documented edit/inspection gesture for that object.
  For objects with editable in-view dimensions this means dimension
  inspection, not automatically opening Properties.
- Properties is always available as an explicit context action.
- During an active reference command, clicking a valid Tree reference confirms
  that exact reference through the same command contract as View picking.

### View and selection

- Hover, pre-confirmation RMB cycling and LMB confirmation consume one common
  ordered candidate list. Click does not run another picker.
- Candidate geometry, hit geometry and highlighted geometry are identical.
  Screen-constant Origin planes/axes must not be hit-tested at a different
  scale or position from their drawing.
- Hover is orange; confirmed selection is cyan. Highlight recolours the exact
  candidate and must not obscure it with a second unrelated overlay.
- An active command explicitly defines displayed, offered, accepted and
  persisted candidates.
- Candidate packets are prepared/cached when the scene or camera changes.
  Mouse movement must not copy the complete reference mesh or rebuild an OCCT
  result.
- Ordinary selection is container/occurrence oriented. Stable topology is
  exposed only by commands whose selection contracts request it.

### Properties and editing

- Creation and later editing use one internal `Qt::SubWindow` class and one
  transaction contract.
- Only OK and Cancel are exposed. OK validates/calculates/commits/closes;
  Cancel restores the unchanged persisted state and all normal View/Tree
  selection settings.
- Pending data and previews are transient.
- Middle-button double click invokes enabled OK; short MMB and MMB drag retain
  their documented meanings.
- Editing a Container forces its complete local Origin visible independently
  of global Origin/Axis/Plane visibility actions.
- A double click intended for dimension inspection shows in-view editable
  dimensions without opening Properties.

### Calculation and persistence

- The native document is the complete persisted source of model information.
  Save/load retains hierarchy, per-owner history order, globally unique IDs,
  dependencies, ancestry, independent fallback values, parameters, visibility
  and document settings. Required data must not live only in external caches.
- Hover, Tree refresh, Properties opening, overlay creation and ordinary tab
  activation never invoke OCCT.
- OK or explicit Regenerate are calculation boundaries.
- Current-format save/load must preserve every identity, reference, fallback,
  offset, orientation role and visibility property needed for later editing.
- Legacy file compatibility is not required. Do not add fallback schemas to
  preserve a discarded model.

## Origin contract

### Persisted hierarchy

Every Origin contains these locked children in this exact order:

```text
Origin                       owner: <parent-id>:origin
├── Point 0,0,0              id: <origin-id>:point
├── X Axis                   id: <origin-id>:axis:x
├── Y Axis                   id: <origin-id>:axis:y
├── Z Axis                   id: <origin-id>:axis:z
├── XY Plane                 id: <origin-id>:plane:xy
├── YZ Plane                 id: <origin-id>:plane:yz
└── XZ Plane                 id: <origin-id>:plane:xz
```

The Container ID, Origin ID and child IDs are not interchangeable. Viewer
references may use the Origin as presentation owner, but Tree/model ancestry
must retain every child identity.

### View presentation

- Origin point is a black filled circle, radius 4.5 screen pixels.
- Hover/selection recolours this marker orange/cyan; it does not stack another
  marker over it.
- X/Y/Z axes are red/green/blue and use screen-constant arrowheads and labels.
- Origin planes are brown and screen constant.
- Document Origin visibility follows the shared global visibility and
  empty-document/definition rules.
- A normal Point container shows only its black user-point marker when Points
  are enabled. It does not always show its axes and planes.
- The complete Container Origin is shown while its owner is being edited,
  irrespective of global reference visibility.

### Origin size

The approved display ratios are:

- every Container Origin axis length is exactly 50% of the document Origin
  axis length;
- every Container Origin plane linear size is exactly 50% of the document
  Origin plane linear size;
- arrowhead and label sizes remain unchanged in screen pixels.

This applies to every container type, normal auxiliary display, editing and
creation preview.

## Point Container contract

### Data model

A Point is a Container, not a flat datum record:

```text
Point Container
├── Container Origin
│   ├── Point 0,0,0
│   ├── X/Y/Z Axis
│   └── XY/YZ/XZ Plane
└── Point entity
```

The container owns name, coordinate system, auxiliary-geometry visibility and
the transaction/edit lifecycle. The Point entity owns constraint descriptors,
fallback coordinates and rotation offsets needed by the solver. References
retain stable owner/element identity, instance path, type, offset and any
orientation metadata.

The model must represent this ownership with unambiguous identities. A flat
display record alone does not establish the persisted hierarchy.

### Creation and Properties

- Command title is `Bod`; “Vlastnosti” is not part of the command name.
- The internal window contains name, reference table,
  X/Y/Z fallback, RX/RY/RZ orientation, DOF/status and OK/Cancel.
- A new command immediately arms the next placement reference.
- Reference rows remain available until the maximum is reached or translation
  DOF is zero.
- Removing a reference restores the correct DOF and next empty row.
- Point/vertex/Origin, axis/straight edge and plane/face are valid placement
  inputs according to the constraint capability.
- Selecting an Origin expands to its ordered plane/reference set; it is not stored as an ambiguous single container click.
- Plane and face rows enable signed offset. Point, axis and edge rows do not.
- Mouse wheel over a combo/spin box must not accidentally alter its value
  unless that control has focus, following the shared dialog input policy.

### Solver and DOF

- A point/vertex anchor can constrain three translations.
- A straight axis/edge contributes the two independent equations normal to
  its direction.
- A plane/face contributes one normal equation.
- Plane/face offset is added along the signed reference normal.
- Redundant equations do not remove extra DOF.
- Conflicting equations are rejected with an explicit diagnostic and do not
  commit a broken Point.
- Under-constrained directions preserve X/Y/Z fallback values.
- Point plus planes/axes treats the point as an anchor for remaining free
  directions; it is not blindly added as three competing equations.
- DOF is matrix rank (`3 - rank`), not a decrement counter based only on the
  clicked candidate kind.
- Solver preview and OK use the same equations and persisted reference packet.

### View/Tree selection loop

- Opening Point Properties forces its complete local Origin visible.
- The command uses original persisted solids/references, never result-body
  topology or transient preview geometry.
- Hover offers the exact orange candidate. RMB changes the index in that same
  list. LMB confirms that exact item and passes it to the current reference
  row.
- Tree confirmation accepts the exact same entity types and persists the same
  descriptors as View confirmation.
- A reference confirmation clears temporary Tree/View selection before
  arming the next row.
- Own Origin/content and cyclic/self references are excluded.
- Screen-constant axes and planes are picked from their displayed geometry,
  not their unscaled model-space helper packet.

### Normal selection, double click and dimensions

- Normal Point display is a black marker controlled by Points visibility.
- A single Tree click does not open Properties.
- A View or Tree double click activates dimension inspection for the Point
  Container.
- Inspection exposes placement dimensions and every supported planar
  `reference_offset:<index>` dimension using the same persisted references as
  Properties.
- Editing a displayed dimension updates through the same solver/transaction
  path as Properties. It does not create a separate definition path.
- Explicit context-menu Properties opens the shared Point dialog.

### OK, Cancel, edit and persistence

- Creation and edit use the same dialog and solver.
- Preview never changes the stored document.
- OK commits exactly one new Point or replaces the existing Point in place.
- Cancel removes creation preview or restores the unchanged edited Point,
  clears forced Origin visibility, candidate lists, highlights and dimensions,
  and preserves the camera.
- Save/load retains the Container, Origin hierarchy, Point child, fallback,
  rotation offsets, ordered typed references and per-reference offsets.
- Explicit Regenerate resolves references from persisted viewer geometry;
  opening Properties or hovering does not calculate OCCT geometry.

## Verification procedure

For a changed feature, trace its native model and shared command entry points.
Record normal, active-command, preview, edit and post-Cancel states, exact
candidate types, persisted data and calculation boundaries. Reuse the shared
implementation and remove obsolete parallel paths.

Choose regression coverage appropriate to the change. For model and interaction
changes, verify ownership and identity, solver conflict/fallback behavior, exact
View/Tree candidates, transient preview, one committed revision, Cancel,
Undo/Redo, and native save/load. Check affected Part, Assembly and nested
occurrence contexts. Changes to drawing, picking or manipulators also require
renderer or real GUI interaction evidence. Record what actually passed and any
remaining limits; do not reuse an old migration matrix as a current test result.
