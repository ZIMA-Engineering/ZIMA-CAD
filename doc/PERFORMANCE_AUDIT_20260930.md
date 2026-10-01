# Application performance audit — 2026-09-30

Follow-up: [A1 independent equation blocks](performance/20260930-rectilinear-blocks.md)
records the first implemented optimization and its verification. The audit below
retains the original pre-change observations and priorities.

The [A2/A3 scene and picking follow-up](performance/20260930-assembly-scene-picking.md)
records the subsequent limited implementation, desktop measurements and checks.

The [A4 presentation-refresh follow-up](performance/20260930-presentation-refresh.md)
records the scoped removal of rebuilds on ordinary filter changes and the end
of Assembly dimension inspection. Other refresh paths remain audit candidates.

The [A12 parsed-definition follow-up](performance/20260930-symbol-definition-cache.md)
records bounded reuse of immutable Symbol definitions and exact geometry checks.

## Result and scope

The [Assembly visibility-refresh follow-up](performance/20261001-visibility-refresh.md)
reuses one freshly assembled scene within an ordinary refresh, retaining full
Tree/picker updates and recording paired measurements and identical frames.

The [Assembly appearance-preview follow-up](performance/20261001-appearance-preview.md)
removes repeated scene assembly during color previews, with generation-bound path
reuse, callback measurements and identical before/after framebuffer captures.

The [per-refresh source-lookup follow-up](performance/20261001-source-lookup.md)
records bounded identity reuse, source-order measurements and mixed small-case
results without claiming a uniform speedup.

The [open Assembly root follow-up](performance/20261001-open-assembly-roots.md)
removes duplicate traversal of open subassemblies within one refresh, with
three-level ownership/Undo checks and bounded synthetic measurements.

The [A5 source-refresh follow-up](performance/20260930-source-refresh.md) records
per-refresh reuse of completed repeated subassemblies and its measurements.

The [A6 object-bounds follow-up](performance/20260930-view-object-bounds.md)
records a limited CPU preparation optimization. The broader GPU/presentation
separation below remains an audit candidate.

The [A8 fingerprint follow-up](performance/20260930-history-fingerprint.md)
records removal of temporary profile-identity copies with exact fingerprint
equivalence. The [batch follow-up](performance/20260930-fingerprint-batch.md)
adds exact all-prefix encoding and moves the encoder into one compiled source.

The [A13 icon-cache follow-up](performance/20260930-icon-cache.md) records
lookup before SVG reads, exact pixel comparisons and its limited microbenchmark.

The [A10 current-state snapshot follow-up](performance/20260930-drawing-read-snapshots.md)
records removal of Undo/Redo copies during read-only Drawing source preparation.
The [history-sharing follow-up](performance/20260930-history-sharing.md) preserves
complete atomic staging while sharing immutable inactive history. The
[Drawing reuse follow-up](performance/20260930-drawing-reuse.md) narrows completed
Part invalidation and reuses per-export metadata.

The [A12 Symbol leader follow-up](performance/20260930-symbol-leader.md)
records removal of a discarded mesh calculation without adding cache state.

### Follow-up review after Windows 2026093005

All remaining hypotheses were reviewed. Implemented changes are deliberately
limited to demonstrated redundant work, with exact-output or state-isolation
checks. The original audit below remains a record of pre-change observations.

