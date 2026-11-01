# Exact tangent routes and planar Fillet experiment

## Scope

This investigation replaced the authored angular threshold used by automatic
Fillet/Chamfer route selection and evaluated a narrow custom Fillet builder on
the representative skeleton:

`build/performance/ZE0176-0101-0000_SKELETON-boundary-v2.prtz`

Measurements used the Windows Release build with OCCT 8.0.1. Linux compilation
and runtime verification remain outstanding. The custom builder is experimental
and disabled during ordinary product execution.

## Exact route selection

Automatic continuation now requires all of the following:

- the same persisted endpoint identity;
- exactly two incident topological edges at the endpoint;
- exact native curve endpoint derivatives that are antiparallel within a
  floating-point equality allowance.

Display chords no longer decide continuation. A real corner stops traversal,
including a measured 0.5-degree fixture. A T-junction also stops traversal even
when two of its edges are tangent. Missing exact curve data never falls back to
the display polyline.

The public command/query schema no longer accepts `tolerance_degrees`. The
obsolete localized tolerance label was removed from all five catalogs (`cs`,
`en`, `de`, `fr`, and `ru`). No new user-visible text was introduced.

## Support classification on the skeleton

The new read-only support inspector decodes an already calculated native Body,
resolves selected stable edge references, and records exact curve and adjacent
surface kinds. It neither recalculates nor changes the document.

The first two Fillets each contain 16 separate straight plane/plane routes. The
first support angle is 90 degrees and the second is 60 degrees. Later Fillets
contain mixed line/ellipse/circle edges and plane/cylinder/BSpline supports, so
they remain outside the first custom-builder scope.

## Custom planar builder

The experiment constructs the exact circular removal section for a straight,
convex plane/plane edge, sweeps a cutter, and performs one OCCT Cut for all
requested disjoint edges. The ordinary OCCT Fillet remains the fallback.

Focused fixtures verify:

- one 90-degree box edge;
- four disjoint 90-degree edges in one calculation;
- one 60-degree prism edge;
- exact cylindrical result faces and edge-to-face ancestry;
- rejection of duplicate, adjacent, curved, and non-plane/plane selections.

The skeleton revealed a terminal case not represented by the initial box: every
first-Fillet route ends at one three-edge vertex and one four-edge vertex. The
initial cutter ended at the selected topological interval and left one planar cap
per edge. OCCT continues the same cylinder until the fourth incident face trims
it along an oblique curve. Extending the cutter by one radius lets the input Body
own that terminal trim and removes the artificial cap.

After that correction, each of the 16 edges and the complete first Fillet have
zero bidirectional Boolean volume difference from OCCT. The complete result has
56 faces in both paths and the same 296 stable reference identities.

## Downstream equivalence

The experimental builder was enabled only for the first two supported Fillets.
All later operations used their unchanged production implementation.

| Prefix result | OCCT/custom faces | OCCT/custom reference identities | Bidirectional volume difference |
| --- | ---: | ---: | ---: |
| First Fillet | 56 / 56 | 296 / 296 | 0 / 0 mm3 |
| Second Fillet | 72 / 72 | 392 / 392 | 0 / 0 mm3 |
| Third Fillet | 104 / 104 | 456 / 456 | 0 / 0 mm3 |
| Fourth Fillet | 168 / 168 | 744 / 744 | 0 / 0 mm3 |
| Shell | 332 / 332 | 1548 / 1548 | 0 / 0 mm3 |

This proves that the supported custom geometry can feed the current later OCCT
Fillets and Shell without changing their calculated Body or reference set. It
does not yet prove that each native reference is persisted by a future primary
ZIMA boundary representation; that requires per-reference geometric binding,
save/reopen, Undo/Redo, and parameter-edit verification.

## Rounded-face boundary presentation

The rounded-face wire was also checked independently from material equivalence.
One exact topological edge produces one `ViewerEdge` with one stable reference.
Picking tests every chord stored in that `ViewerEdge`, but returns the same edge
candidate for every chord. The GPU upload duplicates adjacent endpoints and
uses `GL_LINES` so the complete ordinary wire can be submitted in one batch;
this is a display tessellation, not a split into separately selectable or
persisted edges. Consecutive line primitives share the identical converted
endpoint, so the batching path does not introduce a geometric gap.

