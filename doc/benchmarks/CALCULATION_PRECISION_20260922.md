# Calculation tolerance benchmark, 2026-09-22

Changing linear calculation tolerance from 0.001 to 0.01 mm did not provide a
consistent useful speedup on these fixtures. This is calculation precision,
not the number of displayed decimal places. No product default was changed.

## Method

Windows x64 Release build, native Qt/OCCT runtime, one warm-up cycle followed by
five measured cycles alternating 0.01 and 0.001 mm. No compiler or other test
process ran during the final benchmark. Mesh deflection remained unchanged
(0.1 mm for the synthetic fixtures). The original Part was opened as read-only
input and never saved. GUI rendering times below are CPU sheet painting to a
1200 by 1600 QImage, not GPU frame latency or end-to-end user interaction.

The production file-settings transaction recalculates a Part and refreshes its
metadata. Assembly measurements separate source refresh, explicit regeneration,
scene construction and an Assembly-owned subtractive extrusion. Three drawing
views use Front, Top and Isometric projections. The larger synthetic assembly
has eight occurrences of a Part containing twelve separate cylinders.

Reproduce from the repository root after building `zima_cpp_precision_benchmark`:

```powershell
$env:QT_QPA_PLATFORM='offscreen'
build/cpp-windows-release/zima_cpp_precision_benchmark.exe build/precision.json Projects/01.prtz
```

The optional second argument selects the real Part fixture. All individual
samples, geometry checks and per-boundary counts are in
[the raw report](calculation-precision-20260922.json).

## Median elapsed times

Times are milliseconds; each cell is the median of five samples.

| Workload | 0.001 mm | 0.01 mm |
| --- | ---: | ---: |
| One cylinder: Part settings transaction | 5.30 | 5.30 |
| Twelve cylinders: Part settings transaction | 293.92 | 307.94 |
| Eight occurrences of one cylinder: explicit Assembly regeneration | 17.24 | 17.65 |
| Eight occurrences of twelve cylinders: explicit Assembly regeneration | 216.07 | 209.35 |
| Assembly-owned cut, one-cylinder source | 18.97 | 19.53 |
| Assembly-owned cut, twelve-cylinder source | 94.83 | 92.20 |
| Three drawing projections, one cylinder | 1.59 | 1.55 |
| Three drawing projections, twelve cylinders | 17.43 | 17.95 |
| Drawing CPU paint, one cylinder | 1.22 | 1.27 |
| Drawing CPU paint, twelve cylinders | 7.88 | 8.36 |
| Real Part: complete settings transaction | 5447.15 | 5472.58 |

An earlier run of the real Part measured approximately 6093 versus 6073 ms.
That variation is much greater than the difference between tolerances. These
measurements do not establish a general speed advantage for either tolerance.

## Geometry and the unexpected triangle increase

The synthetic cylinder models retained their volumes and triangle counts:
124 triangles for one cylinder, 1524 for twelve. The real Part changed from
1205 to 1351 triangles and from 378017.2371939001 to 378017.4794864017 mm³.
The 0.2422925 mm³ volume difference is approximately 0.0000641 percent. This is
a measured numerical geometry change, not a change to an authored dimension.
It must not be described as identical geometry.

The first six operation boundaries had identical triangle counts for both
tolerances: 12, 367, 403, 435, 475, 511. At the seventh operation, the 2D sweep,
counts diverged to 1039 versus 1223. The subsequent fillet produced the final
1205 versus 1351. All five cycles reproduced these counts and volumes exactly;
all recorded calculation-error maps were empty. Most additional triangles
belong to the sweep's persisted face owners. This locates the difference; it
does not prove the detailed OCCT algorithmic cause. A looser solid calculation
tolerance is not a request for a coarser display mesh.

## Incremental operation diagnostics

Each call extends a calculated operation prefix by one operation, using the
previous native boundaries. These timings include history/cache handling and
result extraction; they are not isolated low-level sweep or fillet solver time.
They also exclude the full application transaction and reference-resolution
work, so they must not be summed as a decomposition of the full settings time.

