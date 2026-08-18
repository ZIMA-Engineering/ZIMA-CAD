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

The prototype Part is no longer a one-object shortcut. Its text model owns an
ordered history of containers with stable unique IDs and explicit Add/Subtract
semantics. The application evaluates the complete history in one explicit
kernel request, and only the final ZIMA viewer packet crosses back into UI.

Each history container also persists an independent placement: translation in
millimetres and rotations around local X, then Y, then Z in degrees. The OCCT
adapter transforms each operand before its Boolean operation. Placement is
therefore model data, not a viewer-only offset.

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

The Assembly prototype has its own `.zca.json` text format. It persists
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

The `zima-cad-workspace-cpp` migration target provides the first Assembly GUI:
document tabs, new/open/save Assembly, open Part, insert active Part, explicit
Regenerate, occurrence tree, leaf occurrence picking, and Assembly-owned
component Properties. It remains a separate executable until its Workspace
shell is merged with the already verified Part modeling commands.

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

It intentionally uses its own prototype suffix (`.zcp.json`). It must not
silently claim compatibility with current `.prtz` files before the C++ model
can preserve their complete current contract.

## Modules

- `document_core`: Qt- and OCCT-free persisted document model;
- `kernel_api`: ZIMA-owned requests and results, with no OCCT types;
- `kernel_occt`: the only module that knows live OCCT shapes;
- `viewer`: renders ZIMA viewer mesh data;
- `assembly`: immediate component ownership, instance paths, and scene composition;
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
