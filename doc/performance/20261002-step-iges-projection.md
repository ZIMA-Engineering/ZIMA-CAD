# STEP/IGES import investigation and locator reuse

This follows the [remaining-area investigation](20261001-remaining-investigation.md).
Inputs are the unchanged local STEP/IGES files, import options and existing OCCT
surfaces. Required outputs are the same serialized B-Rep, mesh, topology bindings,
original references and edge-side directions. Means are operation-local reuse of
expensive preparation and the existing read-only capture probes. Source CAD files are
not committed or packaged.

## Located bottleneck

The large STEP has 508 structure nodes. Isolating leaf request 151, definition
`0:1:1:68`, located most time in the edge packet inside `make_result`. An initial
instrumented baseline spent 86,328.5 ms there, versus 736.740 ms meshing, 129.289 ms
calculating volume and 78.6519 ms calculating surface/inertia. Complete component
capture took 92,273.2 ms. These initial diagnostic timings included overlapping work and
are not accepted before/after speedup evidence.

The large IGES baseline completed in 805,813 ms. Its edge packet took 680,145 ms;
read/transfer took 31,076.5 ms, validation 7,536.18 ms, and face/edge/vertex identity
capture took 18,751.1 / 4,309.21 / 2,804.86 ms. This run also overlapped other
diagnostics. It locates the cost but does not establish a speedup ratio.

## Implementation boundaries

IGES identity capture memoizes each immutable runtime shape's exact locator within one
topology-kind pass, including ambiguity counting. Runtime map indices are temporary
lookup slots only. Persisted identity still comes from the source IGES directory pointer
and geometric locator; candidate specificity, ordering and collision handling are
unchanged.

The experimental `sampled_inward_face_directions` owned one optional surface projector
for one edge/face sampling operation. Its first query retained the original surface,
bounds, tolerance and default algorithm/flags. Subsequent edge samples and inside probes
called `Perform` on their actual points. No projection, oriented normal, inside
classification, fallback or tie-break was removed. The object could not survive its
edge/face operation. The OCCT 8 API supports repeated queries.

The minima-only extrema experiment was rejected because it changed the complete isolated
STEP packet. Default-extrema projector reuse was temporarily withdrawn when compared
with an instrumented IGES baseline: that run had hash `8fc6732e...`. A repeated,
uninstrumented original kernel and the locator-only candidate both produced the same
full hash `04cd3edc...` as the combined candidate. Therefore the original discrepancy
does not establish a projector regression. Its cause was not isolated; the accepted
large-result equivalence uses the uninstrumented original and candidates. The
default-extrema reuse was nevertheless rejected after controlled timing failed to
confirm any gain; the original projector construction and search remain. No tolerance,
shape check, sample count, signed-zero semantics, reference or display accuracy is
relaxed. Temporary phase logging is absent from production.

## Locator work count

A disposable diagnostic used the exact `step_shape_locator` function and the same
transferred IGES runtime shapes, entity-child queries and ambiguity-count queries. For
each topology kind it compared every resulting locator in four passes: original, cached,
cached, original. All values matched in all passes. The following counts are
deterministic for this input; timing is not a test gate.

| Kind | Original calculations | Cached calculations |
| --- | ---: | ---: |
| Faces | 143,422 | 27,012 |
| Edges | 681,830 | 132,690 |
| Vertices | 784,738 | 168,358 |
| Total | 1,609,990 | 328,060 |

This removes 1,281,930 repeated locator calculations (79.6%) for this file. It leaves
all 1,609,990 lookups and their original order and values intact. This is not a claim of
a 79.6% reduction in complete import time: edge-side projection still dominates the
overall operation.

## Reproduction and verification

The test executables expose capture diagnostic modes:

```text
zima_cpp_step_model_contract_tests --capture-file <STEP> [leaf-request-index]
zima_cpp_import_model_contract_tests --capture-file <IGES> [diagnostic-json-output]
```

The optional STEP index retains its original deterministic owner. An explicitly supplied
IGES output path writes the diagnostic packet; omitting it leaves the probe read-only.
Both probes hash the complete serialized BodyResult, including B-Rep, viewer packets and
reference bindings. Capture timing excludes the STEP probe's separate structure
inspection and excludes final JSON hashing. It is not complete Part-history construction
time. Supervisors check input hashes before and after and terminate only their owned
diagnostic processes on a bounded timeout.