| Operation index (one-based) | 0.001 mm | 0.01 mm |
| --- | ---: | ---: |
| 1 | 6.25 | 6.59 |
| 2 | 48.31 | 48.38 |
| 3 | 76.64 | 72.24 |
| 4 | 97.94 | 100.14 |
| 5 | 127.72 | 132.30 |
| 6 | 166.02 | 166.88 |
| 7: 2D sweep | 533.68 | 525.01 |
| 8: fillet | 901.74 | 872.48 |

The sweep is a comparatively expensive step in this model, and the following
fillet costs more. Profiling the complete application transaction would be the
next step before selecting an optimization. This benchmark does not modify
shared placement or reference-solving behavior.

## Settings-path audit

`set_file_settings` compares the actual native `linear_tolerance` values and
requests calculation when operation fingerprints change. `PartDocument` passes
that tolerance into `sweep2d_request`, whose `Sweep3DRequest.linear_tolerance`
reaches OCCT's pipe/loft builders and Boolean fuzzy tolerance. Mesh deflection
is a separate setting. Angular tolerance and several topology/construction
thresholds remain separate as well. The control is therefore not a global
multiplier for every arithmetic operation or iteration count.

OCCT documents pipe
[3D, boundary and angular tolerances](https://www.occt3d.com/dev/doc/refman/html/class_b_rep_offset_a_p_i___make_pipe_shell.html)
as separate parameters. Its
[Boolean documentation](https://www.occt3d.com/dev/doc/overview/html/specification__boolean_operations.html)
describes fuzzy tolerance as an additional geometric tolerance and gives
case-specific performance examples. Neither establishes inverse proportionality
between tolerance and execution time. The source audit found no ignored-setting
error in this path; it is not a claim that all performance defects are excluded.

## Follow-up: ten times higher precision

At the user's request, the same benchmark was repeated with 0.0001 versus
0.001 mm, using five measured alternating cycles after warm-up. This leaves
the user's files and product defaults unchanged. Reproduction:

```powershell
build/cpp-windows-release/zima_cpp_precision_benchmark.exe build/precision-higher.json Projects/01.prtz 0.0001 0.001
```

See [all higher-precision samples](calculation-precision-20260922-higher.json).
Compare within this run; machine timing varies between runs.

| Workload, median milliseconds | 0.001 mm | 0.0001 mm |
| --- | ---: | ---: |
| One cylinder: Part settings | 4.97 | 4.83 |
| Twelve cylinders: Part settings | 300.75 | 299.11 |
| Eight one-cylinder occurrences: Assembly regeneration | 14.46 | 15.22 |
| Eight twelve-cylinder occurrences: Assembly regeneration | 207.11 | 201.85 |
| Assembly cut, one-cylinder source | 19.92 | 20.65 |
| Assembly cut, twelve-cylinder source | 90.71 | 88.67 |
| Three drawing projections, one cylinder | 1.38 | 1.50 |
| Three drawing projections, twelve cylinders | 17.19 | 15.60 |
| Drawing CPU paint, one cylinder | 1.11 | 1.27 |
| Drawing CPU paint, twelve cylinders | 8.02 | 7.14 |
| Real Part: complete settings transaction | 5615.92 | 5701.91 |
| Real Part: incremental sweep call | 499.94 | 496.01 |
| Real Part: incremental fillet call | 853.91 | 857.16 |

The real Part's transaction median increased by about 1.5 percent, which is
small relative to observed run-to-run variation. Its triangle count decreased
from 1205 to 1179. Volume changed from 378017.2371939001 to
378017.23609973374 mm³, a decrease of approximately 0.00109417 mm³.
The synthetic cylinder fixtures retained their counts and exact volumes.
These cases provide no evidence of a tenfold work/time relationship and do
not justify changing the default tolerance for performance.
