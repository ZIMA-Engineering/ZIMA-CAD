# Measurement index partition centres

Inputs are the same finite measurement points, segments and triangles. Outputs
are the same shortest distance, witness points and approximation classification.
During each spatial-index partition, the selected split axis is fixed. Previously
`nth_element` repeatedly summed and divided the same primitive coordinates in its
comparator. Each primitive now stores that partition's centre once, using the
original arithmetic, and the comparator reads it. Child partitions overwrite the
value for their own axis. Bounds, split choice, partition algorithm, tie comparison,
primitive insertion order and geometric distance calculations are unchanged.

The value belongs only to the operation-local index. It cannot survive a source
change or affect persistence/Undo. The tradeoff is one additional temporary double
per primitive (about 313 KiB for 40,000 primitives on this Windows build). No cache
invalidation policy, precision reduction or skipped geometric check is introduced.

## Paired measurements

Windows Release, baseline `a7d0f475` with the same additional probes. Original and
candidate executables ran serially in baseline/candidate/candidate/baseline order,
three groups. Each native execution performs twelve queries; each grid execution
performs twenty per size. The table reports medians of six per-execution means.
Loading native data and preparing the selected geometry are outside this timing.

| Distance query | Baseline ms | Candidate ms | Reduction |
| --- | ---: | ---: | ---: |
| Point outside saved H-Sweep, 25,260 triangles | 12.14865 | 7.54906 | 37.9% |
| Point above planar grid, 2,500 triangles | 0.91131 | 0.62642 | 31.3% |
| Point above planar grid, 40,000 triangles | 16.44595 | 11.00060 | 33.1% |

The existing native fixture `helical-noop.prtz` is read without regeneration or
saving. SHA-256:
`5e21b0091947f478a5307bb76e250071b108c17a937ee1c629a7eb767125a3f9`.
The native distance's approximate flag remains true. All exact hexadecimal
witness/distance signatures match. These measurements concern distance calculation,
not complete dialog interaction or whole-application performance.

## Equivalence and regression

An additional 96-case differential probe compares every distance and both witness
positions exactly, including signed floating-point text, for point, segment,
triangle and mixed sets, degenerate elements, tied coordinates, finite queries,
planes and reversed operand order. All match the original executable. Repeated
native queries return the identical result and leave the serialized source packet
unchanged. Grid queries retain independent expected distance/witness assertions.
The partition probe also checks that witness separation equals the reported
nonnegative distance and is registered as a separate CTest entry.

All six final contracts passed in 17.44 seconds: measurement geometry, partition
witnesses, measurement commands and edits, measurement inspector GUI and the
five-language catalogs. GUI and CLI were rebuilt. Generated evidence:
`build/measurement-partition-regression.log` and
`build/measurement-partition-app-build.log`.
Localization review: no product UI strings changed. Native files, shared placement,
reference identities and the development launcher remain unchanged. This source
change is after Windows 2026100101 and does not modify its published archive.
Linux acceptance remains separate.

[Raw paired timings and exact signatures](20261001-measurement-partition-centres.txt).
Reproduce using `zima_cpp_measurement_contract_tests --benchmark`,
`--partition-proof`, or `--distance-file <native-part>`.
