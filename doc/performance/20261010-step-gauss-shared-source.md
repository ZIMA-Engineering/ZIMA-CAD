# STEP integration and source-transfer follow-up

## Scope and current status

This work follows the user's approval to repair G92K1 import, share one STEP
read/transfer, measure rotation, and align calculated-model behavior between
Windows and Linux. Native Linux execution is assigned to the user after reboot;
see [the Linux handoff](../LINUX_RELEASE_HANDOFF.md). The dependency patch is
shared by both builds. The source version remains OCCT 8.0.0.

The complete patched Windows SDK is built and deployed. The analytical kernel
matrix and both representative full imports pass. Native Linux execution and
Windows release packaging have separate acceptance gates below.

The user subsequently requested a Drawing angular-dimension text-placement
repair after these four tasks: verify the applicable dimensioning convention,
keep text above the dimension arc both inside and outside the angle, and cover
interactive display and output. Final delivery is documentation, commit/push
and a new verified Windows release on GitHub.

## Reproduced OCCT memory corruption

G92K1 leaf 0, product definition `0:1:1:1`, contains 6,344 faces. One trimming
boundary has 3,388 integration spans. The corresponding diagnostic face was
exported to an ignored 672,059-byte B-Rep file; the source STEP was not modified.

Windows symbols resolve the first invalid write to
`BRepGProp_Gauss::Compute+0x24f2`, source line 999 in OCCT 8.0.0,
`BRepGProp_Gauss.cxx`: `aLI.Ixz = mult(CIxz, lr)`. Running only this face
reproduces the fault independently of the viewer and the complete model.

The integrator initially allocates 1,953 records. `FillIntervalBounds` correctly
enlarges the arrays to the dynamic subdivision budget (108,417 for this
boundary), but the integration code clips its stopping index back to 1,953.
Initial processing already exceeds that index; subsequent subdivision cannot
reach it again and eventually writes past the larger allocation. An analytic
face with exactly 1,953 spans also demonstrates a skipped interval without a
crash. These are kernel defects, not evidence that the STEP file is invalid.

The user explicitly approved removing this historical fixed cap on 2026-10-10.
`tools/dependencies/occt-gauss-large-spans.patch` removes both L and U budget
clips. The stopping index remains the actual allocated dynamic budget; the
requested integration tolerance and convergence checks remain unchanged.

Removing the cap alone revealed a second defect on G92K1: the computed budget
was 2,145 records while the allocated array held 2,113. Trim bounds can include
an additional interval beyond the last knot. Allocation now counts the actual
emitted intervals first. Reused error vectors are cleared over their complete
capacity because `Max()` searches that complete vector, including an old tail.
The initial cap-only SDK candidate is superseded and must not be deployed.

The Windows vcpkg overlay and Linux source-build script apply the same patch.
Both SDKs write `share/opencascade/zima-sdk.json` with its SHA-256. CMake checks
the source version and patch identity before building the application. This
prevents accepting an unpatched SDK merely because its version is 8.0.0.

## Independent analytical regression

`zima_cpp_occt_gauss_interval_contract_tests` builds valid rectangular faces
with 1,952, 1,953 and 1,972 spans, separately in a trimming B-spline and a
surface B-spline. It verifies area, centroid and inertia against analytic
values, then extrudes each face and independently verifies volume, centroid
and every inertia entry. Adaptive tolerance is `1e-12`.

The original SDK fails the 1,953-span area check. An isolated executable built
with the complete patch passes all six cases plus a valid 66-span B-spline
face whose trim extends slightly beyond the last knot. The cap-only patch fails
that additional case with `NCollection_Array1::ChangeValue`. All seven cases also
pass against the actual rebuilt Windows SDK. Both full representative imports
complete without a kernel crash; STEP/native/reference and dependent geometry
contracts pass with that SDK.

