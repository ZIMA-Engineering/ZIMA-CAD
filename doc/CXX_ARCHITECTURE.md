# Native C++ architecture

## Product status

C++ is the only ZIMA-CAD product implementation. `zima-cad-cpp` provides the
GUI and `zima-cad-cli` provides command-line execution through the same command
host and model operations. The Python application, its packaging tools and its
runtime were removed on 2026-09-15 with the user's approval. Migration history
remains in Git; it is not an active development or release gate.

[Native behavior](NATIVE_BEHAVIOR_CONTRACT.md), [AGENTS.md](../AGENTS.md) and the
focused domain contracts define required behavior. [Command coverage](CAD_COMMAND_COVERAGE.md)
records the supported operation scope. AI integration and a validated native
portable-release pipeline remain separate work.

## Module boundaries

| Directory under `cpp/modules` | Responsibility |
| --- | --- |
| `document_core` | Native Part model, ownership, history, parameters, persisted calculated state and sessions |
| `kernel_api`, `kernel_occt` | ZIMA calculation requests/results and the narrow OCCT solid-modeling adapter |
| `viewer` | Viewer packets, scene geometry, common candidate list, highlighting, dimensions and rendering |
| `sketcher` | Persisted 2D geometry, constraints, dimensions and solver |
| `assembly` | Exact occurrence paths, dependency graph, mates and Assembly sessions |
| `drawing`, `drawing_render` | Native sheets, views, annotations and drawing presentation/export |
| `interchange`, `measurement` | Import/export and measurement operations |
| `workspace` | Shared document operations, active source context and atomic publication |
| `commands`, `command_host` | Command interfaces and typed command dispatch |
| `ui` | Shared internal dialog presentation and confirmation |

`cpp/app` contains Qt windows, dialogs and GUI adapters. See the
[workspace source map](WORKSPACE_SOURCE_MAP.md) before changing large application
flows. Public interfaces use ZIMA data such as `BodyResult`, `FaceReference` and
`ViewerMesh`. Live OCCT objects do not become UI, reference or document identity.

## Calculation, references and history

OCCT is a dynamically linked solid-modeling dependency behind the native adapter.
It is not a viewer or picker. An independent kernel process/IPC layer should be
introduced only for a measured need. Keep heavy OCCT headers inside adapter
implementation boundaries.

An explicit successful OK or Regenerate calculates bodies and prepares the
persisted viewer/reference data needed for later interaction. Opening Properties,
rollback display, hover, selection and tab changes consume those packets without
hidden OCCT work. A document without a valid calculated result requests explicit
regeneration. History fingerprints prevent stale geometry being accepted for a
changed definition.

Original feature references and result-body display geometry are separate.
`ViewerMesh::original_references` preserves original faces, edges, vertices and
axes with semantic ZIMA identity and exact occurrence paths. Result-body edges
may be drawn without becoming normal placement or mating references. Fillet and
Chamfer explicitly consume the real input body's edges at their operation
boundary. Generated topology persists its source ancestry; OCCT enumeration
order never defines its identity.

Each owner has its own ordered, heterogeneous history. Ownership, local order
and dependencies are separate relations. Shared sessions preserve atomic changes,
transient previews, Undo/Redo and saved state. Failed history calculation retains
the last valid input, marks the failing owner and blocks dependent operations;
independent bodies may continue. A partial failed body is not a valid Boolean,
Mirror or Pattern source. Error state remains editable and is retried during
explicit regeneration after repair.

Assemblies display current calculated source Part geometry, including open,
unsaved sources. They do not pin historical Part revisions. Mate solving and
Assembly-owned operations still require explicit regeneration. The displayed
top-level Assembly and the exact active writable source document are distinct.
Every component is positioned only by its immediate owning Assembly.

## Build and performance

Use CMake and Ninja with prebuilt Qt/OCCT dependencies for normal development.
Keep public headers small; use forward declarations, PImpl or precompiled headers
where measured improvements justify them. A dialog change should not force a
rebuild of the Sketcher or kernel.

Measure clean build, incremental build, linking, targeted/full tests, startup,
regeneration, picking and large Assembly display separately. Historical benchmark
fixtures and observations are in [C++ performance](CXX_PERFORMANCE.md). A speedup
requires reproducible measurements and equivalent verified geometry.

## Deferred analytical edge-treatment optimization

The migration notes contained an approved future optimization worth retaining.
It is a design direction, not a claim that specialized analytical algorithms are
implemented. Keep one Fillet/Chamfer command and one properties contract, with
explicit reference provenance:

- `ORIGINAL_ENTITY_EDGE`: a stable owner and semantic edge from an original
  feature. A specialized analytical method may be considered for a supported
  primitive/profile only when its parameters and pre-operation history prove
  that the relevant neighborhood remains valid.
- `OPERATIONAL_BODY_EDGE`: an edge of the real body immediately before the
  operation, processed by the general OCCT method. It remains exclusive to the
  edge-treatment command and cannot become a general placement or Sketch source.

The common picker must preserve the distinction even when the two candidates
geometrically overlap. Eligibility must be conservative and deterministic;
features after the treatment do not affect it. A changed neighborhood requires
an explicit valid operational reference or an unresolved operation, never a
silent nearest-edge substitution. Before enabling a fast path, compare geometry,
volume, validity limits and regeneration time with the general algorithm.

## Mechanism continuation

The current [purple rotation arm](ASSEMBLY_ROTATION_HANDLE.md) extends the shared
mate-value transaction. Reuse its limits, original references, exact occurrence
ownership and cancellation behavior for later mechanism work.

General dragging through all remaining DOF, a global solver moving multiple
components simultaneously, richer overconstraint diagnostics and broader real
mechanism coverage remain further work. Solver iterations should reuse local
reference equations and physical matrix rotations; Euler display coordinates
must not introduce a false freedom around a 90-degree orientation. A conflicting
step preserves the last valid placement.

## Distribution

Follow [portable release policy](PORTABLE_RELEASE.md) and the
[Linux handoff](LINUX_RELEASE_HANDOFF.md). Windows GUI/CLI build and console
verification passed with the old runtime absent. Linux dependency presets still
need replacement on Linux. This does not establish a validated portable release.