| Candidate | Implemented scope and retained boundary |
| --- | --- |
| A4/A6 View refresh/GPU | Removed a verified unused index buffer; twenty opaque/transparent/mode captures match exactly. General mesh/presentation separation remains a profiling candidate. See [GPU evidence](performance/20260930-view-unused-index-buffer.md). |
| A7 atomic Workspace staging | Share inactive session history, detach before mutation, retain complete Undo/Redo and exclusive current states. Deferred relocation is covered. See [history sharing](performance/20260930-history-sharing.md). |
| A8 fingerprints | Encode operations once for all requested prefixes, preserving every fingerprint; retain incremental single-prefix matching. See [batch evidence](performance/20260930-fingerprint-batch.md). |
| A9 native serialization | Embed/read typed Sketch JSON without redundant text round trips. Keep Assembly scene and all reference validation. See [native packets](performance/20260930-native-sketch-packets.md). |
| A10 source invalidation | Reuse completed Part projections after unrelated edits; retain conservative Assembly and observed native-file checks. See [Drawing reuse](performance/20260930-drawing-reuse.md). |
| A11 Drawing output | Reuse source metadata within one multi-sheet PDF export and eliminate a duplicate Assembly scene calculation. Exact output preparation remains. See [Drawing reuse](performance/20260930-drawing-reuse.md). |
| A12 Symbols | Bounded exact-source parsed-definition cache; variants/text/geometry still evaluate normally. See [Symbol cache](performance/20260930-symbol-definition-cache.md). |
| A14 measurement | Added repeatable distance/witness benchmark; retain existing acceleration and validation pending representative slow interactions. See [remaining review](performance/20260930-remaining-review.md). |
| A15 build dependencies | Compile the fingerprint encoder once rather than inline it in every consumer. No whole-build timing claim or removed release gate. See [compiled encoder](performance/20260930-fingerprint-batch.md). |

No precision, supported geometry, persistent identities or Undo/Redo capability
was sacrificed to close an audit row. The [remaining review](performance/20260930-remaining-review.md)
explains retained candidates and obsolete regression-fixture corrections.
Linux verification is explicitly outside this Windows pass.

The first investigation should target large Sketch dimension edits. The existing
Release benchmark exposes a severe scaling problem. The next priorities are
Assembly scene construction and picking, then avoiding complete scene/Tree
refreshes for small presentation changes. These are candidates for subsequent
work, not implemented optimizations or promised speedup percentages.

The audit followed Windows release [2026093004](releases/2026093004.md). Its
product source is `1dbcdc5d2c5e43d7d3e30d8c2a11c5714834494b`; the documentation
baseline is `706fbfd3`. No product code, persistent format or user configuration
was changed by this audit. No code was deleted: the orphan-file checks did not
establish a safe deletion candidate.

The tracked-source inventory covers 962 source/build/script files, approximately
251,893 lines; 679 files / 178,723 lines are under `cpp/app` and `cpp/modules`
(including integrated GUI verification). The review combined an all-file
inventory and reference/build scans with detailed tracing of the major paths
below. It is not a claim that every line has been proved optimal or that every
runtime path was profiled. Generated builds, external libraries, user projects,
backups and image files were excluded from the cleanup analysis.

**Inputs → means → outputs:** current native documents, persisted viewer data and
user gestures → existing caches, indexes, transactions and Qt/OCCT code → the same
calculated geometry, selection, presentation and saved documents with less
unnecessary work. Independent checks use the existing benchmark and correctness
assertions. Reducing precision, hiding context, removing selectable geometry,
skipping validation or dropping Undo records is not an acceptable optimization.

## Current measurements

Windows x64, Intel Core Ultra 9 285HX (24 logical processors), MSVC 14.51.36231,
CMake Release `/O2 /Ob2 /DNDEBUG`, existing native Qt/OCCT dependencies. The
benchmark targets were rebuilt from current source. No optimization variant was
built, so these measurements are baselines, not before/after comparisons.

Run from the repository root:

```powershell
cmake --build build/cpp-windows-release --target zima_cpp_performance_benchmark --parallel 2
./build/cpp-windows-release/zima_cpp_performance_benchmark.exe
```

The existing benchmark reports `mean_ms`, but some large cases explicitly use
one repetition. The table records the actual count from its source rather than
assuming the header's default applies everywhere.