An earlier experimental repair retained the cap for smaller initial span
counts. It is superseded by the approved complete removal. Its full G92K1
Gauss result was 1,945,134.4939601296 mm³; an independent Gauss-Kronrod result
was 1,944,861.8688071731 mm³. Their reported errors were approximately `9.77e-9`
and `2.65e-7`, respectively, despite a requested `1e-12`. This discrepancy is
unresolved and is not accepted as proof of physical-property accuracy. The
Gauss-Kronrod call took roughly 20 minutes while an SDK build overlapped it;
that observation is not a controlled benchmark or an accepted speedup.

## One source transfer per import

Previously `inspect_step_parts(path)` read and transferred the STEP into an
XCAF document, discarded it, and `freeze_step_components` repeated that work.
`StepSourceDocument` now owns one explicit read/transfer. Both Part and Assembly
imports inspect and capture from that same source document. The source context
is released immediately after capture, before meshing and physical-property
calculation, to avoid retaining the complete STEP reader during those phases.

This is an operation-scoped resource, not a filename cache. Later imports open
fresh source data. Owner-specific frozen B-Rep, topology identity, occurrence
placement and native persistence continue through their existing paths.

The regression compares complete serialized body packets against separate
read/transfer preparation, captures while an owned test fixture's source path
is temporarily absent, rejects foreign source paths and missing definitions,
and rewrites a fixture at the same filename to verify that a later import sees
the new volume. The complete product STEP matrix passes, including hierarchy,
repeated occurrences, mm/inch conversion, frozen references and Part/Assembly
native save/reopen. G92H1 prepared history is exactly equal to the earlier
accepted packet, SHA-256
`7ea67287da61be2342dda6168698dc806a167242816fbea6b454b0dd573ab9e3`.
Sequential preparation on the final SDK measured 105.360 s for two transfers
and 93.829 s for the shared transfer. These runs also overlapped small regression
workloads, so they are observations, not universal speedup promises. Evidence:
`build/import-followup/g92h-final-{frozen,shared}.log`.

## Rational-volume integration of imported solids

STEP now reuses the existing bounded four-worker face integration path used
for rational Corner FORM surfaces. Each trimmed face has a private adaptor,
domain and Gauss-Kronrod integrator; precision, spans and barycentre are the
same as the sequential OCCT call. Reduction follows the original face order.
The full inertia calculation remains its original adaptive Gauss path.

Origin and translated NURBS cylinders/tori have bit-exact volume equality to
sequential OCCT and bit-exact tensors to the original Gauss calculation. Analytic
volume and centroid are checked independently. Existing Gauss inertia on the
NURBS cylinder differs from its analytic value by about 1.4e-7 relative; the
analytic tensor bound is 1e-6 and is explicitly not a claim of 1e-12 physical
accuracy. Product integration tolerances were not reduced.

Full G92K1 history is byte-identical before/after this parallelization, SHA-256
`fb01a2534df4bc8d74c6a10225cf77508e79d97fa8138c71d83099cd17554514`.
Total import measured 1,246.16 s sequential and 845.018 s parallel. The latter
spent 751.235 s in volume/inertia. Differing overlapping build/test loads prevent
a controlled percentage claim. Exact physical-property integration remains the
main cost on this STEP. No lazy or approximate property substitution is used.
The integration discrepancy above remains an explicitly recorded numerical
limitation; output equality is not evidence that either algorithm is accurate
to its requested tolerance on this particular model.

## Rotation without changing rendered data

Ordinary unhighlighted frames no longer allocate and look up an EdgeKey for
every edge, visit every relation edge when no relation is highlighted, or scan
every confirmed-wire candidate when no wire is selected. All highlight channels
still use the existing path when active. Picking, geometry, line sampling,
silhouettes and depth testing retain their behavior.

The actual G92H1 packet has 308,352 triangles and 57,598 edges. The same camera,
1650 × 1275 framebuffer, 14 actual middle-button movements and 12 measured
frames per mode produced these medians. Measurements include a framebuffer
readback and `glFinish`, so they are not presentation FPS.

