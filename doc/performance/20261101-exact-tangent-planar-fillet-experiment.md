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

## Exact descendant-alias result (2026-11-02)

The next experiment removed geometric remapping from the direct-equivalence
probe and implemented only ancestry proven by the calculated operation history.
It preserves every exact edge and vertex alias that OCCT reports on the same
descendant shape. Before the direct Add boundary is modified by a Fillet or
Chamfer, later selected Add-intersection references are resolved against their
exact parent-face ancestry and attached as aliases. Later treatments propagate
those aliases through their ordinary `Modified` and `Generated` history. An
absent or ambiguous exact alias still rejects the direct block; there is no
proximity, ordering, or nearest-edge fallback.

The representative input was the protected project document
`ZE0176-0101-0000_SKELETON.prtz`, inspected read-only. Measurements used the
Windows Release build, OCCT 8.0.1, and baseline commit `76eac23e2`. No native
format or persisted project data changed.

For the six-operation prefix containing the first two Add operations and all
four sequential Fillets, every authored selection resolved exactly:

| Fillet | Resolved exact references |
| --- | ---: |
| First | 16 / 16 |
| Second | 16 / 16 |
| Third | 48 / 48 |
| Fourth | 32 / 32 |

The direct and serial prefix results had zero bidirectional Boolean material
difference. The persisted, freshly serial, and direct results also had zero
pairwise material difference. One measured serial/direct calculation was
approximately `897 / 1043 ms`; a later correctly forced direct-path repeat was
`1013 / 1160 ms`. An intermediate `988 / 958 ms` result was discarded after
the harness was found to have compared the serial evaluator with itself. This
small prefix is a correctness result, not a speedup claim.

A focused synthetic regression constructs four separate additive protrusions,
captures their Add-intersection identities, and applies four sequential
Fillets. It requires valid B-Reps and at most `1e-7 mm3` bidirectional material
difference between serial and direct evaluation. It also requires the complete
final stable reference sets to be equal. The regression passes with the original
exact treatment requests and performs no remapping.

The complete 33-operation skeleton used the direct path for Add blocks of 1,
17, and 2 operations without falling back. Two serial/direct measurements were
`51.78 / 30.62 s` and `50.71 / 31.58 s`, an observed reduction of about
38-41 percent on this document. A later correctly forced run measured
`49.92 / 29.67 s`, consistent with that range. This result does **not** pass the production
gate. Quick final-body checks found:

- equal solid and face counts (`1` and `1200`);
- a relative volume difference of about `5e-10` and a center displacement of
  about `2.49e-7 mm`;
- different edge/vertex counts (`3104/1824` serial versus `3109/1833` direct);
- `1487` missing serial reference identities and `1501` additional direct
  identities, predominantly Add-intersection versus direct boundary-union
  names.

The expensive whole-body bidirectional Boolean oracle did not complete within
the investigation window. It is not needed to reject this candidate: the exact
topology and reference sets already differ. The direct evaluator therefore
remains disabled by default and cannot be used for a production build or
release. The next required step is an exact generated-intersection identity
rule for the complete multi-Add block, followed by full material, topology,
reference, save/reopen, Undo/Redo, and parameter-edit verification.

A subsequent geometry experiment replaced the custom sewed Add boundary with
one OCCT multi-tool Fuse followed by the existing ZIMA provenance-protected
same-domain unification. On the 17-operation Add block, the Fuse took about
`0.72-0.76 s` and protected unification about `0.16 s`. The protected result
was valid and matched the serial topology exactly at `1/920/2344/1392`
solids/faces/edges/vertices. In comparison, the raw Fuse had
`1/1016/2440/1392`, an unprotected global unification over-merged the result to
`1/824/2056/1200`, and the custom sewed boundary retained five extra split
edges at thin-profile junctions (`1/920/2349/1401`). The protected multi-tool
Fuse is therefore the better materialization candidate.

With this candidate enabled only by test environment switches, the complete
26-operation prefix through the two following Fillets measured
`31.97 / 13.00 s` serial/direct and matched topology exactly at
`1/1000/2520/1488`. The complete 33-operation skeleton measured
`46.96 / 24.29 s` and matched topology exactly at
`1/1200/3104/1824`. Its area matched at the reported precision, center distance
was about `1.7e-12 mm`, and volume differed by about `2.4e-7 mm3` (roughly
`1e-14` relative). The final calculations exposed the same number of stable
references (`6128`), but `1487` serial identities were replaced by `1487`
direct-ancestry identities. This remains a production rejection: a new direct
identity system must first prove deterministic parameter-edit, save/reopen,
Undo/Redo and downstream-reference durability, or an exact serial-compatible
identity derivation must be implemented. The candidate remains disabled by
default and does not change the native format.

The direct boundary edge identity originally included a quantized geometric
interval signature. That signature was deterministic for an unchanged result,
but a driving-dimension edit changed `62` edge/vertex identities even though
the topology stayed at `1/1200/3104/1824`. It has been removed. Split intervals
are now distinguished only by persisted source vertices, incident source
edges, named adjacent faces and exact intersection-event ancestry. Coordinates,
length, parameter values and OCCT enumeration do not participate in the key.

The representative thin-profile owner
`01a16ee6ad9b779aa473ddddafd63257` was then changed from `5 mm` to `6 mm` in the
complete skeleton. The direct calculation took `21.13 s`, save took `2.52 s`,
reopen took `1.63 s`, and an unchanged repeat took `21.24 s`. The direct
reference set before and after the edit was identical: zero missing and zero
added face, edge or vertex identities. The repeated calculation after reopen
was also identical. This run additionally found and corrected an experimental
deferred-output defect: each direct Add row must retain the accumulated
measurement-source prefix, not publish an empty prefix before the final row.