The first skeleton Fillet has 144 exact and visible result edges in both the
OCCT and custom paths. Its 16 rounded faces contribute 80 exact boundary edges
and those edges are displayed with 320 chords. Five exact boundary edges per
rounded face are expected in this fixture because the four-face terminal trims
the cylinder into an additional exact boundary. The second Fillet has 192 exact
and visible result edges in both paths; its rounded-face wire contains 64 exact
edges and 256 display chords. Every visible stable edge has exactly one Viewer
fragment in both paths.

The same comparison was continued through the later Fillets and Shell:

| Prefix result | Exact edges OCCT/custom | Visible edges OCCT/custom | Visible chords OCCT/custom | Maximum Viewer fragments per stable edge |
| --- | ---: | ---: | ---: | ---: |
| First Fillet | 144 / 144 | 144 / 144 | 384 / 384 | 1 / 1 |
| Second Fillet | 192 / 192 | 192 / 192 | 560 / 560 | 1 / 1 |
| Third Fillet | 224 / 224 | 224 / 224 | 764 / 764 | 1 / 1 |
| Fourth Fillet | 384 / 384 | 384 / 384 | 1,352 / 1,352 | 1 / 1 |
| Shell | 768 / 768 | 768 / 768 | 2,632 / 2,632 | 1 / 1 |

The focused custom-builder test additionally requires an ordinary planar
rounded face to retain four exact boundaries: two lines and two circular arcs.
It also requires the complete custom result to have the same edge and vertex
counts as the OCCT reference. No display or topology defect was found, so the
high-throughput wire batch was not replaced with one draw call per edge.

## Performance result

The custom solid-Boolean implementation is not a production speedup.

| Measured calculation | Production OCCT | Experimental custom |
| --- | ---: | ---: |
| Prefix through first Fillet | about 131 ms | about 224 ms |
| Prefix through second Fillet | about 204 ms | about 486 ms |
| Prefix through Shell | about 2536 ms | about 2803 ms |

The production first-Fillet OCCT build itself takes approximately 13-18 ms. The
custom 16-cutter Boolean takes approximately 80-100 ms before ordinary topology
publication. Therefore replacing this already inexpensive OCCT Fillet with a
solid Cut would regress performance.

Naively merging both Fillets into one barrier saved time relative to two custom
Cuts (approximately 490 to 369 ms), but remained slower than the two production
Fillets (approximately 200 ms). The merged result also contained 88 faces instead
of the canonical sequential 72, despite zero volume difference. A larger Boolean
is therefore not the intended barrier-removal architecture.

## Primary-region treatment experiment

The first two additive skeleton groups and their first two Fillets were also
evaluated as two independent primary regions. Calculating both regions in
parallel and fusing their already rounded results took about 184 ms in one
measured run, essentially the same as the approximately 184 ms serial reference.
That naive composition was geometrically wrong: the second positive region
filled material removed by the first region's Fillet. It had 80.0692 mm3 excess
material and 88 rather than 72 faces.

Therefore a primary Fillet cannot publish only its positive rounded boundary.
It must also publish its exact treatment domain with its history precedence.
The custom planar builder now exposes those derived in-memory removal domains.
It also exposes a TopoDS-independent analytic definition containing the source
interval, sweep axis, both inward support directions, tangent points, cylinder
center, radius and terminal extension. These values are not persisted and do
not change the native format.

Applying each exact treatment domain across the other overlapping primary
region removed the material error and restored 72 faces. That per-region OCCT
materialization retained 16 extra split edges and vertices, however. Treating
those fragments as separate durable identities would be incorrect.

Fusing the two base primary regions and materializing all exact treatment
domains once produced complete equivalence with the serial reference:

| Property | Serial reference | Primary regions plus treatment domains |
| --- | ---: | ---: |
| Bidirectional material difference | 0 / 0 mm3 | 0 / 0 mm3 |
| Faces | 72 | 72 |
| Edges | 192 | 192 |
| Vertices | 128 | 128 |

The measured prototype took about 434-449 ms, including about 279-293 ms in the
final multi-tool OCCT Cut. This is a correctness oracle, not the target execution
path. The primary ZIMA boundary graph must apply the analytic treatment domains
directly to affected patches and retain one semantic edge across materialization
fragments. Removing that final whole-body Cut is the remaining performance
objective.