| Mode | Before (ms) | After (ms) |
| --- | ---: | ---: |
| Shaded with edges | 31.656 | 17.323 |
| Shaded | 24.515 | 12.894 |
| Wire | 25.351 | 17.307 |
| Hidden edges | 29.626 | 20.306 |
| No hidden edges | 26.247 | 18.546 |

Both executables use the same current libraries apart from the affected viewer
source. Other finite contract work overlapped these measurements. Every final
frame is byte-identical in all five modes and eight additional states: selected
edge, hovered edge, preview owner, relation highlight, Assembly reference edge,
whole-result selection, container inspection and selected edge index. Evidence:
`build/import-followup/rotation-final-{baseline,candidate}.log` and corresponding
13 PNG pairs. Performance is platform/model dependent.

## Imported-face reference lookup and Body placement preview

The user approved an entry-scoped geometry index on 2026-10-10. The index maps
persisted occurrence/owner/semantic identities to source entities. A solver
packet contains every entity matching the submitted references, in the original
order, with exact vertex coordinates, winding, analytic surfaces, splines and
signed values. Only storage indices are remapped. The existing solver equations,
tolerances, branch choices and persisted references are unchanged. Analytic-only
construction packets without mesh storage retain the established complete input.

The candidate filter uses this packet for measurement, feasibility and degrees of
freedom. It keeps the existing prepared-reference cache and scene-revision
invalidation. A Body editor shares one index of its fixed source-reference
snapshot between successive entry rows and placement previews; changing the
pending Body placement cannot mutate that snapshot. Other entry sessions rebuild
the index when the base scene revision changes. The index is disposable memory,
not a required file, native-format field or source of topology identities.

The initial diagnostic G92H1 display packet has 308,337 reference triangles. Its actually
offered face `step:face:#1496349` has 299 triangles. A measured fully oriented
solve took 114.747 ms with the complete input and 0.10626 ms with the selected
packet, with byte-identical serialized placement results. Index construction
took 118.98 ms and selection 0.259 ms. These numbers include overlapping focused
checks and do not predict complete command latency. On the 40-triangle small
fixture, index construction took 0.0044 ms and selection 0.0312 ms; individual
solve measurements are too short for a reliable relative percentage.

The general-surface and exact-curve regression matrices additionally compare
full/selected solving, degrees of freedom, alternative branches and measurements.
Independent existing equations verify cylinder/cone contacts, curved finite
patches, second/third references, impossible constraints, both sides, signed zero,
transformed occurrences, changed sources and persistence. Seven focused native
and GUI suites pass, including the cylindrical placement UI. The actual large
GUI check also exercises the common picker, RMB cycling, exact LMB identity and
cached highlight. Successive GL frames may differ by one 8-bit antialiasing level;
reference identity remains exact. Final cold-picker and complete Body edit
acceptance are recorded with the release after verification.

In the diagnostic-packet GUI, synchronously re-arming row 1 prevents earlier pointer
events from warming the filter. At logical View point `(352,176)`, the original
reference-selection implementation from commit `1676d62b` took 120,058 ms for
the first offered face. The final implementation took 16.1992 ms and its next
20 passes averaged 16.5676 ms. The row-entry transition itself measured 0.0624 ms
before and 0.0566 ms after; the Body index is reused rather than rebuilt there.
The common picker offered the same face identity and the final GUI test passed
RMB cycling, LMB confirmation and cached highlight. Confirmation including the
complete Body preview measured 3,072.72 ms; it is a different operation from
hover. The original probe was stopped after its first result instead of spending
another 20 expensive iterations. Its repeated-hover and confirmation timings are
therefore unverified. This is one measured face/model, not a general speedup
promise. Evidence: `placement-fixed-baseline.log` and
`large-original-placement-fresh-ui.log` under `build/import-followup/`.

