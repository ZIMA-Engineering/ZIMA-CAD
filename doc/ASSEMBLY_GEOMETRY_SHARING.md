# Assembly and Drawing geometry sharing

The version numbers in the implementation history below identify historical
sharing changes; the serializers and start templates define current versions.

## Source storage audit (2026-09-17)

The audit below records the previous implementation. The subsequent implementation
uses Assembly INI version 25 / payload version 37 and the updated start template.
Ordinary components persist source paths and IDs, occurrence placement, references
and properties. Their `operation_geometry` is null and their nested snapshot is
empty. `operation_geometries` contains only results owned by Assembly cuts or
derived-copy operations. Cut inputs remain persisted for rollback/editing without
recalculation. No source revision/cache sidecars are introduced.

`AssemblyDocument::load` hydrates ordinary components from native dependencies,
recursively and with sharing between repeated sources. Hydration uses saved Part
calculation packets and builds viewer scenes; it never invokes OCCT, solves mates
or regenerates operations. It checks native source identity and dependency cycles.
Missing files retain an unresolved occurrence in the tree instead of loading an
embedded fallback or preventing the entire Assembly from opening. GUI/CLI Open
captures an immutable source resolver so open
unsaved documents take precedence over disk. Metadata-only rename staging reads
definitions without hydrating dependency paths that have not moved yet.

The source snapshot remains an in-memory display/calculation handle. It is not
serialized for ordinary occurrences. Assembly-owned results retain their last
explicit calculation while source-only occurrences display current native data.
The required native dependency files must accompany a shared/moved project.
Legacy Assembly payloads are intentionally unsupported.

The component context menu exposes **Source file…** (`Zdrojový soubor…`). Activate
the immediate owning Assembly first, then select the relocated native document.
The command validates its document identity, updates repeated references to that
source in the owner, and preserves occurrence IDs, placement, mates and calculated
Assembly-owned results. It is one undoable owner transaction, opens no additional
tabs and performs no modeling calculation. A different source identity is rejected;
replacing a component is a separate command. Save the owner to persist repaired
paths. A missing subassembly exposes its own unresolved row; its children become
available after its source has been located.

### Verification of reference-only storage

The regression coverage checks native STEP/IGES file creation and independent
reload, repeated-source sharing, nested relative paths, open unsaved source
authority, Assembly-owned cut persistence, missing-source recovery, identity
rejection, Undo/Redo, family-member hydration and dependency-cycle rejection.
The GUI component-properties contract also checks the unresolved tree row and
the enabled localized **Source file…** context action. Import source documents
remain hidden from the tab bar until explicitly opened.

The broad Windows CTest audit exercised all 190 registered tests. Stale fixtures
that expected embedded component geometry were changed to save real native
dependencies. The audit is not an all-green certification: remaining failures
include a bounded construction Axis, a shared title-block dimension, a
Universal Dimension two-segment interaction and parallel application-instance
numbering (also reproduced in isolation, then resolved by the instance-isolation
change described in `MULTIPLE_INSTANCES.md`). After the STL fixture was repaired,
the comprehensive console GUI contract progressed to a Family Table fixture
failure (`LENGTH`: invalid family column reference). The other four failures remain
open; their causes have not all been established. The dedicated Sketch dimension-entry,
Bend attachment and edge-treatment GUI contracts passed. Detailed build/test logs
are retained locally under `build/assembly-storage-*` and
`build/assembly-reference-*`; they are generated artifacts, not native document
dependencies.

After the final source/family changes, all 33 selected console regression tests
passed (`build/assembly-storage-completion-tests.log`). The Windows Release build
and the additional cached-family hydration assertion also built successfully.
The final component-properties and Assembly-refresh GUI reruns passed; the
console GUI rerun failed at the Family Table case described above
(`build/assembly-storage-completion-gui-tests.log`).

Scope: distinguish authoritative native sources, derived display data and
Assembly-owned operation boundaries before changing persistence.

Verified baseline before the reference-only change:

- `AssemblyDocument::serialized` stores component source paths and identities,
  placements and occurrence properties, plus a `source_geometries` table.
  Table entries are deduplicated by shared `BodyResult` address within this
  document, not across native files. Their packets include `kernel_shape`,
  viewer geometry and original references. Part history is excluded, but
  nested `body_outputs` are recursively included as table references.
- `calculated_source` currently serves two roles: current source geometry for
  ordinary occurrences and the calculated result of Assembly-owned operations.
  Removing the table indiscriminately would lose those operation results.
- Cuts persist their definitions, target occurrence IDs and
  `input_component_bodies`. These inputs capture every component at the operation
  boundary and support Properties rollback without OCCT. They cannot simply be
  replaced with the latest source data: that would change the historical input
  shown while editing an already calculated operation.
- `Workspace::refresh_source_geometry` consumes open source documents first,
  otherwise native files. It deliberately skips cut targets and derived copies,
  retaining calculated operation results until explicit regeneration. Missing
  ordinary source files currently leave embedded geometry available; changing to
  references-only storage therefore also requires explicit missing-source handling.
- `CachedBodies.data` in Assembly INI is empty; actual packets are in the packed
  Assembly document payload. Looking at that INI section alone does not establish
  that an Assembly contains references only.

Redesign recommended by the audit and subsequently implemented above: represent source handles
and Assembly operation results separately; hydrate ordinary source geometry from
native dependencies without OCCT; retain Assembly-owned results and the input
boundaries needed for rollback. Audit nested snapshots and derived copies under
the same ownership rule. Resolve dependencies for all document consumers, not
only GUI refresh. Keep open unsaved source documents authoritative, stable
occurrence paths, explicit regeneration and native-only persistence. Update
format versions and start templates together when implementing this change.

### Import files and discoverability

`import_assembly` saves native Parts and subassemblies before publishing the
insertion. Following the user's correction during this audit, the default now
writes directly in the working directory regardless of the owner's save path.
STEP preserves unique definitions and repeated occurrences; IGES creates a native
Part. Filenames use the import stem and `part-N` / `assembly-N`, with collision
suffixes. Explicit CLI output directories retain their existing contract.
Suppressing automatic tabs does not suppress these writes. Existing imports in
subdirectories are not moved, so their persisted references remain valid.

The Open dialog accepts native Part and Assembly extensions and does not list
subdirectory contents recursively. Its current import completion message reports
counts but not the destination directory, which makes these files less obvious.
The import regression checks file existence and independently reloads every STEP
source and the IGES source, verifying their document identities.

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


The supplied IGES was also examined through a real OCCT import. Its groups
contain both the final B-Rep and support surfaces, so blindly transferring all
group members adds unwanted display geometry. See the measured
[IGES comparison](IGES_DXF_IMPORT.md#comparison-of-supplied-step-and-iges-2026-09-11).
The audit also exposed quadratic semantic-key lookup in archive binding;
`persist_imported_topology` now builds a lookup index once per topology kind.
This leaves the persisted identities and archive contract unchanged.


## Supplied STEP sharing measurement

The diagnostic imported `Projects/import/ze0026-0000-0000.stp` at 0.1 mm
mesh deflection: 85 unique Part documents and 11 Assembly documents in
256.92 s. Traversing component source packets and nested body outputs across
all generated Assembly documents counted 2,436 uses and 180 distinct immutable
snapshots. The selected geometry buffers (B-Rep strings, display vertices and
triangle indices, original-reference vertices and triangle indices) totalled
305,948,204 B when counted per use, versus 82,451,254 B counted once per snapshot
(73.05% less). This does not include every mesh field, Part history, allocator
cost or the entire application; it is not a before/after process RAM benchmark.

The import-package process reported 2,326.4 MiB working set and 2,445.72 MiB
private memory, with 3,113.49 MiB peak working set. Other local diagnostics/tests
were running during this measurement. No GUI interaction or saving benchmark
was performed in this run.

After the archive lookup index and feature-origin visibility changes, all 44
Windows CTest contracts passed again (425.67 s). The origin regression verifies
that hiding/requesting one occurrence does not expose another occurrence of
the same Part. Additional dialog-level coverage exercises Protrusion and Mirror.