An experimental signed boundary compositor now performs that direct patch
operation for the same two primary groups. It first constructs the positive Add
boundary, intersects only source faces, splits the affected faces, classifies
both sides of every patch against the signed regions, and sews the retained
patches. It does not invoke a whole-body Fuse or Cut for the final composition.

The skeleton exposed a tangential case that ordinary transverse face/face
intersection omitted: a Fillet cylinder touches an exterior plane along the
exact line that opens the trimmed face. The graph now constructs that
plane/cylinder tangent line analytically, projects exact PCurves to both
supports, trims it against both authored faces and uses it to split the plane.
Without this line the compositor retained a 10 x 30 mm internal rectangle and
reported four free edges. The corrected result is closed and matches the
reference exactly.

One Windows Release measurement of all 32 removal domains produced:

| Property | Serial OCCT reference | Signed primary boundary |
| --- | ---: | ---: |
| Bidirectional material difference | 0 / 0 mm3 | 0 / 0 mm3 |
| Faces | 72 | 72 |
| Edges | 192 | 192 |
| Vertices | 128 | 128 |
| Complete measured prefix | 246 ms | 840 ms |

The signed path spent approximately 153 ms calculating the independent source
regions, 39 ms constructing their positive boundary and 648 ms applying the
negative domains. The latter contained about 81 ms of intersection-graph work,
297 ms of local face splitting, 193 ms of signed patch classification, 42 ms of
sewing and 21 ms of identity-protected unification. This is slower than both the
serial reference and the 482 ms whole-Cut oracle, but it establishes an exact
non-Boolean boundary route whose topology ownership can be moved into ZIMA.
Those timings identify face splitting and classification as the next avoidable
costs; they are not a reason to discard the primary topology architecture.

Treatment scheduling must follow the real parent graph. A later Fillet whose
selected edge or radius is defined on geometry created by an earlier Fillet
cannot be evaluated in the same unordered batch. The earlier treatment must
publish its named faces and edges first. Only treatments on independent graph
branches may run in parallel before their signed boundaries are composed. The
exact 72/192/128 result above covers the first two independent skeleton Fillets;
the later Fillets that consume generated rounded geometry remain a separate
required test.

The signed result also has a calculation-only canonical naming pass. Unchanged
source objects inherit their authored identity; split and intersection objects
derive a child identity from parent faces, edges, vertices, exact intersection
events and their current interval. Generated keys use the separate
`boundary-composition:signed:` namespace, so they cannot collide with ordinary
`boundary-union:add:` children. Focused tests require deterministic identities
after repeated naming and reversed region traversal. Persisting these primary
identities remains a future native-format change and is not claimed here.

The architectural rule generalizes to Chamfer and Shell: a primary operation
publishes its exact positive boundary edit plus its signed material domain, and
that domain is composed in history order across every overlapping primary
region. A difference between the pre-treatment and post-treatment source body
is insufficient because it truncates the domain at the source body and cannot
remove material contributed by another overlapping region.

## Production gate and next architecture

The exact route-selection correction is suitable for production after its
remaining platform verification. The custom planar builder stays disabled.

The primary ZIMA boundary system remains the target even when one isolated
operation does not beat OCCT, because it owns durable identity, composition and
future kernel independence. Timings remain mandatory: acceptance compares the
complete parameter-edit and final-result workflow and rejects avoidable large
regressions rather than requiring every local microbenchmark to be faster.

The next performance experiment must represent Fillet/Chamfer as a primary ZIMA
boundary edit: trimmed parent faces, exact treatment surfaces and semantically
named longitudinal and terminal topology. OCCT B-Rep then becomes a disposable
materialization for operations that still require it, rather than the owner of
identity or a required intermediate cache. The experiment must avoid repeated
solid Booleans and compare complete skeleton timing, canonical topology,
per-reference geometry, Undo/Redo, save/reopen, and downstream Shell behavior.

Native Shell is the planned follow-on primary-boundary operation. It must offset
the primary ZIMA faces directly, preserve outer/inner side intent and inherited
face/edge identities, and support the command's existing ability to treat
multiple disconnected objects in one history step. Until those contracts are
implemented and verified, OCCT Shell remains an explicit materialization
barrier. Leaving it as a permanent barrier would reintroduce a full expensive
Body operation after otherwise native Fillet and Chamfer edits.