That diagnostic native file contains the calculated display packet but has an
empty ImportedStep topology definition. It is valid evidence for the isolated
lookup comparison above, but cannot establish product import, regeneration or
Undo/Redo correctness. Its failed post-regeneration picking run was discarded;
no product topology fallback was added. The GUI test now rejects such a fixture
before running. Complete acceptance instead uses `g92h-product-native.prtz`,
created by the public `import_step_part` path, with 121,405 persisted topology
entries and a successful native save/reopen. Its complete Body matrix passed
in 1,223.125 seconds after the cache traversal change: successive offsets and
rotation, complete preview, Cancel, unchanged OK, changed OK, Undo/Redo, native
save/reopen and exact face confirmation after reopening. This establishes
functional coverage, not acceptable movement latency: changed OK took 86,751 ms,
Undo 3,032.43 ms and Redo 4,543.2 ms. Save timings are reported separately and
must not be described as Assembly insertion timings.

The initial diagnostic-display Extrusion contract on G92H1 passes: three openings measured
2453.46, 3047.72 and 2378.28 ms; Cancel measured 1596.22, 1633.98 and 1511.72 ms.
Each transition publishes one scene and performs no geometry-buffer upload.
Cancel restores the exact model packet, camera, document and Undo/Redo state.
The lookup index does not claim to accelerate OCCT's Boolean cut calculation.

The proper product-native fixture also passed three Extrusion transitions:
opening 3,841.63 / 3,065.84 / 3,120.12 ms and Cancel 1,894.41 / 2,233.60 /
1,842.35 ms. Each publishes one scene with zero geometry-buffer uploads.
The exact packet/camera/transaction checks pass; retained/fresh upload digests
match, with four antialiasing pixels differing by at most one channel level.
These multi-second transitions still leave interaction work to optimize.

## Related changes and remaining acceptance

### Native cache traversal

The product-import Body matrix exceeded its original 1,200-second limit during
native save processing, after preview, Cancel, unchanged OK and changed OK/save
had passed. This timeout does not establish complete Undo/Redo acceptance. A
stack sample located repeated subtree copying/destruction in shared cache
packing. Both packing and unpacking previously copied each complete subtree
before recursively replacing its children. They now build object/array children
directly in the same traversal order. Pool thresholds, equality checks, block
numbering, validation and on-disk encoding remain unchanged.

The regression compares exact stored bytes against the previous copying
traversal, including repeated/distinct body histories, nested objects, threshold
boundaries, UTF-8 and both signs of zero. Malformed references and cycles retain
their rejection checks. Product Part/Assembly persistence, STEP and native
calculation-proof tests are rerun, and the complete large Body matrix is repeated
with a 3,600-second limit and separate confirmation/save/Undo/Redo timings.
Actual product-file before/after measurements and final acceptance follow below.

### Rigid Body movement (work in progress)

The user's target is at most 0.5 seconds for ordinary position changes. A Body
owns already calculated local geometry relative to its Origin. The traced
confirmation path nevertheless read its opaque BRep, transformed/copied it,
serialized the placed solid, read that solid again and serialized the final
Part compound. Local STEP geometry did not need recalculation for that motion.

The pending fix stores lossless ordered rigid placements alongside the opaque
local solid. A Part aggregate names its ordered, self-contained calculated
Body outputs rather than rewriting a compound archive during every move.
Kernel operations and exports materialize this recipe using the existing
transformation convention when they actually require solid geometry. The
reference convergence and dependent Boolean/copy calculation paths remain
active. Native files store all operands and placements; ordinary loading,
previews and picking never materialize the kernel recipe. Assembly persistence
reuses its existing geometry table instead of duplicating compound operands.

Finite native tests compare the materialized result against an independent
eager transform, local archive and reference retention, actual viewer vertices,
physical properties and recipe save/reopen. GUI latency, dependency tests,
factory-template regeneration and final acceptance are pending. No claim that
the 0.5-second target has been achieved follows from the current implementation.