A second-generation persistence check loaded that newly written `6 mm` file,
changed the same feature to `7 mm`, saved, reopened and recalculated again. It
measured `22.00 s` calculation, `2.63 s` save, `1.85 s` reopen and `22.65 s`
repeat. Both the persisted-versus-calculated and pre-edit-versus-post-edit
reference sets were exactly equal at `6165` identities, again with final
topology `1/1200/3104/1824`.

The full serial/direct comparison was repeated after the ancestry-only edge
change. It measured `42.11 / 20.50 s`, with equal topology, area at reported
precision, a `2.35e-7 mm3` volume difference (about `1e-14` relative) and a
`1.72e-12 mm` center displacement. The direct identity set remains internally
durable, but still differs from the established serial set by `1487` names in
each direction. Therefore the default remains off. Production activation still
requires either a compatibility translation for established serial references
or explicit approval of a new topology contract and its required native-asset
regeneration. Undo/Redo and Linux verification also remain open gates.

The focused four-protrusion/four-Fillet regression now enables the multi-tool
Fuse candidate itself, changes one additive tool dimension, and requires the
complete direct identity set to survive that edit while the material changes.
This makes ancestry-only parameter durability an automated contract rather
than a skeleton-only measurement.

Profiling then found that retaining `64` future Fillet aliases after the
17-tool Add block repeated the same geometric parent-face search once per
reference. The resolver now accepts the complete list in one call. More
importantly, when every supplied ancestry face is an exact member of the
current boundary, it obtains edge and endpoint support directly from the exact
edge/face and vertex/face incidence graph. The existing geometric projection
remains the fallback when the supplied faces are older operands rather than
the current boundary. This reduced the measured 17-tool block from about
`5.4-5.8 s` to `3.27-3.36 s` without changing its geometry or identities. A
complete `9.0 -> 9.5 mm` parameter calculation with Add batching only fell to
`18.90 s` and retained the complete identity set.

The four consecutive subtraction operations at history indices 27-30 are one
cut feature followed by three dependent mirror tools. A separately gated
prototype now calculates those tools first and applies the existing multi-tool
Cut once, preserving their document order and refusing blocks whose required
intermediate boundary is retained. The complete skeleton then measured
`38.80 / 13.82 s` serial/direct. It retained exact
`1/1200/3104/1824` topology, equal reported area, `1.72e-12 mm` center
displacement and about `2.35e-7 mm3` volume difference. The four-operation
subtract section itself measured about `0.87-0.98 s` instead of
`4.4-4.9 s` as four separate stages.

With both Add and Subtract batching enabled, the `9.0 -> 9.5 mm` edit took
`14.26 s`, save `1.82 s`, reopen `1.46 s`, and the unchanged repeat
`14.15 s`. A newly saved direct file was then changed from `9.5` to `9.75 mm`:
calculation took `14.03 s`, save `2.00 s`, reopen `1.21 s`, and repeat
`14.07 s`. Both runs had zero missing or added direct identities before and
after the dimension edit; the second-generation persisted set also matched at
all `6129` identities. At exactly `10 mm` this fixture undergoes a real
topological event and changes to `1/1172/3016/1776`; identity-set equality is
therefore neither expected nor claimed across that boundary.

The Subtract experiment increases the difference from established serial names
to `1607` identities in each direction because its generated cut topology is
now owned by the direct ancestry namespace. Its geometry and new identities are
durable, but the same compatibility/native-topology approval gate still
applies. `ZIMA_CPP_DIRECT_BATCH_SUBTRACT` and the complete direct evaluator
remain disabled by default.

The change adds no user-visible text. Localization catalogs for `cs`, `en`,
`de`, `fr`, and `ru` therefore require no update. Windows tests passed; Linux
compilation and runtime verification remain outstanding.

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

Additional Windows checks on 2026-11-02 passed:

- the four-sequential-Fillet direct-alias regression, including a tool
  dimension edit through the multi-tool Fuse path;
- document-shaped multi-tool Add and Subtract fixtures, including serial
  material/topology comparison and dimension-edit identity durability;
- OCCT boundary union/composition;
- Boolean split-edge ancestry, isolated Fillet/Chamfer resolution, and native
  save/reopen;
- OCCT edge-treatment support and custom planar Fillet tests;
- the complete C++ document and OCCT contract executable;
- five-language localization source/catalog coverage;
- complete-skeleton thin-profile edits from `5` to `6 mm` and from the newly
  saved `6` to `7 mm`, including save/reopen and repeated-calculation identity
  equality;
- exact-incidence alias resolution and the batched four-tool Cut, including
  complete-skeleton `9.0 -> 9.5 -> 9.75 mm` save/reopen cycles.

The contract executable contained two stale expectations discovered by this
run. Its old multi-solid Shell rejection contradicted the established support
for shelling a selected disconnected solid, and its Undo check hard-coded a
Czech feature name. The tests now verify the untouched second solid and compare
Undo with the actual saved revision name; product Shell and Undo behavior were
not changed.

The aggregate `zima_cpp_ui_contract_tests` executable still fails before reaching
the changed tangent-route fixture: its framebuffer reports an unrelated base
colour mismatch for a construction point. An earlier run failed at a different
unrelated native hover-style assertion. The specialized affected UI contracts
pass. This general framebuffer failure remains explicitly unresolved rather than
being attributed to the Fillet change.
