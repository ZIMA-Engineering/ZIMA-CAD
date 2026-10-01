# Body context transfer and Properties closing — 2026-10-01

## Scope

Inputs are persisted calculated Body meshes and the current visible Body/history
context. The required output is the same ordered viewer and reference packet,
with unchanged cached geometry, history, visibility, placement and model accuracy.
This follow-up targets the full-scene restoration after closing Properties.

The closing handler already publishes the scene once. Temporary instrumentation
on the user's `Projects/02.prtz` spring found approximately 40–50 ms assembling
the Body context and 17–21 ms copying it again for display. That instrumentation
was removed after diagnosis. No OCCT calculation or precision change is involved.

## Accepted changes

`DocumentSession::body_context_mesh()` still obtains the same calculated boundary
and performs the same owner filtering and Body placement. Its disposable local
mesh is now moved into placement, and its arrays are transferred into the result
instead of deeply copying strings, sampled curves and reference records again.
Cached Body outputs are still copied before modification; they are never moved
from. Subsequent Bodies retain their order and the same triangle index offsets.
The exact set of aggregated fields remains unchanged.

The ordinary Part scene consumes that locally owned result by move after its
source consumers have finished. Cosmetic Hole thread edges are prepared against
the same unmodified supporting mesh first, then appended in the same order.
Borrowed session boundaries are still copied. No display data is retained across
action boundaries, so there is no new cache or invalidation policy.

## Verification

The multibody contract now compares repeated complete serialized display/reference
packets and confirms that context assembly leaves the calculated cache unchanged.
Existing checks cover active/passive Bodies, placement, history cursor, rollback,
Undo/Redo, suppression and visible reference owners.

All 11 selected Windows contracts passed in 89.76 s: multibody, Body commands,
history recovery, 2D Sweep GUI, Helical Sweep GUI, Holes GUI, Assembly refresh,
refresh scope, selection filters, surface-profile GUI and five-language catalogs.
The affected native test executables were relinked against the changed core.

Native-document GUI probes use the user's spring and the existing 2D/3D Sweep
fixtures, with three unchanged OK and three Cancel operations. They check exact
restored viewer-packet serialization, camera, document state and Undo/Redo
availability, plus repeated preview framebuffer hashes. The spring's restored
framebuffer hashes vary even within baseline runs; they are not used as evidence
of pixel-identical restoration. Exact packet restoration is checked separately.

Localization review: no product UI text changes. New diagnostics are test-only;
all documentation is English. Native formats, shared placement contracts and
`zima-cad.bat` are unchanged. No Linux verification or portable release is included.

## Timing results and limits

Baseline is `d7966104`. Both GUI executables were built locally against the same
Qt/OCCT runtime and run serially from the same directory in baseline/candidate/
candidate/baseline order. Each run performs six opens/closes; the table excludes
trial 0, leaving ten warm samples per version and fixture. The spring has 13,370
viewer vertices. Timings include GUI event processing, not only mesh assembly.

| Fixture | Baseline close | Candidate close | Baseline open | Candidate open |
| --- | ---: | ---: | ---: | ---: |
| User Helical spring | 279.532 ms | 245.470 ms | 159.919 ms | 174.522 ms |
| Small 2D Sweep | 28.736 ms | 29.385 ms | 47.216 ms | 47.615 ms |
| Small 3D Sweep | 11.426 ms | 10.698 ms | 64.865 ms | 61.803 ms |

The paired spring closing mean improves by 12.2%. Its complete open/close cycle
improves by approximately 4.4%; opening alone was slower in this sample and is
not claimed as an improvement. The small controls provide no convincing overall
speedup. Earlier separate runs varied substantially, which is why the alternating
comparison is reported instead of selecting the fastest run.

An isolated native probe of `body_context_mesh()` on the same spring measured
55.258 ms before and 25.544 ms after (seven warm samples, excluding trial 0).
This isolates the removed copying from rendering and window events. It does not
mean that total closing becomes twice as fast. The probe is available in
`zima_cpp_multibody_contract_tests` with the native-document path in
`ZIMA_PROFILE_BODY_CONTEXT_DOCUMENT`; serialization/equality checks run outside
the timed region.

All 72 preview hashes in the alternating GUI comparison match their corresponding
fixture baseline. Every run passed packet, camera and no-op state checks. Raw
measurements and regression results: [logs](20261001-body-context-transfer.txt).
The remaining closing cost includes scene preparation and redraw; no GPU or
rendering-precision changes were made in this pass.