The cross-platform calculation proof is described in
[calculated-model parity](../CALCULATED_MODEL_PARITY.md); Drawing angular text is
covered in [angular-text clearance](../DRAWING_ANGULAR_TEXT.md). New digest errors
are translated into all five supported languages. The approved reference lookup
changes introduce no UI text. Placement equations and meaningful geometry-side
identities are unchanged.

Native Linux execution belongs to the agreed reboot session, with exact SDK,
fixture and test commands in [the handoff](../LINUX_RELEASE_HANDOFF.md). The
G92K1 Gauss/Gauss-Kronrod numerical discrepancy remains unresolved; no universal
performance or physical-accuracy claim follows from these finite checks.
Windows publication requires the committed-source, dependency, archive,
signature and extracted-runtime checks recorded in the release notes.


### Rigid Body follow-up: cold reuse and immutable data (in progress)

On the complete product-native G92H1-T fixture, an independent preflight
resolution changed the imported feature's resolved pitch from `+0.0` to `-0.0`.
The local operation fingerprint changed from `f2737b74e49dc41c` to
`3d48ccaa9722ba9c`, although the complete local archive, vertices, triangle
order, face references, volume and area remained equal. A cold settling pass
therefore rebuilt the import and consumed about 47 seconds.

Only the six resolved STEP rigid-transform coordinates now hash both numeric
zeros alike. This does not rewrite placement data or normalize authored
reference offsets, directed dimensions, contact sides or other request fields.
Six cold-kernel tests reuse an exact calculated packet after deleting the STEP
source; each also checks retained negative-zero input bits and rejection of a
changed nonzero transform key. These tests and the ordinary import-carrier
comparison (unreferenced/referenced, translated/rotated/signed-zero cases) pass.

Separate Body inputs and outputs now share one immutable `BodySnapshot`. The
import-only carrier avoids copying and transforming a full reference packet
when its numeric placement has no geometry consumers. A subsequent native
public Body-command run measured 4.287, 3.967, 4.069 and 4.068 seconds for offsets
3, 7, -2 and 0, respectively, and verified the complete unchanged local geometry.
This removes the cold rebuild but still misses the requested 0.5-second target.
These are command timings without GUI publication or file saving.

Immutable opaque BRep archive bytes now share storage through calculated
history and Undo snapshots. Native JSON still contains the complete archive as
an ordinary string; this introduces no required sidecar. Eighteen dependent
native suites passed (100.30 seconds total), covering imports, persistence,
Assembly, Boolean operations, linked/derived Bodies and reference queries.
With the dialog-equivalent resolved input prepared before the command timer,
the proper G92H1-T public Body command measured 2.071, 1.764, 1.794 and 1.708
seconds for offsets 3, 7, -2 and 0. Every complete local packet remained equal.
These timings exclude viewer publication and file saving.

The corresponding complete GUI previews measured 2.672, 2.696, 2.734 and 2.729
seconds; preview Cancel took 3.204 seconds. Profiling identified repeated scene
preparation, GPU triangle preparation and silhouette adjacency rebuilding.
Sharing archives alone therefore does not meet the interaction target.

The next measured stage shares immutable topology arrays, archive bindings and
calculated local Body histories. Unchanged pure import histories retain every
matching prefix packet; requests for live original faces or solid operands
still use the established evaluator. Four focused native suites passed,
including repeated translated/rotated import movement after deleting the source
STEP and exact unchanged local geometry. A scoped analytic-surface transform
cache is also being verified: it reuses only the same retained surface pointer
within one invocation with identical transform inputs. It preserves all scalar
equations, orientation and side metadata. Full dependent rebuilds, GUI timings,
Undo/Redo, native round-trip, factory templates and renderer comparisons remain
pending for this stage. The requested 0.5-second target is not yet achieved.

### Bounded reference preparation during rigid confirmation (in progress)