| Operation / fixture | Time (ms) | Repetitions | Interpretation |
| --- | ---: | ---: | --- |
| Part history, 24 features | 297.490 | 3 | Full boundary materialization, volume 91,520 mm³ |
| Change final Part feature, live cache | 21.278 | 3 | Reuses 23 boundaries |
| Same change, cold kernel | 422.376 | 3 | Reconstruction from persisted data |
| Fillet edit, live cache | 130.375 | 3 | Reuses 24 boundaries |
| Same Fillet edit, cold kernel | 1,833.179 | 3 | Existing cache already has substantial value |
| Assembly scene, 256 occurrences / 9,216 triangles | 89.551 | 3 | CPU construction, excludes GPU upload |
| Ordered Assembly picking, 34 candidates | 12.077 | 3 | Convenience overload includes reference-packet preparation; not a direct GUI hover latency |
| Three-level nested regeneration | 3.756 | 3 | Small synthetic dependency fixture |
| Sketch solve, 100 independent branches | 35.841 | 3 | Geometry, status, DOF and residual assertions |
| Sketch solve, connected chain of 250 segments | 78.035 | 3 | Different graph shape; do not extrapolate from branch count alone |
| Sketch dimension edit, 100 branches | 34.057 | 3 | Fixture starts geometrically unsolved |
| Sketch dimension edit, 1,000 branches | 62,616.498 | **1** | Severe stress case; starts geometrically unsolved |
| Sketch point drag, 1,000 branches | 16.105 | **1** | Different interaction path |
| Copy that 1,000-branch Sketch | 0.539 | 3 | Copy cost alone cannot explain the dimension-edit result |
| Validate that Sketch | 1.145 | 3 | Validation cost alone cannot explain it either |

The run completed successfully. Counts, volumes and solver assertions are
checked by the benchmark, but it is not a replacement for the full regression
suite. [Raw output](performance/20260930-windows-baseline.txt) is retained with
this report (also `build/audit-20260930-baseline.log` locally).
Historical Linux timings in [CXX_PERFORMANCE.md](CXX_PERFORMANCE.md) are not a
same-machine comparison with this Windows run.

### Solved-input cross-check

A temporary measurement driver under ignored `build/` reuses `solver_fixture`
from `cpp/tests/performance_benchmark.cpp`. It places each moving endpoint at
its exact 5 mm horizontal solution, runs `solve()` and requires zero DOF, then
edits only the last dimension to 6 mm on three independent copies. Every
endpoint is checked: the edited branch is 6 mm, all others remain 5 mm, and all
remain horizontal within 1e-7 mm. No application source is instrumented.

| Already solved fixture | Initial full solve (one run) | Dimension edit (mean of three) |
| --- | ---: | ---: |
| 100 independent branches | 4.570 ms | 27.960 ms |
| 1,000 independent branches | 170.261 ms | **44,902.1 ms** |

All branch checks passed. This confirms that the slow edit is not restricted
to initially unsolved geometry. It remains a synthetic stress fixture, not a
measurement of every user Sketch. The initial solve and the subsequent edit
have different paths and must not be treated as equivalent work or a claimed
speedup. [Cross-check output](performance/20260930-windows-solved-sketch.txt) is
retained; the temporary driver and build log remain under ignored `build/`.

## Prioritized opportunities

Evidence labels: **M** = measured operation, **S** = directly observed source
behavior whose contribution still needs timing. Priority reflects likely user
impact and investigation value, not a guaranteed speedup.

| ID | Priority | Area | Evidence | Proposed direction | Risk |
| --- | --- | --- | --- | --- | --- |
| A1 | First | Large Sketch dimension edits | M + S | Index equation relationships and isolate repeated global work | High: solution branches and constraints |
| A2 | First | Assembly scene materialization | M + S | Reuse immutable source packets / transformed occurrence packets | High: exact occurrence ownership |
| A3 | First | Common viewer picking | M + S | Broad-phase spatial index and cached identity sets | High: ordering, tolerance, RMB cycling |
| A4 | Next | Whole-scene / Tree refresh | S | Separate geometry, tree structure and presentation invalidation | Medium–high: synchronized UI state |
| A5 | Next | Source refresh across repeated subassemblies | S | Memoize each source once per refresh, then apply per occurrence | High: authoritative unsaved sources |
| A6 | Next | Mesh replacement and GPU upload | S | Retain unchanged buffers; update transforms/overlays separately | High: rendering and picking coherence |
| A7 | Next | Workspace copies for atomic operations | S | Stage only required documents/current states or share immutable state | High: rollback and Undo/Redo |
| A8 | Next | History fingerprints | S | Reuse exact per-operation encoding and repeated prefix results | High: all geometry-affecting inputs and signed zero |
| A9 | Next | Native save/load and validation | S | Avoid duplicate representations and discarded scene construction | High: persistence and validation |
| A10 | Next | Drawing source/cache scope | S | Per-dependency invalidation and read-only snapshots without Undo histories | High: stale geometry and references |
| A11 | Later | Drawing refresh and output reuse | S | Preserve unchanged sheet caches; share exact export preparation | Medium–high: sections, crop, export |
| A12 | Later | Embedded symbol/Sketch decoding | S | Cache immutable parsed definitions and local geometry | Medium: variants, text, placement |
| A13 | Small | Icon cache lookup | S | Look up cached icon before reading/replacing SVG bytes | Low: palette/DPI states still required |
| A14 | Conditional | Repeated measurements | S | Retain acceleration data while selected geometry is unchanged | Medium: exact witness points |
| A15 | Developer | Build dependency tracking and verification units | Observed build behavior + S | Fix stage dependency tracking; separate heavy verification compilation | Medium: reliable builds/test entry points |