The preferred experiment applies Shell directly to the authoritative primary
bodies or regions before constructing one fused OCCT solid. Shared walls,
overlaps and junctions are then reconciled once in the primary boundary graph.
This route is acceptable only when it remains equivalent to the current Shell
for common-wall removal, open-face selection, thickness side, multi-region
results, reference inheritance and subsequent operations; independent offsets
must not leave doubled or overlapping walls at a region boundary.

Persisting that primary boundary as the authoritative `.prtz` representation is
a native-format change. It requires separate user approval, regenerated start
templates and affected factory library assets, and Windows/Linux verification.

## Dependency waves and direct-Add rejection (2026-11-02)

The skeleton treatment graph is not one unordered Fillet batch. Exact persisted
reference dependencies split the tested operations into these waves:

- phase 0, wave 0: operations 2 and 3;
- phase 0, wave 1: operations 4 and 5;
- Shell at operation 6 is a materialization barrier;
- phase 1, wave 0: operations 24 and 25.

The wave calculation follows indirect paths through operations that are not
themselves selected. It rejects duplicate history order, absent selected IDs
and dependency cycles. This is the required scheduler input; angle or feature
position is not a substitute for the authored parent graph.

A direct Add experiment reduced the 26-operation calculation from approximately
`25.74 s` to `7.74 s`, but the result was invalid: `872` rather than `1000`
faces and a nonzero bidirectional material difference. The first divergence is
operation 4. After the two independent first-wave Fillets, 32 of its 48 stored
edge references remain exact, while 16 refer to Add-intersection identities
whose current descendants were republished only as Fillet-generated edges.
Geometric nearest-edge remapping was measured and rejected; it changed the
selected contour and material.

The experimental direct evaluator now treats any partially resolved authored
selection as a hard incompatibility and falls back to the established serial
calculation. It never applies a Fillet or Chamfer to a surviving subset. The
fallback is disabled by default together with the direct evaluator. Measured
prefixes 5 and 6 then reproduce the exact reference calculation, respectively
`104/224/128` and `168/384/224` faces/edges/vertices, with all `456` and `744`
stable references equal. A 26-operation timing-only run after fallback produced
`1000/2520/1488` topology objects and all `4976` stable references equal; the
profiled serial and fallback runs were `41.23 s` and `40.96 s`. This is no
speedup because the direct branch was rejected. The expensive full-body
symmetric-difference oracle was deliberately skipped in that last timing run;
it remains a required gate after the descendant map is implemented.

The missing contract is explicit and must be implemented at topology creation
time: every Fillet and Chamfer must publish the complete mapping from each
authored parent edge to all surviving or generated child intervals. The mapping
must retain multiple valid aliases of one physical interval, distinguish
coincident but topologically separate edges, and survive subsequent treatments.
Only after that map resolves all 48 operation-4 references may the direct Add
result continue instead of falling back. Proximity, OCCT enumeration and a
single compressed context digest are insufficient evidence of ancestry.

## Verification status

Passed on Windows:

- exact tangent-route unit and query tests;
- OCCT edge-treatment support inspection tests;
- custom planar Fillet geometry tests;
- signed positive/negative boundary composition for planar, curved and
  multi-positive fixtures;
- exact tangent plane/cylinder boundary splitting;
- signed skeleton composition through the first two independent Fillets with
  exact material and topology counts;
- exact rounded-face boundary kinds, topology counts, Viewer identity
  multiplicity, and display-chord equivalence through Shell;
- Fillet/Chamfer command persistence test;
- solid-state Fillet document test;
- translations contract;
- edge-treatment, symmetric edge-treatment, and treatment-selection UI
  contracts;
- skeleton equivalence through the downstream Shell.

The aggregate `zima_cpp_ui_contract_tests` executable still fails before reaching
the changed tangent-route fixture: its framebuffer reports an unrelated base
colour mismatch for a construction point. An earlier run failed at a different
unrelated native hover-style assertion. The specialized affected UI contracts
pass. This general framebuffer failure remains explicitly unresolved rather than
being attributed to the Fillet change.
