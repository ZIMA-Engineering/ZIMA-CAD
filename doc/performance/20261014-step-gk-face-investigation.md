# G92K1 STEP volume-integration scheduling, 2026-10-14

This records the initial investigation and the verified scheduling optimization
in the shared C++ implementation on Windows. The native format, OCCT 8.0.0 pin,
SDK patch and integration tolerance are unchanged. The local GUI/CLI build retains
identity 2026101303 and includes this working-tree change; this is not a new
published release. Linux was not exercised.

## Scope and evidence

The input is the original G92K1 STEP identified in
[the previous complete import measurement](20261013-large-model-followup.md).
That complete backend import took 692 seconds, including 607 seconds of
span-aware Gauss--Kronrod volume integration. Those remain the authoritative
complete-import timings.

The initial separate diagnostic loads the STEP with STEPControl_Reader, enumerates 6,344
oriented faces, computes the common vertex-average reference point and runs
the existing four-worker, ordered face integration at 1e-12. It is a kernel
diagnostic, not the application's complete STEPCAF import lifecycle. Face
indices below are temporary diagnostic positions, never persistent identities.

The probe completed 6,343 faces before being stopped. Face 3619 remained
unfinished. Therefore there is no new completed aggregate volume, import
duration, or before/after speedup. Other exploratory work overlapped parts of
the probe, so its durations are diagnostic observations rather than controlled
benchmark results. Log: `build/step-gk-profile.log`.

| Diagnostic face | Surface type | Observed completed duration |
| --- | --- | ---: |
| 357 | Surface of extrusion | 59.17 s |
| 4187 | Surface of extrusion | 42.86 s |
| 801 | Surface of extrusion | 40.49 s |
| 3762 | Surface of extrusion | 14.60 s |
| 5055 | B-spline surface | 4.59 s |
| 4148 | B-spline surface | 3.94 s |

The previously captured diagnostic face 3619 has a non-rational, degree-five
extrusion basis with 257 knots and 771 poles. Two boundary curves report 3,388
integration subdivisions each. Samples on those approximately horizontal
boundaries have small, nonzero transverse derivatives, around 1e-11 to 1e-10.
They must not be rounded away or treated as exact straight parameter curves
merely to obtain speed.

## Rejected or unfinished experiments

An exploratory change enabled boundary-knot subdivision on extrusion surfaces.
It did not complete within the exploratory run and was stopped. There is no
accepted accuracy or performance result for that change.

Another standalone experiment reused exact-U evaluations of an extrusion basis
curve. Its cache observed approximately 95% hits, but a high hit rate did not
establish a faster or equivalent integration. Both the fast experiment and the
point/derivative verification run were stopped without a final volume. The
verification compared evaluated points and first derivatives, not a completed
physical-property packet; it does not prove preserved model properties. The
experimental executable recompiles several kernel translation units, so that
build must also be controlled before drawing a performance conclusion.

The original installed kernel completed integration of the isolated, serialized
face 4187 in 32.01 s, with volume -6707.8554858589832 and reported absolute error
2.5219894913476298e-6. These are signed, reference-point-dependent contributions,
not the model's physical volume. `BRepGProp_VinertGK::Perform` returns absolute
error, despite maintaining a separate relative error field internally. Do not
compare this absolute contribution error directly with the requested relative
tolerance or report it as a relative model-volume error. The initial note used
the wrong error classification; source inspection corrected it.
Log: `build/step-gk-original-4187.log`.

## Next verification requirements

Investigate quadrature refinement and stopping on the long extrusion boundary,
using the exact application's prepared shape as well as isolated fixtures.
Keep the existing tolerance, real trimming curves and complete properties.
Control compiler flags and run baseline/candidate serially. Any accepted change
requires completed large-import measurements, independent property checks,
native save/reopen and relevant existing import regressions. A shared kernel
patch also requires aligned Windows/Linux SDK provenance and explicit reporting
of Linux verification gaps.

Temporary probes, extracted faces and copied kernel sources remain under ignored
`build/`; they are excluded from releases. No user-visible text was changed, so
there are no new localization keys or catalogs to translate. Original user
documents, local configuration and unrelated library edits were not modified.

## Controlled continuation

Standalone kernel copies must use the SDK's Release definitions, notably
`NDEBUG` and `No_Exception`, as well as its precise floating-point mode. The
initial standalone experiment omitted those definitions. The unchanged copied
kernel rebuilt with the SDK's flags returned exactly the installed kernel's
volume and absolute error on serialized face 4187, in 37.40 s. This controls
the result discrepancy before evaluating a candidate; these single-run times
are still observations rather than hardware-independent performance gates.

An alternative schedules the original face's independent boundary curves.
Each worker still passes the complete original face to `BRepGProp_Face`; a
temporary single-edge domain restricts only iteration. VinertGK uses the same
0.1-times-requested curve tolerance regardless of the domain's curve count.
Reduction retains curve order within each face and face order within the shape.
It leaves the native kernel, geometry, real pcurves and 1e-12 tolerance intact.