### A1 — Sketch dimension edits and solver scaling

Evidence: `cpp/tests/performance_benchmark.cpp:584`,
`cpp/modules/sketcher/src/sketch.cpp:2810`, `:8239`, `:8570`, `:8609`, `:9295`,
`:10906`, `:11521`.

`set_dimension_value()` enters `apply_dimension()` and the general transactional
solver. Editing an existing driver **already skips rank recomputation** through
`solve_impl(100, needs_rank_for_redundancy)`. Therefore the slow dimension case
must not be presented as proof that rank computation caused the delay.
Constraint/dimension iteration and relationship queries deserve phase timings
and sampling first. For example, `project_common_tangent_segments()` at `:8769`
scans constraints for each segment on every solver iteration, including a
fixture with no tangencies. This is a concrete repeated-work candidate, not
proof it accounts for the entire 44.9 seconds. Point lookups already have an
index inside the solver.

For full solves, a serialized Sketch is used as the rank-cache key. Structural
colouring, component partitioning and sparse elimination already exist, but a
global dense Jacobian is allocated before extracting smaller blocks. Direct
block construction is a separate candidate for large DOF analysis; it does not
explain every editing path.

Candidate: build reusable equation/geometry adjacency, avoid rediscovering
relationships inside repeated projections, and solve affected components only
when dependency closure proves independence. A geometrically unsolved input must
still resolve all required components. Keep residual and conflict validation.
Do not reduce iteration limits, change tolerances or postpone visible DOF state
to obtain a nominal speedup.

Gate: compare cold and solved inputs at 100/300/1,000 branches and connected
chains; verify every point, residual, DOF, reference dimension, tangent branch,
signed distance/side, external reference, failure rollback and Undo/Redo. Existing
Sketch tests should be supplemented with the demonstrated scaling fixture before
changing the algorithm. Record p50/p95 interaction latency separately from full
Regenerate time.

### A2 — Flattened Assembly scenes

Evidence: `cpp/modules/assembly/src/assembly_document.cpp:1585` and its loops over
vertices, triangles, edges and original references; measured 89.551 ms for 256
occurrences. Source `BodySnapshot` sharing already exists. `build_scene()` still
transforms/appends display and reference geometry for each occurrence.

Candidate: first cache transformed packets by immutable source identity,
placement and visibility. A later, larger option is GPU instancing of shared
source meshes, retaining an exact occurrence-aware reference/picking layer.
Cache bounds and invariant lookup maps with the same generation. Do not replace
persisted source references with result-body topology or collapse repeated
occurrences into one selectable object.

Gate: repeated Parts and nested Assemblies at 256/1,000+ occurrences, source edit
without saving, repeated source in different placements, active nested context,
hide/suppress, selection/Select Parent, cuts, Pattern, save/reopen and memory use.
Measure scene construction, upload, frame time and peak memory separately.

### A3 — One faster common picker

