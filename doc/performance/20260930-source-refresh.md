# Repeated subassembly source refresh — 2026-09-30

This A5 follow-up uses `bb71010f2bdfb8a50bdae5d8f1bbfe5655d59d0e`
as its baseline. Previously, repeated subassemblies shared their completed
geometry but still copied their source document, recursively checked its children
and compared occurrence metadata before reaching that shared result.

`Workspace::refresh_source_geometry()` now retains each successfully refreshed
subassembly's body, nested occurrence metadata and source name for the duration
of that call. The key includes the source document identity (including a Family
member) and source path. An open source uses its authoritative open path. Each
occurrence retains its own placement, visibility, name and ownership. The active
recursion stack is checked before reuse, independently of the completed-source
map. Missing sources and Assembly-owned operation results retain their existing
paths. No cross-refresh cache, file-format change, geometric approximation or
new calculation is introduced.

## Measurements

Windows x64, Core Ultra 9 285HX, MSVC Release, Qt 6.11, OCCT 8. The synthetic
fixture repeats a subassembly containing eight calculated 10 mm cubes. Timings
cover source refresh only, excluding scene construction, GPU upload and drawing.
Warm values average five calls; cold means one first call on a fresh Workspace,
not a flushed operating-system disk cache.

| Sources | Groups / Parts | Baseline warm (ms) | Changed warm (ms) | Baseline first (ms) | Changed first (ms) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Closed native files | 32 / 256 | 7.443 | 0.351 | 19.221 | 12.496 |
| Closed native files | 128 / 1,024 | 29.775 | 0.578 | 39.796 | 7.589 |
| Open documents | 32 / 256 | 0.518 | 0.105 | 2.438 | 1.959 |
| Open documents | 128 / 1,024 | 2.442 | 0.291 | 4.484 | 2.437 |

These fixtures demonstrate the cost of repeated traversal; they do not predict
whole-application latency or every user model. [Raw results](20260930-source-refresh.txt).

## Checks and reproduction

```powershell
cmake --build build/cpp-windows-release --target zima_cpp_source_refresh_benchmark
./build/cpp-windows-release/zima_cpp_source_refresh_benchmark.exe
```

The benchmark checks shared body identity, nested metadata, occurrence identity
and placement, and unchanged revision/dirty/Undo state. It reports 3,072 and
12,288 displayed triangles in both implementations. It is intentionally not a
timing-sensitive CTest.

The Workspace contract covers repeated closed sources, unsaved open Part edits,
relative paths, native reopening, missing/reappearing subassemblies, unsaved
source names and appearance, nested visibility/placement, Undo/Redo and cycle
rejection. Family, rename and component-property contracts exercise related
source ownership paths. Display refresh continues to use calculated source
data; it does not explicitly solve mates or evaluate body operations. No
protected shared placement or transaction contract was changed.

Localization review: no new user-visible text. The cycle error is the existing
message. Local GUI/CLI builds remain accessible through `zima-cad.bat`; this
follow-up does not replace the immutable Windows 2026093004 archive.

### Validation result and existing limitation

Follow-up: the [locked-offset frame correction](../ASSEMBLY_LOCKED_OFFSET_DIAGNOSIS.md)
resolves the independent failure below after explicit shared-placement approval.
The complete Assembly profile contract passes with that subsequent correction.

Eight focused contracts passed: Workspace, Family, file rename, component
properties, translations, Feature GUI, Relation picker GUI and presentation
refresh GUI. The Feature/Relation checks also cover the accompanying change to
hide zero draft-angle annotations while retaining positive/negative picking.

`zima_cpp_assembly_profile_command_tests` fails at line 170: locked reference
offset editing expects placement Z = 3 but produces 5. Rebuilding that test with
the original `workspace.cpp` from the baseline commit reproduces the identical
failure. This pre-existing shared-placement issue is not fixed or weakened in
this follow-up. Earlier extrusion/revolution cut checks in that executable run
before the failure; assertions after the failure are not verified by this run.
