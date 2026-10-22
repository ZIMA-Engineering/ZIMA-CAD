# Fillet on connected segments of one original curve

Post-release correction for `Projects/2000.prtz`, `Vytažení 003`.
Windows: Qt 6.11.0 / OCCT 8.0.0. Signed Windows 2026102201 remains immutable;
this correction belongs to subsequent development. Linux execution is unverified.

## Cause and preserved contract

Two input curve keys each resolve to two geometric segments separated at a
periodic seam. Their persisted endpoint graphs have degrees 1, 1, 2. Workspace
validation rejected every key with multiple segments before reaching OCCT,
although the existing kernel already supports multiple shape uses of one
original curve. Both reported keys failed R = 0.5, 1 and 2 mm this way.

The shared classifier now proves one connected, unbranched chain from persisted
vertex identities. It rejects disconnected/branching groups, incomplete split
endpoints, inconsistent copies of a vertex, repeated geometry and foreign curve
identities. The 0.000001 mm coordinate check validates copies of the same vertex;
it never joins different IDs by proximity. Standalone circles without persisted
ends retain their existing incomplete-endpoint state.

Queries, tangent-route restoration and commit validation use the same classifier.
Routes contain each original curve identity once, including when multiple runtime
segments are selected. Internal seam vertices do not become free R1 endpoints.
No OCCT index, runtime array order or screen position creates a persistent ID.
Distinct original side keys remain distinct route members. No geometry kernel
algorithm, native format, original source identity or factory asset is changed.

Ordinary single-segment validation retains its inexpensive path. Hover reads
saved viewer data; its existing route cache changes only with the offered seed.
The implementation adds no OCCT calculation during hover, picking or opening.
No product-visible strings changed; existing five-language labels/errors remain.

## Verification

| Matrix | Checks | Result |
| --- | --- | --- |
| Shared chain/traversal | Both segment orders/directions and either live seed; one deduplicated identity; closed curves; disconnected, branching, coincident-but-distinct IDs, inconsistent/duplicate geometry, foreign occurrences and incomplete endpoints | Passed |
| Native Fillet/Chamfer commands | Analytical box/cylinder volumes, constant/variable radii, explicit R1, opposite ends, locks, impossible edits, source changes, native persistence and transactions | Passed |
| Query contract | Input boundary, exact original endpoints, pagination, Bodies, rollback, circle unknown-endpoint state and unchanged revision/cache | Passed |
| Existing GUI | Five Fillet/Chamfer modes, all annotation grips, value editing, creation/editing, Cancel/OK, Escape, Undo/Redo and direct View inspection | Passed, 58.36 s |
| Actual user geometry | Six keys × R = 0.5/1/2 mm; formerly rejected curves reach valid exact calculation | 18 commits passed |
| Repeated real-model edits | Either reported seed selects the same two-member closed tangent route; R = 0.5/1/2/0.75, actual volume changes, locks, atomic invalid/closed-variable rejection, no-op set, Undo/Redo, save/reopen and explicit regeneration | Passed |
| Dependent Chamfer behavior | Both reported seeds × equal-distance/two-distance/distance-angle; create, changed size with FLIP, unchanged original route identities and Undo | Passed, six creations and six edits |
| Actual mouse on model copy | Hover/LMB, deduplicated original route, create R = 1, Cancel R = 1.5, commit R = 1.5, measured volume change, Undo/Redo and unchanged OK | Passed, 4.388 s |
| Calculation counts | Profiled actual mouse scenario: exactly two Fillet builds, for creation and the committed radius edit; none added by hover, draft changes, Cancel or unchanged OK | Passed |

Original model volume: 418924.36326914246 mm3. Either seed produces
418893.5304226121 mm3 at R1 and 418788.5533751532 mm3 at R2. Both edited copies
retain their original Sketch definitions and route identities; reopened and
regenerated volumes agree within 0.000001 mm3. These values are measured evidence,
not an independent analytic formula for the curved blend; separate ordinary
box/cylinder regressions check analytical geometry.

Evidence: `build/connected-fillet-chain-final-build.log`,
`build/connected-fillet-chain-final-native.log`,
`build/connected-fillet-chain-final-gui.log`,
`build/2000-reference-fillet-20261022/native-edit-results.json`,
`connected-gui-verified/result.json` and `connected-gui-verified/result.log`.
The dependent Chamfer results are in `connected-chamfer-probe/results.json`.
The original and isolated input remain byte-identical:
`1fe7f4a358c54f87604b8e1073f86bed4a9c99bb9c1db06b0d492527014db72f`.

## Baseline failures and remaining limits

The later [symmetric Feature endpoint correction](20261022-symmetric-feature-endpoints.md)
addresses the separately discovered cache issue and its dependent multi-edge
variable Fillet law. The baseline/fixture investigation below remains historical
evidence; it is not a claim that changing the fixture itself repaired production.

The first related run passed only the traversal test. Two older analytical
box tests expected 12 edges and two longitudinal endpoints from a symmetric
two-side Feature. Both the unchanged release and modified implementation expose
16 logical-side edges and missing longitudinal endpoint caches in that fixture.
The old GUI test then indexed the empty endpoint list and crashed; the identical
crash was reproduced using immutable source a49735ec's packaged runtime.

The analytical box fixtures now explicitly author a centered one-sided extrusion,
matching their intended 12-edge reference geometry. Other fixture defaults and
actual product symmetry are unchanged. The GUI test checks endpoint availability
before indexing. Three native suites pass in 1.88 s and the complete GUI suite
passes. This fixture correction does NOT repair or certify symmetric longitudinal
endpoint ancestry; that pre-existing native-cache issue remains a separate task.
Baseline evidence: `symmetric-box-baseline.json` and
`baseline-edge-gui/result.json` under the investigation directory.

Disconnected curves sharing a key remain ambiguous. Closed tangent contours do
not acquire an arbitrary R1 for variable-radius Fillet. Approximate wire preview
for general curved blends is unchanged and is not equivalent to the exact OK
result. This finite model matrix does not certify every spline blend or every
Assembly occurrence. Native Linux build and GUI checks remain outstanding.