Evidence: `cpp/modules/viewer/src/picking.cpp:49` scans triangles,
`:211` onwards scan edge samples, and `:416` rebuilds persisted identity/path
sets for each candidate query. Some uniqueness checks scan vectors.
`cpp/modules/viewer/src/mesh_view.cpp:1036` already passes a retained reference
mesh and restricts Sketch-only queries. The standalone benchmark's convenience
overload performs extra reference copying, so its 12.077 ms is not a measured
12.077 ms cost on every GUI mouse move.

Candidate: a scene-generation spatial index for triangles, segments and points;
cache invariant identity sets. Use broad phase only to reduce candidates for the
existing exact narrow phase and stable ordering. All hover, LMB and RMB must
continue consuming the same list. Screen-constant Origins/dimensions and infinite
axes need explicit handling; a simple world-space BVH alone is insufficient.

Gate: compare complete ordered candidate lists to the current picker for a
recorded ray/camera/DPI set, including overlaps, hidden Origins, reference entry,
Fillet input boundaries and repeated occurrences. Measure both direct picker
calls and real mouse-move handling before choosing the data structure.

### A4 — Scope refresh work to what changed

Evidence: `cpp/app/workspace/scene.cpp:17` parses Relations and enumerates
dimensions for all open Part/Assembly documents, refreshes source geometry and
clears the complete Tree at `:57`. Tree population allocates items again.
Many command completion and selection/context paths call this common function;
this is not a claim that all mouse movement invokes it.

Candidate: separate geometry changes, Tree structure changes, selection,
dimension presentation and action availability. Cache parsed relation targets
by exact relation source; rebuild bindings when dimension identifiers change.
Retain Tree items by stable identity and update changed labels/state. Coalesce
multiple synchronous refresh requests within one operation where no intermediate
state is required.

Gate: count rebuilds for selection, activating a nested Part, editing one value,
opening/cancelling Properties and adding Sketch geometry. Verify expansion,
scroll position, active row, error colours, empty-click deselection, command
enablement, relation markers and all five locales. Preserve full passive context.

### A5 — Refresh a shared source once

Evidence: `cpp/modules/workspace/src/workspace.cpp:335` copies each open Assembly
document, recursively reads/checks sources, copies nested documents and compares
occurrence snapshots. Native caches and shared output packets already exist,
but per-call `assembly_sources` reuse is reached after recursive processing of
the nested source. Repeated source instances can repeat that preparation.

Candidate: memoize the fully refreshed source state once per refresh invocation
before descending repeated occurrences; retain cycle detection separately from
the completed-source cache. Then update each occurrence's own presentation.
Batch filesystem observations where safe. Do not freeze old Part revisions or
recalculate mates/cuts while switching tabs.

Gate: repeated subassemblies, missing/reappearing sources, Family variants,
unsaved open Parts, cuts owned by the Assembly, appearance changes, rename and
dependency cycles. Compare source generations, occurrence paths and dirty flags;
verify zero OCCT calls for display-only refresh.

### A6 — Mesh replacement versus small view changes

Evidence: `cpp/modules/viewer/src/mesh_view.cpp:748` rebuilds bounds, reference
and Sketch interaction packets and marks GPU data dirty. `:2832` reconstructs
triangle/line buffers and silhouette adjacency on upload. Ordinary wires are
already batched with `GL_LINES`; recommending that optimization again would be
incorrect. `cpp/app/workspace/sketch_drag.cpp:523` rebuilds an Assembly scene
during changed placement-reference dragging.

Candidate: retain unchanged base geometry across overlay/dimension changes;
separate source packet, occurrence transform and presentation generations. Reuse
silhouette adjacency until topology changes, but recompute the camera-dependent
visible subset when necessary. Profile transparent sorting/uploads separately.

Gate: identical images and ordered picks for camera motion, transparency,
selection, live dimension/placement dragging, surface visibility and context
sketches. Confirm GPU and CPU selection packets publish together. No coarser
tessellation, dropped edges or lower display precision.

### A7 — Atomic work without copying unrelated histories

Evidence: `cpp/modules/workspace/src/model_calculation.cpp:323` stages a complete
Workspace for relation-driven Assembly regeneration;
`cpp/modules/workspace/src/drawing_annotation_operations.cpp:19` does likewise.
`cpp/modules/assembly/src/assembly_session.cpp:54` copies current, Undo and Redo
states when an Assembly session is copied. Ordinary commits already move their
current state into Undo; do not confuse that with deep-copying at every commit.

