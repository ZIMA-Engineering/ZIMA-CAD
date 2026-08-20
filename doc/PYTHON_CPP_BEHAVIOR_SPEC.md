# Python → C++ behavioural specification

## Purpose

This is the binding working specification for the native C++ migration.
The Python application is the behavioural reference. A C++ feature is not
complete merely because it compiles, produces geometry, has a similarly named
dialog, or passes an isolated unit test. It is complete only when the same
user-visible vertical scenario has been compared with Python and covered by a
native behavioural test.

The comparison unit is always the complete flow:

```text
persisted model and ownership
→ Tree presentation
→ normal View presentation
→ active-command selection contract
→ hover / RMB cycle / LMB confirmation / Tree confirmation
→ constraint solving and remaining DOF
→ transient preview and dimensions
→ OK / Cancel / edit lifecycle
→ save / load / regenerate
```

Local C++ differences are allowed only when explicitly recorded in the
section **Approved differences**. An accidental difference is a defect, not a
new design decision.

## Source hierarchy

When sources disagree, use this order:

1. current explicit user requirements and repository `AGENTS.md`;
2. current Python behaviour and its persisted model;
3. focused architecture documents such as `VIEWER_SELECTION.md`,
   `HISTORY_EDITING.md`, `STABLE_TOPOLOGY_NAMING.md` and
   `ASSEMBLY_REFERENCES.md`;
4. migration/audit documents;
5. existing C++ behaviour.

`CXX_MIGRATION.md` and `CXX_PARITY_AUDIT.md` describe migration intent and
historical progress. Their word “Native” is not proof of current behavioural
parity. The actual Python flow and an end-to-end comparison are authoritative.

## Documentation routing

Before changing an area, read the corresponding documents completely and
trace the listed Python entry points.

| Area | Required documentation | Primary Python implementation |
|---|---|---|
| Engineering/CAD decisions | `AI/ZIMA_ENGINEERING_REASONING.md` | feature-specific model and solver code |
| Shared selection | `VIEWER_SELECTION.md`, `STABLE_TOPOLOGY_NAMING.md` | `selection.py`, `viewer.py`, `viewer_scene.py`, selection handlers in `app.py` |
| Containers and rollback | `HISTORY_EDITING.md`, `OCCT_VIEWER_CLEANUP.md` | `model.py`, definition/edit lifecycle in `app.py` |
| Part features | `CXX_MIGRATION.md`, `NEXT.md`, focused feature documents | `model.py`, `app.py`, `viewer_scene.py`, `viewer_mesh.py` |
| Assembly | `ASSEMBLY_REFERENCES.md`, `VIEWER_SELECTION.md` | assembly paths, ownership and activation in `app.py`/`model.py` |
| Sketcher | `SKETCHER.md`, `SKETCH_MODEL.md`, `SKETCHER-TERMINOLOGY.md` | `sketch_model.py`, `sketch_geometry.py`, Sketcher portions of `app.py`/`viewer.py` |
| Drawings | `DRAWINGS.md`, `CAD-DIMENSION-SYMBOLS.md`, `DIMENSION_RANGES.md` | `drawing*.py`, drawing portions of `app.py` |
| File/runtime | `WINDOWS_RUNTIME_AND_BUILD.md`, `CXX_MIGRATION.md` | `storage.py`, `versioned_io.py`, `file_dialogs.py`, `settings.py` |

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
- A double click invokes the Python edit/inspection gesture for that object.
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

- The Python application's current persisted document schema and semantics are
  the source format for the 1:1 C++ port. C++ must read and write the same
  document information; it must not introduce a simpler parallel C++ document
  format.
- A Python-saved current document and a C++-saved current document must retain
  the same object hierarchy, per-owner history order, globally unique IDs,
  parent-child relations, dependencies, topology ancestry, fallback state,
  parameters, visibility and application/document settings.
- Serialization may use a different implementation language internally, but
  round-tripping must not discard or reinterpret Python model information.
- Hover, Tree refresh, Properties opening, overlay creation and ordinary tab
  activation never invoke OCCT.
- OK or explicit Regenerate are calculation boundaries.
- Current-format save/load must preserve every identity, reference, fallback,
  offset, orientation role and visibility property needed for later editing.
- Legacy file compatibility is not required. Do not add fallback schemas to
  preserve a discarded model.

## Origin contract

### Persisted hierarchy

Python source: `model.py:create_origin_object`, `add_origin_children`,
`add_coordinate_system_children`.

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

Python source: `viewer_mesh.py:origin_axes_mesh`, `point_marker_mesh`,
`datum_plane_mesh`; `viewer_scene.py:_append_object_origins` and document
Origin assembly.

- Origin point is a black filled circle, radius 4.5 screen pixels.
- Hover/selection recolours this marker orange/cyan; it does not stack another
  marker over it.
- X/Y/Z axes are red/green/blue and use screen-constant arrowheads and labels.
- Origin planes are brown and screen constant.
- Document Origin visibility follows global visibility and the Python
  empty-document/definition rules.
- A normal Point container shows only its black user-point marker when Points
  are enabled. It does not always show its axes and planes.
- The complete Container Origin is shown while its owner is being edited,
  irrespective of global reference visibility.

### Approved size difference

This project intentionally changes the Python ratios:

- every Container Origin axis length is exactly 50% of the document Origin
  axis length;
- every Container Origin plane linear size is exactly 50% of the document
  Origin plane linear size;
- arrowhead and label sizes remain unchanged in screen pixels.

