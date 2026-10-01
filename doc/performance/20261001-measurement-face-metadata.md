# Repeated face metadata during object measurement

Inputs are persisted viewer triangles and exact face identities (owner, semantic
key and occurrence path). Outputs are the same measured geometry, area, volume,
closure classification and approximation flags. `measure_entity()` previously
constructed three-string map keys for every accepted triangle, including long
runs belonging to one face. It now retains the last face's map-value address for
that collection only. A different identity performs the original lookup.

The ordered map remains: area summation order is unchanged. Every accepted
triangle still writes its stored area, including an unknown value, preserving
last-value-wins behavior. Invalid/nonmatching triangles are skipped before the
shortcut. Map elements and the source reference array remain stable throughout
collection. There is no persistent cache, source mutation or changed geometry
calculation. Source changes on subsequent measurements cannot reuse old entries.

## Measurements

Windows Release, baseline `4f0bd353`, same toolchain/runtime. Two executables
were run serially in baseline/candidate/candidate/baseline order, three groups.
Each execution performs twelve queries; the table reports the median of six
per-execution means. Native loading is outside the timed interval. The first
result snapshot/checking overhead is present in both versions.

| Input | Baseline ms | Candidate ms | Reduction |
| --- | ---: | ---: | ---: |
| Saved H-Sweep, 25,260 triangles | 19.67815 | 16.29715 | 17.2% |
| 64 synthetic boxes, 768 triangles | 0.31730 | 0.25870 | 18.5% |
| 2,048 synthetic boxes, 24,576 triangles | 20.27520 | 18.02855 | 11.1% |

The native fixture is the existing local `helical-noop.prtz`, read without
regeneration or saving. SHA-256:
`5e21b0091947f478a5307bb76e250071b108c17a937ee1c629a7eb767125a3f9`.
This local file is not added to source. Its source packet is verified unchanged.
All native and synthetic area/volume signatures match exactly, including closure
and approximation flags. The native result remains approximate, as before.
These are operation-specific measurements, not overall application speedups.
The larger baseline synthetic case has one 74.59 ms outlier; raw samples are
retained and the median is reported rather than treating that outlier as a gain.

## Verification and limits

New cases cover repeated and non-adjacent faces, distinct owners/keys/occurrences,
known-to-unknown and unknown-to-known area records, and invalid triangles. Geometry
checks also exercise open, reversed, non-manifold, collapsed and translated meshes.
All pass on both baseline and candidate. The original closed-surface algorithm,
quantization, oriented edge counts and numerical tolerances remain unchanged.
Exploratory hash/sorted-edge alternatives were not retained because their small-
case timing benefit was inconsistent; this change only addresses face metadata.

Five final contracts passed in 20.57 seconds: measurement geometry, commands,
edits, measurement inspector GUI and five-language catalogs. The GUI/CLI were
rebuilt. No product UI text changed; localization review is complete. Native
formats, the development BAT and user settings are unchanged. This source change
is after Windows 2026100101 and does not modify its published archive. Linux
verification remains separate.

[Raw paired results](20261001-measurement-face-metadata.txt).
Reproduce with `zima_cpp_measurement_contract_tests --surface-benchmark` or
`zima_cpp_measurement_contract_tests --surface-file <native-part>`.
Generated evidence: `build/measurement-face-regression.log`,
`build/measurement-face-app-build.log`, and `build/measurement-face-paired.log`.