Candidate: isolate only the transaction's dependency closure and required current
states, or use immutable shared state with copy-on-write. Preserve the approved
all-or-nothing publication and single Undo step. This is a high-risk change to
stage after lower-risk improvements, despite its potential memory benefit.

Gate: many open documents and long histories, injected calculation/projection
failure, modified sources, one-step Undo/Redo, Cancel, dirty/save state and native
round trip. Track peak private bytes as well as duration. Removing isolation is
not an optimization proposal.

### A8 — Repeated history hashing

Evidence: `cpp/modules/kernel_api/include/zima/kernel/geometry_kernel.hpp:981`
hashes the requested operation prefix from its beginning. The unchanged-prefix
loop in `cpp/modules/kernel_occt/src/occt_kernel.cpp:6273` requests successive
prefixes; save validation in `cpp/modules/document_core/src/part_document.cpp:11999`
does so again. This gives repeated traversal of early operations in long
histories, even where geometric results can be reused.

Candidate: reuse exact per-operation byte encodings and memoize identical prefix
requests within a calculation/save. The prefix length is itself hashed first:
naively extending the preceding final hash would change the existing result.
A new fingerprint scheme would require a deliberate broader design and tests.

Gate: exact old/new fingerprints if claiming representation equivalence; changed
input invalidation for every operation type, placement, dependencies, mesh and
boolean precision, imported geometry and positive/negative zero side choices.
No tolerance-based geometry-key merging.

### A9 — Save/load allocation and discarded scene construction

Evidence: `cpp/modules/document_core/src/part_document.cpp:11997` validates
boundaries and builds JSON; `:712` converts it to INI values with embedded JSON.
Several Sketch/definition serializers produce strings which callers parse back
into JSON. Reference deltas and final-only kernel payload storage already exist.
`cpp/modules/assembly/src/assembly_document.cpp:2282` and `:2337` construct a full
scene for validation and discard it during load/serialization.

Candidate: measure intermediate allocation/encoding costs; use typed JSON
helpers or streaming where this preserves the exact native content. Split
Assembly validation from mesh construction only after identifying every check
currently performed by `build_scene()`. Reuse a valid constructed scene if it is
needed immediately afterwards.

Gate: small and large native documents, damaged references, failed calculations,
Unicode, symbols, Family data, source paths, all persisted references and
save/reopen. Keep every required datum in the native document and retain atomic
save/error behavior; no required sidecar caches.

### A10 — Drawing cache invalidation and source snapshots

Evidence: `cpp/modules/workspace/src/drawing_projection.cpp:71` collects stamps
for all open model documents. A change to an unrelated model invalidates global
reuse. `:109` copies non-Drawing model states into a private Workspace, including
their sessions. `cpp/modules/document_core/include/zima/document/native_read_capture.hpp:35`
compares actual native bytes when validating reads; this prevents stale results
when timestamps are preserved and must not be weakened to timestamps alone.

Candidate: record dependency-specific stamps, use current-state-only read
snapshots, and reuse metadata/geometry independently where actual inputs allow
it. Preserve authoritative unsaved sources and exact byte/generation invalidation.
Camera caches and ordinary interactive 3D-backed projection already exist.

Gate: edit unrelated versus related documents, overwrite files with equal size
and timestamp, rename/missing source, Family, live unsaved annotations and nested
Assemblies. Count source loads, bytes read, copied history records and calculated
cameras. Verify source changes invalidate reuse even when topology IDs stay equal.

### A11 — Drawing refresh and repeated output preparation

Evidence: `cpp/app/drawing_window.cpp:3349` refreshes symbol contacts for all
sheets, reconstructs tabs and calls `set_sheet()`; the latter clears presentation
caches at `:953`. `cpp/modules/drawing_render/src/sheet_renderer.cpp:106` prepares
each view for exact output. `cpp/modules/drawing/src/drawing_document.cpp:422`
performs deferred projection from its source. `sheet_export_context.hpp:5` reads
metadata and validates other view sources per sheet.

