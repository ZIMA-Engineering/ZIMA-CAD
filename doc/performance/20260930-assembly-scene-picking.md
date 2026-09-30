# Assembly scene transfer and picking — 2026-09-30

This implements a narrow follow-up to A2/A3 of the
[application audit](../PERFORMANCE_AUDIT_20260930.md). The baseline is
`f35f0f32300cc05b80e6b32dca722241998c7c7a`.

## Changes

Assembly scene construction previously copied the completed intermediate mesh
into the merge helper, then copied its edges, references and annotations again
into the final scene. The helper now transfers its owned payload, and the caller
moves the completed intermediate scene. Vertex/index order, datum-first ordering,
reference identities and annotation-frame collision behavior remain unchanged.
The source Part snapshots are never moved from or modified.

The common viewer picker now uses hash sets for membership of complete
`(owner, semantic key, occurrence path)` identities and displayed occurrence
paths. These sets are never enumerated to create candidates. Geometric hit
ordering, stable sorting, tolerances and filtering remain the existing rules.
The already computed original-face hit list is also reused within the same call,
instead of testing the same original triangles twice. Hash storage grows with
unique identities rather than reserving one bucket per triangulation triangle.

There is no cross-call cache, spatial approximation, change to placement solving,
model calculation, serialization or Undo/Redo. New geometry is consumed on the
next normal scene build and pick call. A trial of per-build source-envelope and
Origin reuse did not show a meaningful scene-time improvement on this fixture;
it was excluded from the final change.

## Reproduction and measurement boundaries

Build and run `zima_cpp_assembly_view_benchmark` on a desktop with working OpenGL:

```powershell
cmake --build build/cpp-windows-release --target zima_cpp_assembly_view_benchmark
./build/cpp-windows-release/zima_cpp_assembly_view_benchmark.exe
```

This is an opt-in benchmark, not a timing-sensitive CTest. It displays 256 and
1,024 occurrences of a shared, calculated 10 mm profile solid in a 32-column
grid. Each occurrence retains its own placement and Origin. The normal camera
transition finishes before testing projected occurrence centres. Each measured
scene has respectively 3,072 and 12,288 display triangles, plus original reference
geometry and datums.

Scene construction averages ten builds, including replacement of the preceding
output. `set_mesh` averages five calls, including the input copy and view fit.
Picking and mouse dispatch each average 100 calls over twenty occurrence centres.
Mouse dispatch sends real Qt mouse events through `MeshView`; it excludes queued
painting, GPU time, application-level Tree rebuilding and file loading. The
benchmark additionally verifies that hover and LMB confirmation agree with the
offered occurrence. It does not change the user's configuration or documents.

Measurements use Windows x64, Intel Core Ultra 9 285HX, MSVC 14.51.36231,
Release `/O2 /Ob2 /DNDEBUG`, Qt 6.11 and OCCT 8. The identical benchmark executable
was linked once with the baseline assembly/picking objects and once with the
changed objects. Raw results are recorded in
[the measurement log](20260930-assembly-view.txt).

| Occurrences | Operation | Before (ms) | After (ms) |
|---:|---|---:|---:|
| 256 | Build scene | 64.061 | 35.285 |
| 256 | Set mesh | 30.668 | 31.382 |
| 256 | GUI candidate query | 2.588 | 1.026 |
| 256 | Mouse dispatch | 2.480 | 1.068 |
| 1,024 | Build scene | 236.811 | 151.910 |
| 1,024 | Set mesh | 108.769 | 121.072 |
| 1,024 | GUI candidate query | 16.945 | 5.709 |
| 1,024 | Mouse dispatch | 15.815 | 5.201 |

The large fixture's scene construction took about 36% less time and its mouse
dispatch about 67% less time. `set_mesh` was not changed and did not improve in
this run; its variation is reported rather than hidden. A separate final run
measured 125.149 ms for scene construction and 5.266 ms for mouse dispatch,
illustrating the timing variation even on the same desktop.

These are synthetic fixture measurements, not a guarantee for arbitrary user
Assemblies. `set_mesh`, rendering, recursive source loading and scene/Tree refresh
frequency remain separate optimization candidates. No general frame-rate or
complete application-latency improvement is claimed.

## Correctness checks

Ten focused CTest contracts passed: viewer picking, Assembly, Workspace,
reference-geometry transforms, component activation, component sources,
dimension layout, translations, whole-Origin GUI and selection-filter GUI.
They cover repeated/nested occurrence identities, source changes without
implicit Assembly regeneration, save/reopen, rollback, visibility and command
selection. Shared placement behavior is consumed unchanged.

An additional temporary differential driver compared baseline and changed
serialized scenes, annotation frames and ordered GUI candidates. It also
compared 1,024 ray/flag combinations across both scene sizes after mixing valid,
invalid and changed display-face identities. All eight combinations of the
result-face, original-container and original-face flags were exercised. Exact
comparison includes candidate distances, order, geometry indices, ownership and
occurrence paths; it does not merely compare candidate counts.

Localization review found no new or changed product UI text. The translation
contract, including catalog and coverage checks, passed. Benchmark diagnostics
and documentation are English.

## Delivery

The local GUI and CLI are rebuilt. The repository-root `zima-cad.bat` remains
the development entry point. Published Windows release `2026093004` is unchanged;
this optimization is in the current source and local build, not that archive.
