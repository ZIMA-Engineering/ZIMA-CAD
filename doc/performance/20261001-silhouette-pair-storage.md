# Silhouette adjacency pair storage — 2026-10-01

## Scope and implementation

Inputs are the calculated viewer triangles, their existing face keys and the
persisted topological edges. The required output is the identical ordered list
of silhouette candidates: first/second endpoints and both triangle normals.
This work runs when a changed mesh is uploaded; the camera-dependent visible
subset continues to use the existing renderer.

Previously each adjacency-map entry allocated a growable vector of triangle
records. The next stage uses only entries with exactly two records, discarding
singletons and non-manifold edges. Each entry now stores the first record and
the second normal inline, plus a count capped at three. The second record's endpoints were never consumed.
The pair retains its original order; reaching
three permanently makes the entry ineligible, including after further incidents.
This removes per-edge vector allocations and growth without changing eligibility.

Face labels were also copied into temporary and stored adjacency keys for every
triangle edge. Each exact label is now interned once in a local ordered string
set; adjacency keys hold string views into those stable set nodes. The set is
declared before the adjacency map, so it outlives every view. The output contains
only copied geometric values and retains no views into this temporary pool.
Key values and lexicographic ordering are identical to the previous string keys.

The ordered traversal, face-key values, coordinate rounding, topology exclusion, normal
arithmetic, candidate order and view-direction classification are unchanged.
No persistent topology, geometric precision, reference ownership, data format,
placement behavior, OCCT calculation or Undo/Redo contract changes. The existing
GPU invalidation lifecycle still handles changed and empty geometry.

## Verification method

Baseline: `cecc9726`. The same benchmark extension is used before and after:
64/256 cylindrical and spherical occurrences, the user's `Projects/02.prtz`
spring, and a new non-manifold fixture. The latter duplicates one triangle three
times and adds a degenerate and an invalid-index triangle, exercising the
more-than-two rule and existing invalid-input guards.

Temporary instrumentation times the silhouette preparation block, including
its temporary map destruction. After stopping the timer, it hashes every float
coordinate of each ordered candidate's endpoints and normals. This measurement
code is removed from the final product. The native benchmark also compares full
shading buffers and completed RGBA frames through all five display modes,
opaque/translucent display, hover/confirmation, changed view directions, pan,
zoom, tiny rotation, perspective, changed mesh coordinates, empty geometry and
restoration. Common-picker occurrence identity remains checked.

All 30 candidate-packet hashes in the accepted comparison match exactly. All 240
framebuffer hashes and 24 shading-buffer hashes match as well. The non-manifold
fixture remains stable and selects the same container. The native Part is only
read/calculated for test preparation; its source file is never saved.

Localization review: no user-visible text is changed. Benchmark diagnostics and
documentation are English, with the existing five-language catalog checks.
The root `zima-cad.bat` launcher remains unchanged. Linux verification and portable
packaging are outside this change.

## Timing results and limits

Runs were serial on Windows/Fusion at 1200 × 800. Each value below is the mean
of the three nonempty uploads for that fixture: initial, deformed coordinates and
restored geometry. Both original runs are shown because system timing varied.
Hashing candidate values is outside the timed region.

| Fixture | Original run 1 | Original run 2 | Accepted change |
| --- | ---: | ---: | ---: |
| 64 cylindrical occurrences | 12.455 ms | 11.092 ms | 8.391 ms |
| 256 cylindrical occurrences | 44.535 ms | 56.031 ms | 43.601 ms |
| 64 spherical occurrences | 53.733 ms | 62.491 ms | 52.422 ms |
| 256 spherical occurrences | 257.399 ms | 306.915 ms | 238.532 ms |
| User spring `02.prtz` | 53.025 ms | 52.824 ms | 37.778 ms |

On the spring, this stage improves by approximately 29%. Results on the other
fixtures range from small gains to larger differences and are sensitive to run
variation. This is silhouette preparation after a mesh change, not regeneration,
whole-dialog latency or warm repaint speed. The existing camera-direction buffer
reuse remains separate. No overall application percentage is claimed.

Inline records alone did not establish a reliable spring speedup (the compact
record-only trial measured 53.008 ms). The accepted change also removes repeated
face-label copies. The final candidate sequence matches both original runs
exactly, including all empty and replaced-mesh states. The intermediate trial
is documented to distinguish an allocation reduction from a demonstrated timing
gain; it is not the reported final result.

Evidence: [stage measurements and complete rendering checks](20261001-silhouette-pair-storage.txt).

Final clean Windows build succeeded. All six selected contracts passed in
52.99 seconds: viewer, H-Sweep GUI, refresh scope, selection filters, surface
profiles and five-language translation catalogs. Temporary profiling code is
absent from the rebuilt local application.
