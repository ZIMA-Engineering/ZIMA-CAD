# Measurement primitive allocation

Inputs are the same points, segments and triangles of two measurement operands.
Outputs are the unchanged distance, witness points and approximation flags.
The operation-local spatial index previously grew its primitive vector repeatedly
although all three input sizes were known. Reserving their exact combined count
avoids relocation of already copied primitives. Item insertion, partitioning,
bounds, traversal, arithmetic, tolerances and witness tie-breaking remain unchanged.
No persistent cache or source-invalidation policy is introduced.

## Measurements

Windows Release, baseline `5baa32ce`, the existing
`zima_cpp_measurement_contract_tests --benchmark`. Serial baseline/candidate/
baseline/candidate groups, each with three complete runs and twenty point-to-grid
queries per size. Each query checks distance and all three witness coordinates.
The table reports medians of six per-run means, not whole-application speed.

| Triangles | Baseline ms | Candidate ms | Reduction |
| --- | ---: | ---: | ---: |
| 2,500 | 1.09952 | 0.98331 | 10.6% |
| 40,000 | 22.70675 | 20.86100 | 8.1% |

These synthetic inputs quantify allocation overhead; they do not establish that
every real measurement is slow. The complete existing geometry test runs after
each benchmark group and covers finite/analytic operands, witness symmetry,
solid containment, persisted analytic metrics, native measurement records, missing
references and units. All twelve runs passed. The implementation changes capacity
only; it does not change input ordering or numerical calculations.

Five final contracts passed in 20.03 seconds: measurement geometry, measurement
commands and edits, measurement inspector GUI and five-language catalogs. GUI and
CLI were rebuilt. No user-visible strings changed; localization review is complete.
The root development launcher is unchanged. No document format or portable archive
changes are made; this follow-up is after Windows release 2026100101.

[Raw baseline/candidate runs](20261001-measurement-allocation.txt).
Generated evidence: `build/measurement-reserve-regression.log` and
`build/measurement-reserve-app-build.log`.