For documents containing only independent numeric STEP features, the explicit
reference-settling pass now selects every referenced Body face/edge/point/axis
from the current calculated packet before making a source copy. Other feature
histories retain their full reference preparation. The last resolved branch no
longer assembles a reference packet that has no subsequent consumer. The shared
solver, dependency discovery, convergence and persisted placement are retained.

The requested-reference index stores only the requested exact identities while
still traversing the source in its original order. Both index variants produce
identical serialized reference packets in the curve and surface matrices.
Seven focused native suites passed (4.05 seconds total). The native Body-command
matrix also compares full reference convergence and complete calculated packets
for two imported Bodies, two source orientations, both Flip states and `+0`,
`-0`, `+2` and `-2` offsets. A preliminary single-pass comparison was invalid
because its still-attached source Body moved during settling; the corrected
comparison explicitly settles both paths and retains the exact side values.

On G92H1-T, native confirmation measured 0.752, 0.728, 0.670 and 0.645 seconds
for offsets 3, 7, -2 and 0, excluding GUI publication and saving. Reference
preparation fell to 24–28 ms and reference resolution to about 0.2 ms.
Inspection then found another unused full geometry preparation for external
Sketch refresh even when the document contained no Sketches. That path now
detects native, embedded and Section Sketch consumers before preparing their
source. It preserves retirement of stale reference errors. The Body-command
suite passed, including stale-error retirement and unchanged-state checks.
The corresponding finite native confirmation timings were 0.584, 0.593,
0.549 and 0.523 seconds; the complete local geometry remained equal.

These command measurements are not GUI interaction latency. The complete
immutable-topology GUI preview measured 2.997, 2.829, 2.676 and 2.659 seconds;
Cancel took 2.813 seconds. That run preceded bounded reference preparation,
which affects explicit confirmation rather than the numeric preview path.
GPU adjacency and complete scene preparation remain measured work. Final
dependent builds, rendering comparisons and the full persistence/Undo/Redo
GUI matrix are still required before Windows publication.

### Dependent contracts and controlled renderer comparison

The first expanded native run executed 47 suites in 799.63 seconds: 40 passed
and seven failed. Four checks still read the root archive directly as BRep,
although a calculated Body may now retain ordered placement/compound operands.
Those test readers now use the explicit OCCT materialization interface; their
independent topology, section, curvature, volume and inertia checks remain.
The FORM geometry, copy matrix and copy-after-Unbend checks subsequently passed
in 20.76, 167.22 and 23.76 seconds.

Three other failures were reproduced from clean committed source `1676d62b`
with the same Windows SDK. New valid definitions already retain a calculation
error under the established authoring policy; the old continuity/hole checks
expected an exception instead. They now require the explicit native error,
unchanged complete input vertices/triangles/face identities and properties,
Undo/Redo, and native save/reopen. The failed-Fillet Undo check now independently
serializes the expected document with its legitimately retained dimension
registry, including a freshly calculated definition proof. It no longer changes
the registry in a JSON copy while leaving that copy's proof stale. These three
suites passed in 129.13 seconds total. No production failure policy was changed.

One existing gap remains: `zima_cpp_solid_state_spatial_sweep_document_tests`
does not converge during reference regeneration. The same failure occurs on
clean `1676d62b` after the independent nine-section geometric check passes.
Observed maximum perimeter error is 0.0000575935 mm, side error 0.0000158442 mm,
right-angle cosine 0.0000202591 and edge bow 0.0000000000482931 mm, within that
fixture's established approximation budget. This is a real unverified lifecycle
variant, not evidence that all solid-state regeneration passes. No tolerance,
convergence validation or geometry equation was relaxed to hide it.

The first adjacency lookup optimization matches all 200 controlled synthetic
frame/hash records and thirteen actual product-native G92H1-T PNGs against
clean `1676d62b`: five display modes and eight highlight states. Baseline and
candidate use the same Qt platform plugin and Fusion style. The initial frozen
rotation helper contained only five mode cases; its incomplete thirteen-image
assertion was discarded. A complete helper was built against clean committed
source for the accepted comparison. These are Windows framebuffer comparisons,
not cross-platform golden images or serial performance measurements.