This applies to every container type, normal auxiliary display, editing and
creation preview.

## Point Container contract

### Data model

Python source: `model.py`, `app.py:_create_point_object`,
`_create_constrained_point`, `_update_point_object`.

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

The current flat C++ `ConstructionObject` is not parity merely because it can
draw a point. Until it represents the ownership above without ambiguous IDs,
its status is **incomplete**.

### Creation and Properties

Python source: `app.py:PointConstraintDialog`, `_create_point_object`.

- Command title is `Bod`; “Vlastnosti” is not part of the command name.
- The internal window contains name, fixed Container type, reference table,
  X/Y/Z fallback, RX/RY/RZ orientation, DOF/status and OK/Cancel.
- A new command immediately arms the next placement reference.
- Reference rows remain available until the maximum is reached or translation
  DOF is zero.
- Removing a reference restores the correct DOF and next empty row.
- Point/vertex/Origin, axis/straight edge and plane/face are valid placement
  inputs according to the constraint capability.
- Selecting an Origin expands to the same ordered plane/reference set as
  Python; it is not stored as an ambiguous single container click.
- Plane and face rows enable signed offset. Point, axis and edge rows do not.
- Mouse wheel over a combo/spin box must not accidentally alter its value
  unless that control has focus, following the shared dialog input policy.

### Solver and DOF

Python source: `app.py:_solve_point_constraints` and reference-expansion
helpers.

- A point/vertex anchor can constrain three translations.
- A straight axis/edge contributes the two independent equations normal to
  its direction.
- A plane/face contributes one normal equation.
- Plane/face offset is added along the signed reference normal.
- Redundant equations do not remove extra DOF.
- Conflicting equations are rejected with the Python diagnostic and do not
  commit a broken Point.
- Under-constrained directions preserve X/Y/Z fallback values.
- Point plus planes/axes treats the point as an anchor for remaining free
  directions; it is not blindly added as three competing equations.
- DOF is matrix rank (`3 - rank`), not a decrement counter based only on the
  clicked candidate kind.
- Solver preview and OK use the same equations and persisted reference packet.

### View/Tree selection loop

Python source: viewer selection policy and candidate-cycle handlers in
`app.py`, picking in `viewer.py`.

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
- Point is the ordinary-selection exception to the visible "point inside a
  Point Container" hierarchy: its marker offers and confirms the owning Point
  Container. An active reference command still receives the exact persisted
  Point child through the same viewer candidate source.
- A single Tree click does not open Properties.
- Ordinary LMB selection follows one global rule: clicking a valid offered
  candidate confirms exactly that candidate and synchronizes Tree; clicking
  empty View space clears the confirmed View and Tree selection together and
  removes selection and inspection overlays. An active command may define a
  different explicit command-local meaning for an empty click, but must not
  retain an unrelated stale confirmed selection.
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

## Point parity acceptance matrix

No row may be marked complete without a matching automated test and, for View
rows, a real framebuffer capture or deterministic renderer-image test.

| Scenario | Python source checked | C++ implemented | Behaviour test | Image test | Status |
|---|---:|---:|---:|---:|---|
| Persisted Container → Origin → children hierarchy | yes | yes | model + round-trip | n/a | complete |
| Normal black Point marker | yes | partial | mesh only | document Origin capture only | incomplete |
| Edited Point forces full local Origin | yes | yes | mesh contract | no | incomplete |
| Container Origin 50% size, unchanged arrows | approved difference | partial | partial | no | incomplete |
| Single Tree click does not open Properties | yes | yes | no | n/a | incomplete |
| Context Properties uses shared Point dialog | yes | partial | partial | n/a | incomplete |
| View double click opens placement dimensions | yes | partial | no | no | incomplete |
| Tree double click follows inspection/edit contract | yes | yes | startup contract | no | incomplete |
| Stable shared hover/LMB/RMB candidates | yes | partial | viewer + Tree startup | no | incomplete |
| Hit geometry equals screen-constant display | yes | yes | no | no | incomplete |
| Tree and View create identical typed references | yes | no | no | n/a | missing |
| Origin reference expansion | yes | yes | no | n/a | incomplete |
| Plane/face offset enabled and solved | yes | partial | unit/UI | no | incomplete |
| Rank-based DOF and redundancy/conflict handling | yes | partial | rank unit | n/a | incomplete |
| Under-constrained fallback preservation | yes | partial | partial | n/a | incomplete |
| Preview/OK use the same solution | yes | partial | no | no | incomplete |
| Cancel restores all viewer/dialog state | yes | partial | partial | no | incomplete |
| Edit existing Point in place | yes | partial | UI only | no | incomplete |
| Save/load exact hierarchy and typed references | yes | partial | hierarchy round-trip | n/a | incomplete |
| Part, Assembly and nested occurrence behaviour | yes | partial | partial | no | incomplete |

## Working procedure for every following feature

1. Trace all Python entry points and every callback they install.
2. Record model ownership and stable IDs before considering UI appearance.
3. Record normal, command-active, preview, edit and post-Cancel states.
4. Record the exact candidate types and data persisted from View and Tree.
5. Record calculation boundaries and prove no hidden OCCT call occurs.
6. Compare C++ and list architectural gaps before editing.
7. Implement one shared path, removing obsolete parallel paths.
8. Add model, UI, interaction, persistence and framebuffer tests.
9. Run the complete suite and manually compare the same scenario in Python and
   C++.
10. Update the acceptance matrix with evidence. Never mark a row complete from
    compilation or visual similarity alone.