On isolated face 4187 this completed in 19.46 s, with exactly the original
volume and absolute error. Its approximately horizontal boundary curves took
19.32/19.45 s concurrently; the other two curves took 0.0067/0.000057 s.
Logs: `build/step-gk-control-release-4187.log` and
`build/step-gk-edges-4187.log`.

The shared C++ candidate enables this scheduling only for imported STEP
extrusion faces with a boundary above 64 spline intervals. Other faces retain
whole-face tasks; FORM does not opt into boundary splitting. On the G92K1
application import, 6,344 faces produce 6,405 tasks, with 15 split faces and
the existing four volume workers.

The expanded STEP property regression passed eight combinations: 64/65 trim
intervals, straight/slightly perturbed trims, and original/rotated-translated
placement. It confirms the intended split predicate, exact serial GK volumes,
exact existing Gauss centroids/inertia, the independent finite slab-volume
bound and complete native definition/packet equality through save/reopen.
Existing rational cylinders/tori and repeated/rotated mixed components passed
as well. Log: `build/step-edge-properties-tests.log`.

## Complete import and equivalence

The application-level probe completed a fresh import of the same original STEP
and saved a separate native document. It ran without a concurrent build or
regression suite. The original input hash still matches the previous record.
Before values are the complete 2026-10-13 Windows measurements on this machine;
after values are this 2026-10-14 local Release run. Inputs are warmed by preceding
reads. These are individual observations, not repeated cold-disk distributions
or universal speedup promises.

| Operation | Before | After |
| --- | ---: | ---: |
| Complete backend Part import | 692.036 s | 520.563 s |
| Span-aware GK volume integration | 607.169 s | 414.875 s |
| Overlapped full tensor integration | 43.493 s | 49.512 s |
| Native save, including validation reload | 41.402 s | 47.417 s |

Complete backend import improved by approximately 24.8%; GK integration improved
by approximately 31.7%. Save and tensor timings did not improve. Those paths and
their calculation precision are unchanged; do not attribute their variation to
an established improvement. Final GUI scene publication is outside both backend
import timings. The import still takes several minutes on this input.

Both native imports occupy 409,117,316 bytes. The new result retains one boundary,
205,818 vertices, 284,803 triangles and 17,216 original edges. Comparing all four
saved aggregate/local/input/output physical-property snapshots against
`build/G92K1-import-parallel.prtz` found exact equality, including volume, area,
current/solid flags, surface centroid, volume centroid and every inertia entry.
Logs: `build/STEP-G92K1-edge-scheduling.log` and
`build/STEP-G92K1-edge-properties-comparison.log`.

A separate fresh kernel evaluated the original saved native document's authored
history, preserving its identities rather than importing a new document. Its
complete serialized calculation packet matched the stored packet exactly:
SHA-256 `3198a866ee4c5bbe390e3937de97376579a43b1162089c225928e00ba842e93d`.
This compares the complete geometry, physical properties, reference/side data
and nested snapshots, not just volume or mesh counts. Ordinary history evaluation
prepared a fresh boundary; it did not accept the stored calculated packet as its
result. Functional tests overlapped this correctness run, so its elapsed time is
not a controlled performance comparison. Log:
`build/STEP-G92K1-edge-packet-equivalence.log`.

Reproduce with `zima_cpp_step_model_contract_tests --import-native-performance
<STEP> <new-output.prtz>` and `--recalculate-native-equivalence <saved-input.prtz>`.
Enable `ZIMA_CPP_IMPORT_PROFILE=1` and `ZIMA_CPP_CALC_PROFILE=1` for the first probe;
use the existing metadata test's `--compare-native-properties <before> <after>`
for saved property snapshots. Keep output paths separate from user originals.

## Final verification and limits

All nine selected native/GUI contracts passed: ordinary import, STEP hierarchy
and frozen persistence, OCCT high-span intervals, expanded strict STEP properties,
import mouse interaction, Extrusion context, plane visibility, translation
coverage/catalog/UI validation in all five languages, and Body properties
analysis/history/transactions/persistence. Log:
`build/step-edge-final-regressions.log`. The eight long-trim cases and existing
rational/repeated/mixed cases are the explicit property matrix above; they are
not an exhaustive test of all possible STEP geometry.

The plane GUI contract additionally passed on a separate copy of the newly
imported G92K1. It exercises actual face selection, repeated offsets through
zero, the independent plane equation, and Cancel restoration of document/camera.
Log/capture: `build/step-edge-G92K1-gui-ctest.log` and
`build/step-edge-G92K1-plane-ctest.png`; the capture was visually inspected.
Body-property workflows cover current-analysis reuse and authored-state/Undo
transaction behavior. No broader untested GUI or Sketcher coverage is claimed.

The rebuilt local GUI and CLI are selected by the unchanged root
`zima-cad.bat`. Original user files, local configuration and unrelated library
edits are retained. There is no user-visible text change; profiling diagnostics
remain developer-only output. The translation contract passed. Linux timing and
execution are unverified, although both platforms consume this same C++ path
and the unchanged pinned SDK source/patch. No release assets were replaced.