The six packets of `8073895_DGST-16-10-L-PA.stp` and the tracked `cube-10mm.igs` packet
match the original kernel exactly. Component 151 also matches exactly:
`623949762ec836ff174c295c920d9091e765ff07793e8bac56ab91ff731fabaf`.

The combined candidate passed 13 focused CTests: core kernel, STEP model, import model,
interchange, 2D Sweep, Curve3D Sweep, Helical Sweep, curve placement, edge-treatment
command, edge-treatment query, Part import command, Assembly import command and
translation catalogs. All three GUI contracts passed: 2D Sweep, Helical Sweep and edge
treatment. These checks include applicable geometry sides, import units and identities,
frozen save/reopen/regeneration, original references and Fillet/Chamfer behavior. The
final locator-only variant passed all seven directly affected CTests: core kernel, STEP
model, import model, interchange, Part import command, Assembly import command and
translation catalogs. The local GUI/CLI were rebuilt with this final kernel. These
checks do not constitute a full repository or Linux test run.

Localization review: no product-visible text changed. The five-language catalog check
passed. Added diagnostics and documentation are English. No file format, start template,
launcher, transaction or shared placement contract changed.

## Input fingerprints

| Input | SHA-256 |
| --- | --- |
| `8073895_DGST-16-10-L-PA.stp` | `04bc77574f2f5dc5d49592e12a76904275b39d304bab1ddd354a3ed5dbeb1527` |
| `ze0026-0000-0000.stp` | `7c6a27a4cb568086575ed319e92643e42ca3b27b4b45cf292683898b04f02da5` |
| `ze0026-0000-0000.igs` | `f7abcb4f5ffe48ef42c461db554b6bafb0b3e4401c438dec8d5f23dd38fd5641` |

## Accepted result and measured limit

The repeated original and locator-only large IGES packets match exactly:
`04cd3edc2be4a7e3de0a6c03557cddc97546e459be1e829a0063a6dcb77805f4`. The final six-body
STEP and small IGES packets also match the original kernel. User inputs were verified
unchanged after every complete probe. The larger comparison runs overlapped and are
functional evidence, not controlled end-to-end speed measurements.

After those calculations finished, the locator diagnostic was repeated serially with no
other owned heavy probes or builds running. Configuration: Windows x64 Release, MSVC,
OCCT 8.0.0, Intel Core Ultra 9 285HX; original source baseline `d62aa71a`. Each kind
used original/cached/cached/original order in one process following one file transfer.
Results below are medians of two samples per variant. They measure locator construction
and lookup over the same pre-collected query sequence, including equal result-vector
writes; they exclude transfer and query collection. No cold-start or complete
Part-import speedup is inferred.

| Locator work | Original ms | Cached ms |
| --- | ---: | ---: |
| Faces | 19,007.250 | 3,410.960 |
| Edges | 1,770.510 | 400.495 |
| Vertices | 451.869 | 158.699 |
| Sum of kind medians | 21,229.629 | 3,970.153 |

The only production change is operation-local IGES locator reuse. The original
projection code remains unchanged. The dominant large STEP/IGES edge-side projection
cost is still open; this first pass does not mark large imports solved. No new Windows
release is implied by these local build and test results.

Evidence: [locator measurements](20261002-iges-locator-observations.txt).

 ## Controlled STEP component measurement

After the combined candidate build and tests finished, the isolated STEP component was
measured serially in original/reuse/reuse/original order. Both binaries contain the same
phase-timing instrumentation outside the projection loops. No other owned heavy probe or
build ran concurrently. This is a warm-file, fresh-process comparison of leaf request
151, not the entire 508-node document. All four complete packet hashes match the
original `62394976...` packet above.

| Run | Kernel | Capture ms | Edge packet ms |
| --- | --- | ---: | ---: |
| 1 | Original | 73,427.300 | 68,109.900 |
| 2 | Projector reuse | 73,506.100 | 68,301.500 |
| 3 | Projector reuse | 73,536.000 | 68,280.300 |
| 4 | Original | 73,093.200 | 67,985.500 |

Median component capture: **73,260.250 -> 73,521.050 ms**, **0.4% more elapsed time** on
this operation and machine; no benefit was established. The IGES locator memo is not
involved in this STEP comparison. The experiment would have affected the shared
body-result calculation path. Its kernel, Sweep, placement and edge-treatment tests
passed, but without a measured benefit there is no reason to retain that broader
production change. No overall large STEP/IGES completion-time claim is made.

Evidence: [component samples](20261002-step-component-observations.json).