Candidate: keep unaffected sheet/view caches and tabs, and share exact
projection/metadata preparation for equal inputs within one export. Separate
annotation/layout invalidation from model geometry. Section/detail/break/crop
settings must participate in any reuse key; equal camera alone is not sufficient.

Gate: multi-sheet PDF/DXF/image output, hidden-edge dashes, hatching, sections,
breaks, crop, title blocks, projected views, dimensions and missing sources.
Keep interactive depth-tested display and continuous grey hidden edges. Never
substitute a display raster for exported/reference geometry.

### A12 — Repeated decoding of embedded content

Evidence: `cpp/modules/symbols/src/instance.cpp:5` parses/evaluates a definition
for validation; `cpp/modules/symbols/src/placement.cpp:55` can parse it again
during mesh/layout construction. Embedded Sweep Sketches are also decoded for
display in `cpp/app/workspace/scene.cpp:321`.

Candidate: immutable parsed-definition and local-geometry caches keyed by exact
authored source, variant, text and geometry-affecting settings. Apply placement
and readable-text orientation afterwards. Bound cache lifetime/memory.

Gate: many repeated symbols, different variants/text, CAD variant, language,
scale, frame/leader, dark/light themes, editing, save/reopen and Drawing handedness.
Small scenes may not benefit enough to justify additional state.

### A13 — Icon cache lookup before SVG reads

Evidence: `cpp/app/resource_icon.cpp:18` opens and reads the resource and performs
colour replacement before checking the existing `QHash<QString,QIcon>` cache.
The cache key depends on path, palette and mode, not freshly read SVG content.

Candidate: check the key before loading the immutable resource. This is a small,
bounded improvement, especially during repeated Tree population, not a proposed
large application speedup. Gate: all icon sizes, neutral and semantic colours,
checked/selected/hover states, theme changes and DPR. No new UI strings needed.

### A14 — Repeated measurement selection

Evidence: `cpp/modules/measurement/src/measurement.cpp:151` already builds a
spatial hierarchy, and `:284` constructs one for each input on every distance
request. `measure_entity()` scans packets to collect one reference.

Candidate: only if profiling shows repeated measurement of unchanged selections,
retain entity extraction and its index for that interaction session. Do not
replace the existing indexed search with a new brute-force algorithm or describe
the current implementation as unindexed.

Gate: touching/intersecting solids, containment, degenerate triangles, large
coordinates, exact planes/axes, witness points, approximation flags and source
changes. Measure build time separately from pair search; retain no stale indexes.

### A15 — Developer build time and reliability

Evidence: `cpp/CMakeLists.txt:444` compiles GUI verification into the product;
`cpp/app/main_workspace.cpp` contains over 13,000 lines and integrated scenarios.
Release staging emitted localized MSVC include messages, and the preceding
release found missing incremental header dependencies in an old stage. Release
2026093004 therefore used a completely fresh stage, not that incremental cache.

Candidate: fix and verify compiler include-prefix/dependency tracking in the
staging configuration before reusing it; separate heavy verification translation
units or use measured build techniques while retaining supported verification
entry points. This primarily helps build/release time, not CAD calculation speed.

Gate: header-touch rebuild test, clean/incremental/link timings, identical version
metadata, packaged startup/contracts and source provenance. Keep the current
root BAT and repository-owned archive/signature checks. Never skip release
validation to make publishing appear faster.

## Coverage and areas not promoted to an optimization task

