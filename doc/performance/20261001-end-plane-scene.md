# End-plane scene preparation — 2026-10-01

## Scope and change

Inputs are the already calculated viewer packet and its original end-plane
references. Output is the same ordered viewer packet, including the existing
border where present and the same generated border otherwise. No kernel call,
coordinate, tolerance, datum size, occurrence identity or persistence changes.

Previously `MeshView::set_mesh` searched every displayed edge separately for
each end plane. It now removes already represented plane keys from its local
pending map in one edge traversal. The exact key still includes owner, semantic
key and instance path. The remaining map retains its original iteration order.
No state survives the operation, so changed geometry requires no cache invalidation.

## Paired measurement

Baseline product source: `36c63fc7`. The same extended benchmark was compiled
against baseline and candidate on Windows. Three ABBA groups provide six runs
per version, each with eight timed preparations after one warm-up. Input copying,
serialization and correctness checks are outside the timed `set_mesh` call.

| Synthetic scene | Baseline median run mean | Candidate | Reduction |
| --- | ---: | ---: | ---: |
| Empty scene | 0.001 ms | 0.001 ms | Below useful timer resolution |
| One existing plane border | 0.004 ms | 0.003 ms | Too small for a useful claim |
| 64 occurrence planes | 2.5675 ms | 0.5255 ms | 79.5% |
| 256 occurrence planes | 34.470 ms | 2.037 ms | 94.1% |

Each occurrence has twenty ordinary edges; half already contain a plane border.
This is a synthetic scaling probe, not a whole-application or GPU frame-rate claim.
The 256-plane scene saves about 32.4 ms of CPU scene preparation in this probe.

## Equivalence and regression gates

All 48 complete serialized viewer-packet hashes match their corresponding
fixture across versions. Each run checks repeated preparation, empty/restored
scenes, exact occurrence ownership, preserved existing borders, generated border
counts and unchanged source packets. Changed source vertices and exact restoration
are additionally checked in the final contract. The new CTest entry runs `--end-planes` in
`zima_cpp_silhouette_view_benchmark`; wall-clock results are never pass thresholds.

All nineteen selected Windows tests passed in 79.56 seconds: the new scene
contract, viewer/picking, 2D and Helical Sweep, Assembly/scoped refresh, selection
filters, Drawing view/dependency reuse, STEP/IGES model import, measurement core,
partition, commands, editing, inspector, drawing dimensions/picking, and catalogs.
This is a focused gate, not the complete repository suite. The subsequent import
profiling option changes test diagnostics only; its default contract, final scene
contract and catalogs passed again (three tests in 6.44 seconds).

No product text changed. Five-language catalog validation passed. Documentation
and new test diagnostics are English. Shared placement and the development BAT
are unchanged. Linux verification remains assigned to the Linux host.

Evidence: [paired measurements and regression output](20261001-end-plane-scene.txt).