Evidence is in `body-rigid-final-dependent-contracts.log`,
`body-shape-test-reader-contracts.log`, `body-state-error-contracts.log`,
`continuity-baseline-{test,fillet,hole,spatial}.log` and
`adjacency-rendering-comparison.json` under `build/import-followup/`.

### Rounded-coordinate adjacency addresses

The next viewer-only stage rounds each mesh vertex once with the existing
`round(value * 1e7) / 1e7` equation. Equal rounded coordinates share an
operation-local address in triangle/edge adjacency lookups. Candidate
publication still sorts by the original face string and rounded coordinate
pairs; triangle encounter order, first two normals and nonmanifold exclusion
remain unchanged. The address table is rebuilt for the actual upload inputs;
there is no persistent cache or reduced precision.

The complete proper G92H1-T GUI preview passed for offsets 3, 7, -2 and 0 and a
subsequent 15-degree rotation, with all expected vertices and triangle order.
Observed preview times were 2.935, 2.717, 2.639 and 2.665 seconds; Cancel was
2.808 seconds. These samples show only a small difference from the preceding
stage, not achievement of the requested 0.5-second interaction target. The
complete synthetic and product-native renderer comparison passed: all 200
synthetic frame/hash records and thirteen actual G92H1-T images match the
committed baseline exactly. Evidence is in `product-point-index-preview-profile.log`,
`adjacency-point-rendering-comparison.json` and `adjacency-point-acceptance-run.log`.

### Serial native cache comparison and final GUI checks (in progress)

The frozen original and optimized cache executables were run serially, with no
concurrent build or GUI workload, against the same proper product-native file.
This isolates the lossless cache traversal change before later rigid-operand
representation changes. Loading measured 24,748.3 versus 20,997.6 ms; saving
measured 132,041 versus 70,629.6 ms. Complete process times were 157.735 and
92.640 seconds. Both saved files contain exactly 618,644,457 bytes with SHA-256
`26d7e648f7a3d184533db6fe992e921faac147443f8976a231b68c3b09dc5a43`.
These are one ordered pair of samples, including filesystem-cache effects;
they are not a universal timing ratio or measurements of STEP transfer.
Evidence is in `product-cache-comparison.json` and
`product-cache-{baseline,candidate}.log`.

Nine final focused native/GUI suites passed in 80.84 seconds, including actual
factory New Part/Assembly creation, all five live translation catalogs, curved
placement and reference assignment/removal. The current native factory templates
were rewritten and reopened with the final serializer before that run.
The initial final large-model run confirmed changed Body OK in 2,352.25 ms,
then failed while saving. The disk had only 390,434,816 free bytes and retained
a 595,444,669-byte temporary document beside the intact 618,644,457-byte
original. Repetition with free space confirmed this is the complete moved-file
size, not evidence of a partial serialization. The established archive step
copies the existing document before replacement and needs additional free space.
The generic harness failure did not retain the original console error; the
harness now prints the command, error code and message before rejecting it.
This run is not accepted as proof of changed-placement persistence.

Cleanup removed 80 explicitly enumerated generated native test files totaling
21,175,129,260 bytes, preserving the proper source fixture, test logs and images.
Four obsolete Ninja/CMake build directories under `C:/zcb/{0904,0906,101,102}/b`
were then removed after checking their generated-build layout and source paths.
Published/signed archives, source directories, projects, configuration, current
SDK and development build were retained. Free space reached 33,237,024,768 bytes.
The broader whole-stage deletion was rejected by automatic approval review;
only the independently verified build subdirectories were removed instead.
The complete large-model Body/Extrusion/hover checks are repeated with sufficient
space; record their final result separately from the focused checks.
