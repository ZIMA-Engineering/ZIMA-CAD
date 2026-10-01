# Helical self-intersection check: disabled-check experiment

The user requested a direct experiment disabling the expensive check and timing
the same spring. This is an experiment report, not a shipped behavior change.

## Method

Starting from `9517be60`, temporarily omit only the final
`BOPAlgo_ArgumentAnalyzer` self-intersection pass in
`make_transported_sweep_data`, including its refinement retry. Keep the pipe's
`BRepCheck_Analyzer`, hollow-section Boolean validity checks, tolerances,
geometry construction, property integration and viewer preparation unchanged.
Build only the Helical test executable; the ordinary GUI/CLI are not relinked.
Use the previous native-input benchmark with fresh kernels, one run per case,
serial execution and no user GUI calculation running.

Inputs are the unchanged user `Projects/02.prtz` and its in-memory
`helical-only` variant. Required output is the same calculated native document;
timing includes request evaluation, geometry and reference/display preparation,
but excludes loading and saving.

## Results

| Input | Check enabled | Check omitted | Reduction |
| --- | ---: | ---: | ---: |
| Complete `02.prtz` | 86.393 s | 20.921 s | 75.8% |
| Helical feature alone | 84.563 s | 22.059 s | 73.9% |

Enabled timings are the preceding clean runs recorded in
[reference properties profiling](20261001-helical-reference-properties.md).
This small experiment shows a dominant cost, not a universal speedup promise.
The slightly longer isolated result demonstrates run-to-run variability; it is
not evidence that removing the Extrusion makes evaluation intrinsically slower.

The complete native document with the check omitted matches the enabled result
byte for byte. SHA-256 of both:
`93eea1d01c56faed2d09f56d45789369921c78ab7c3223402b04a7d9b49d9348`.
Thus this particular accepted input retained its persisted shape, properties,
mesh and references exactly.

## Overlap behavior

An additional diagnostic uses the existing crossing fixture: radius 10 mm,
circular section radius 0.5 mm, pitch 0.5 mm and axial height 0.75 mm. With the
check omitted, evaluation accepts it in 0.532 s and the reloaded BRep passes
`BRepCheck_Analyzer`. It reports volume 74.0267 mm3 and 4,968 triangles.
The established enabled-check contract rejects this fixture.

A successful basic BRep check therefore does not establish absence of geometric
self-intersection. Omitting analysis introduces no union or repair operation.
The result must not be described as a verified overlap-free material solid, and
its integrated volume must not be presented as verified union volume. Subsequent
Boolean operations, treatments and exchange of this crossing fixture were not
verified in this experiment.

## Completion

Temporary production and diagnostic-test edits are restored after measurement.
The normal GUI/CLI remain unchanged. The kernel library and Helical test are
rebuilt from the restored production source so later linking cannot accidentally
pick up the experiment. No UI text or format change is made; no translation keys
are added. The preceding nine-test production validation remains applicable.
No portable release is published. Raw experiment timings are recorded in the
adjacent text file; generated user-document copies remain local under `build/`.

The restored kernel/test build and five-language catalog validation passed.
Logs: `build/helical-check-restored-build.log` and
`build/helical-check-localization.log`.
