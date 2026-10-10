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

## Related changes and remaining acceptance

The cross-platform calculation proof is described in
[calculated-model parity](../CALCULATED_MODEL_PARITY.md); Drawing angular text is
covered in [angular-text clearance](../DRAWING_ANGULAR_TEXT.md). New digest errors
are translated into all five supported languages. Shared placement code and
meaningful geometry-side identities are not changed.

Native Linux execution belongs to the agreed reboot session, with exact SDK,
fixture and test commands in [the handoff](../LINUX_RELEASE_HANDOFF.md). The
G92K1 Gauss/Gauss-Kronrod numerical discrepancy remains unresolved; no universal
performance or physical-accuracy claim follows from these finite checks.
Windows publication requires the committed-source, dependency, archive,
signature and extracted-runtime checks recorded in the release notes.