| Scope | Review result |
| --- | --- |
| App / workspace adapters, dialogs, Tree and View | A4/A6/A13; shared placement reference rows already retain widgets. Protected placement contracts were read, not changed. |
| Sketcher | A1; existing point lookup, graph colouring, rank cache and specialized drag paths acknowledged. |
| Part/document core and kernel API/OCCT | A8/A9; live boundary/ancestry caches already useful in measurements. No tessellation/precision reduction proposed. Sheet geometry remains subject to manufacturing/topology checks. |
| Assembly and dependency refresh | A2/A5/A7; immutable body sharing retained; no implicit mate regeneration proposed. |
| Drawing / drawing_render | A10/A11; current ordinary interactive GPU path and deferred exact output acknowledged. |
| Symbols | A12; definitions/variants/annotation orientation retained. |
| Measurement | A14; existing spatial index confirmed. |
| Interchange | DXF point deduplication already uses an exact coordinate hash (`interchange/src/dxf.cpp:133`); import keeps native identity and checks. STEP/IGES import and kernel work need a large representative input before prioritizing another optimization. |
| Commands / command_host / CLI | Thin dispatch and argument validation lead to shared workspace operations; no independently measured bottleneck. Keep guards and error contracts. |
| Shared UI | Common properties/confirmation and retained placement rows exist. No global dialog rewrite or speculative removal proposed. |
| AI | Provider uses asynchronous QProcess output; bounded shutdown waits are not evidence of ordinary modeling latency. No protocol/security checks removed. |
| Updater / distribution | GUI uses helper-process service; HTTP code's local event loop is in the updater process. Streaming/bounds/signature checks are intentional. No safety checks removed. |
| Research | `zima_transition_core` is linked into document/model code; research folder name does not imply dead code. Separate study/tests are still declared build targets. |
| Tests/build/scripts | Existing performance fixtures used; all tracked C++ translation units are named in build definitions, with the caveat below. |

## Nonfunctional remnants and deletion decision

The all-tracked-file scan checked C++ translation units against CMake source
lists, production headers/includes against references, and preprocessor
`#if 0` blocks. It found no unlisted `.cpp`, no textually unreferenced production
header/include, and no `#if 0` block. Textual reachability is **not** proof that
every function or branch is live, but it supplies no safe whole-file deletion.

Items examined and retained:

- `reference_scene_size` is explicitly ignored in some public construction mesh
  APIs (`part_document.hpp:744`, `:757`; `part_document.cpp:5114`, `:5333`). It is
  a genuine obsolete parameter candidate, but still appears in callers and
  shared reference/placement signatures. Removing it has negligible runtime
  benefit and crosses a protected interface; this audit does not alter it.
- `legacy_definition` branches in Extrusion still interact with currently
  serialized height/extent/direction fields (`profile_serialization.cpp:88`
  onwards). The name alone does not prove the branch is nonfunctional. A separate
  writer/reader and command-coverage audit is required before consolidating it.
- Historical fixture names and Python mentions in comments are not a second
  running Python product. Existing fixtures and benchmark documentation remain
  referenced. No user backups or image files were treated as product dead code.
- Integrated GUI verification is reachable through `--verify-startup`, including
  in release acceptance. Build separation may be useful; deleting it as unused
  would remove a working capability.
- Architecture documentation still contains old status statements about AI,
  portable release and Linux presets. Consult current release records and
  handoffs; stale prose is not evidence that the corresponding implementation
  is absent or removable.

No deletion was made, so no unverified functionality loss was accepted. A later
symbol-level cleanup should require caller/registration/build evidence and a
focused regression gate for each deletion, not just a search with no result.

## Recommended execution order and acceptance

1. Profile and address **A1** using both unsolved and solved-input fixtures.
2. Measure the real GUI picker and scene path, then address **A2–A6** one change
   at a time. Keep full context, exact picking and visual quality.
3. Profile peak memory/serialization on representative large documents before
   undertaking **A7–A10**. These have wider transactional/persistence risk.
4. Take **A13** as a small isolated improvement when convenient; prioritize
   **A11/A12/A14** only with representative repeated-view/symbol/measurement data.
5. Track **A15** separately as build reliability and developer productivity.

For each eventual change, record inputs and build identity, operation counts,
time and memory, then compare actual geometry/properties/references, Undo/Redo,
native round trip and affected GUI contracts. Keep cold/warm measurements and
median/tail latency separate. Run localization coverage; preserve all five
languages for any changed visible text. This audit changes no UI strings.

The original audit was documentation-only and retained Windows 2026093004.
The separately measured implementations and remaining candidates are listed in
the follow-ups above; Windows 2026093005 packages the accepted changes without
replacing the immutable 2026093004 assets.
