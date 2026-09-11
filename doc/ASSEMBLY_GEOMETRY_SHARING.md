# Assembly and Drawing geometry sharing

## Verified baseline, 2026-09-11

The current Windows Release passes all 44 CTest contracts. Completed changes:
- Deferred dimension drag until the pointer crosses the drag threshold;
  pending Assembly angle edits use the component-properties transaction.
- Cached surface batches and selected-reference outlines between camera moves.
  On step-pokus.asmz (506,314 triangles), the isolated 1400 x 900 diagnostic
  measured the shaded-with-edges paint function at about 287 ms before and
  87 ms after. These are paint timings, not interactive FPS. The user confirmed
  smoother navigation; geometry precision and history caches were preserved.
- STEP product relationships distinguish real Assemblies from compound Part
  representations. ZE0026-0101-9001 retains one solid (26 faces) and two auxiliary
  faces in one Part. The source has 85 unique Parts and 11 real Assemblies under
  this classification, previously 121 and 47. Existing imports are not rewritten.
- Ordinary occurrence picking includes visible triangles without a source-face
  identity. It never promotes them into topology/placement references. Hover,
  RMB cycling and LMB confirmation share one ordered list.
- Nested-source Open resolves relative paths through unopened Assemblies without
  activating or regenerating them. Select Parent advances one level through the
  displayed root, highlights the exact subtree and synchronizes the Tree.

## Approved next work

First audit original STEP face/edge/point reference completeness. The ordinary
hover fix does not repair missing topology references. Never invent persistent
identities from triangle indices or OCCT traversal order.

Share calculated geometry between repeated occurrences, including nested
child results. Part owns its current calculated source geometry. Updating that
geometry must become visible in Assemblies; do not pin historic Part versions.
Tab activation may consume already calculated viewer data but must never run
OCCT, solve mates or regenerate Assembly cuts. Those calculations remain explicit.

A mate persists occurrence path, source owner ID and semantic key, and resolves
original source geometry. Storage addresses must not replace topology identity.
If a source face disappears, report a missing reference instead of rebinding.

Drawing views retain calculated projections for fast display. Share common 3D
measuring data between views of the same source geometry. They do not store a
full source B-Rep or Part history.

## Native persistence only (clarified 2026-09-11)

All required persistent data belongs exclusively in `.prtz`, `.asmz` and `.drwz`.
Dependencies refer to these native documents. No external revision directory,
geometry sidecar or mandatory cache is permitted. The proposed external revision
store was rejected and removed. Shared runtime objects and native-file tables
may eliminate duplicate data without introducing another required file type.

Keep extensions. Update and track start Part and Assembly templates with each
format change. Part internal version 17 fixes original import bindings; Assembly
version 14 introduces shared source geometry records; Drawing version 13 stores
shared measuring geometry separately from each view projection. No legacy adapters.

## Verification gates

- Complete reference capture on representative STEP solids and sheets.
- Stable mates after supported Part edits and explicit regeneration.
- Shared geometry with distinct repeated/nested occurrence paths.
- Current calculated source geometry becomes visible without implicit OCCT, mate solving or Assembly operations.
- Save/reopen, project relocation and current native source references.
- Drawing projection and dimension references retain their meaning.
- Compare RAM, loading/saving and interaction on the same STEP source.
- Use repository Windows build scripts and relevant regression contracts.

## Original import binding repair (2026-09-11)

The screw ZE0026-0101-9001 has one solid with 26 faces and two auxiliary
sheet faces. Direct import exposed all 28 original face identities; the old
geometric-hash lookup restored only 8 from frozen B-Rep. Rounding the hash
inputs restored only 20 and was discarded.

Frozen imports now bind the existing source semantic identities to opaque
object references in the same BRepTools_ShapeSet archive. These are archive
addresses, not topology identities. Archive and binding table are written
together. Restoration validates object kind, membership, uniqueness and address
syntax and rejects invalid bindings instead of silently omitting references.
STEP and IGES use the same frozen-body binding contract. UI still consumes
persisted viewer reference geometry; only explicit calculation reads B-Rep.

Part internal version is 17 for this independent reference repair. Extensions
remain unchanged. Update and track the corresponding start Part/Assembly
templates with every format change; config is part of the repository.

## Implemented ownership and display behavior

`BodySnapshot` shares an immutable `BodyResult`. Assembly occurrences and nested
body outputs copy this handle instead of recursively copying geometry. Native
ASMZ source records form a shared table, including nested outputs. Part history
and body calculation inputs are not part of these Assembly source records.
An Assembly still retains shared calculated display/source packets for fast
reopening; this is not a full editable Part document or its feature history.

Ordinary subassembly snapshots keep their viewer scene and child handles, not
a duplicate compound B-Rep. `calculate_component_body` builds a temporary compound
only when an explicit body operation needs it (cuts, Mirror or Pattern).

The Workspace refresh uses current calculated data from open Parts first. For
closed native sources it reads saved data when the file timestamp changes.
It does not call the solid kernel, alter occurrence placement or create Undo
entries. Assembly-owned cut/derived results wait for explicit regeneration.
Unchanged Part data keeps the same shared pointer across View refreshes.

Drawing measuring curves/points have one immutable shared owner. Capturing equal
geometry reuses that owner; DRWZ stores the measuring packet once and views keep
separate projections and references to it. Rebinding a drawing copy creates a
new measuring packet and cannot mutate the source drawing.

Validation of the import binding repair on the supplied STEP preserved all
26,110 offered original face/edge/vertex identities across all 85 unique Parts.
The screw retained all 28 face identities (10 planar, 18 curved), including its
two auxiliary sheet faces. A synthetic curved-solid/sheet regression repeats
archive roundtrips with changed mesh precision and rejects invalid bindings.

Final Windows verification: `tools/build-windows.ps1 -Configuration Release
-RunTests` passed all 44 CTest contracts (376.31 s). Additional regressions cover
current open/saved Part geometry through a closed repeated subassembly, unchanged
placement/history, reuse on repeated refresh, native source-table sharing,
transient compound calculation, and DRWZ measuring-data sharing after reload.
