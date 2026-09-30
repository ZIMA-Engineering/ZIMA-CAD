# A6 follow-up: object bounds during View preparation

## Scope

Input is an already calculated viewer packet and its oriented annotation frames.
Output remains the same map of geometric envelopes keyed by owner and exact
occurrence path. The means are the existing point accumulation and a lookup
reused only for consecutive samples belonging to that same key.

`object_envelopes()` previously constructed a map key and searched the map for
every triangle vertex, edge sample and reference point. It now remembers the
last map entry during this one call. Map insertions preserve entry addresses.
Every valid sample still reaches the same `ModelEnvelope::include()` call in
the same order. Nothing survives between calls, so changed source geometry
cannot reuse stale bounds. Axis-only fallback and supplied frames are unchanged.

Inspection of `MeshView` confirmed that ordinary repaint and camera navigation
already avoid uploading unchanged base geometry. This change does not redesign
GPU buffers, picking, topology, placement, persistence, or Undo/Redo. It removes
repeated CPU lookups when a new viewer packet is prepared. No tessellation,
precision, geometry samples, or supported behavior is reduced.

## Measurement and verification

The desktop benchmark uses calculated box geometry repeated at 256 and 1,024
distinct Assembly occurrences. This synthetic workload measures scaling; it is
not a claim about every user model. The benchmark reports scene construction,
`set_mesh()`, isolated object bounds, picking and mouse dispatch separately.
Optional first argument writes complete object-bound maps as hexadecimal
floating-point snapshots, including keys, validity, origins and all frame axes.
Before/after snapshots must match exactly. GPU paint time is not measured.

Windows MSVC Release, Intel Core Ultra 9 285HX, Qt 6.11 / OCCT 8. Bounds and
`set_mesh()` times are means of five calls in each run:

| Occurrences | Bounds before | Bounds after | `set_mesh()` before | `set_mesh()` after |
| ---: | ---: | ---: | ---: | ---: |
| 256 | 5.717 ms | 1.472 ms | 26.507 ms | 23.291 ms |
| 1,024 | 24.229 ms | 7.009 ms | 110.950 ms | 93.864 ms |

The isolated phase was approximately 3.5 times faster at 1,024 occurrences;
the complete View preparation took about 15% less time in this run. These
figures do not describe overall application or regeneration performance.
Before/after snapshot SHA-256 values matched at each size:

- 256: `FC0CAA393BB78961CB630ED2F90CA24BE912A04085CEA43421D748CF3C22C01B`
- 1,024: `60EFB8C5647D8F5AFF7D13CA9FF47A9F543A8F1F019EC56DDEFC477FE298754E`

Desktop benchmark hover/click confirmation passed for both sizes. Mouse
dispatch at 1,024 occurrences was 5.243 ms before and 5.301 ms after; this
change does not claim a picking speed improvement. Local logs are
`build/view-bounds-before.log`, `build/view-bounds-after.log`, and
`build/view-bounds-tests.log`.

The dimension-layout regression includes repeated source owners at different
paths, interleaved owners, returning to a previous owner, rotated frames,
invalid references, nonfinite points, axis-only bounds, exclusion of oversized
axes from geometric bounds, and recalculation after geometry changes.

All six focused contracts passed after rebuilding: dimension layout, viewer,
Pattern dimensions GUI, surface placement GUI, selection filter GUI, and
translations (catalog/source coverage and all five languages). GUI and CLI
targets also built successfully. This is not a full-suite pass: the independent
owned-profile GUI fixture failure documented in
[the locked-offset diagnosis](../ASSEMBLY_LOCKED_OFFSET_DIAGNOSIS.md) remains
outside this change.

Localization review: no user-visible text was added or changed. Benchmark and
test diagnostics are developer output only. The development launcher remains
`zima-cad.bat`; this follow-up does not replace the published Windows archive.
