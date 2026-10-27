# Native viewer cache, document close and redesign gate

## Scope and representative data

The reported private fixture is `ZE0176-0101-0000_SKELETON.prtz`, a one-Body
Skeleton with 31 calculated operations, dense external references and a large
persisted viewer/reference packet. Measurements use isolated copies with the
Windows x64 Release build, Qt 6.11.0 and the shared OCCT 8.0.1 pin. The source
project file is never modified or packaged.

This work addresses native open/save cost and the long UI pause when closing an
already saved Skeleton tab. It does not claim that a changed Boolean or Fillet is
now inexpensive. Wall-clock values are observations on this machine; exact work
and output comparisons are the correctness gates.

## Exact viewer cache v2

The disposable calculated cache now writes repeated triangle identities as run
lengths and stores viewer coordinates in bounded binary arrays. Identity and
detail tables retain sheet side, thickness, owner, occurrence, display owner,
edge-treatment side/direction/endpoints, measured values and point surface state.
Earlier Body boundaries may reference a single complete measurement-source list
by exact prefix length only after every source archive, property and topology
binding matches. Unsupported layouts keep their full packets.

Controlled isolated-copy observations were:

| Operation | Previous cache | Viewer cache v2 |
| --- | ---: | ---: |
| Save current calculated document | approximately 17.0 s | 12.394 s |
| Reopen rewritten document | 13.7--13.8 s | 9.716 s |
| Complete current-cache load | 13.7--13.8 s | approximately 9.45 s |
| Shared-cache expansion phase | 6.504 s | 4.261 s |

This is approximately a 27--30% improvement in the measured open/save path, not
an order-of-magnitude claim. The complete authored-definition digest and every
expanded calculated packet digest match before and after the round trip. No
geometry precision, topology identity, reference side, Undo/Redo rule or authored
native format version changed. Current v1 calculated caches remain readable and
are rewritten as v2 when their calculated packet is next rebuilt.

## Exact unchanged-cache save reuse

Profiling the current 31-operation Skeleton showed that disk I/O was not the
save bottleneck. The measured full save took 11.558 s: document serialization
took 2.836 s, cache packing and text encoding took 6.198 s, compression took
1.419 s and the final 32 MB disk write took only 0.097 s. The packer visited
725,202 compound blocks and performed 671,256 exact equality checks. Rebuilding
an already validated immutable calculated packet was therefore unnecessary
work, not useful persistence or geometry work.

An open Part session now retains the exact encoded `CachedBodies` text beside
the immutable calculated-boundary packet. Metadata-only revisions share both.
A geometry commit, recalculation or replacement drops the encoded text. An
exact-definition caller additionally requires the operation count, final source
fingerprint and complete authored-definition SHA-256 to match. The live session
may retag the definition proof after a metadata-only commit because it still
owns the same immutable calculated packet; this authority is not available to
ordinary callers. A miss follows the unchanged full serialization and
validation path. Successful full saves publish their newly encoded cache back
to the session for the next metadata save. Undo may restore an older cache only
together with the exact immutable state that owned it.

On the protected source file, an exact unchanged-cache save took 0.587--0.633 s
instead of 11.558 s, an observed improvement of approximately 18--20 times. The
output remained exactly 45,753,383 bytes because reuse deliberately retained
the source cache encoding. Reopen expanded all 31 boundaries and a complete
packet comparison found no change in geometry, properties, references or side
identity. A separate contract changes the geometry while deliberately offering
the old token; the token is rejected, a new cache is built and the exact
save/reopen comparison passes.

The shared-block threshold was measured independently. Raising it from 256 to
1,024 bytes reduced a forced full save from 11.558 s to 10.608 s while growing
the rebuilt file by 3.6%; open time was unchanged within run-to-run variation.
A 4,096-byte threshold regressed both save time and file size and was rejected.
The selected 1,024-byte threshold remains a full-cache packing optimization;
it is not involved in the sub-second unchanged-cache path.

Capturing the encoded token does not duplicate its large string after parsing:
ownership moves out of the temporary INI representation. There is still one
bounded in-memory encoded cache per exact live Undo state that owns one. The
token is disposable derived data and is never required to reopen the native
document. The native format, precision and five-language UI catalogs are
unchanged.

## Direct pooled-string native loading

The open path also repeated work after the cache had already proved exact
sharing. First, cache validation rebuilt the complete kernel-operation list a
second time before calculating history fingerprints. Reusing the already
constructed, immutable operation list reduced that phase from 422 ms to 6 ms
on the representative Part. The same list still receives solid-state
resolution before the fingerprints are accepted; definition, reference and
boundary validation remain enabled.

Profiling the expanded viewer cache then found 114,026 copied pooled strings,
totalling approximately 410 MB, before native packet construction. The v2
fixture contained only 11,582 distinct pooled values, but ordinary JSON
expansion had to materialize each repeated BRep, binary viewer array and long
semantic key at every use. The Part reader now retains exact pooled strings
through cache expansion. Binary arrays are decoded directly from their shared
bytes, and `KernelShapeArchive` takes shared ownership of the already decoded
BRep string. Compound JSON blocks keep the existing independent-copy and
validation behavior. Assembly decoding and the public fully expanded cache
reader are unchanged.

On the same Windows Release build, the rewritten v2 fixture opened in 5.991 s,
compared with 7.43 s before direct pooled-string loading. The original 45.75 MB
v1 source opened in 7.214 s and then completed an exact unchanged-cache save in
0.554 s. Reopening that output and serializing all 31 calculated boundaries
produced an exact packet match. A focused contract additionally round-trips a
large BRep and topology keys longer than 4,096 bytes through the shared path.

The measurement also confirms that identity growth is a separate remaining
cost, not a validation cost. Serializing the representative final packet found
673,415 semantic-key uses with 723,587,850 logical bytes, but only 8,692
distinct values; the longest key was 33,950 bytes. Boundary-reference
validation itself cost 31--42 ms, the authored-definition digest 7--9 ms and
cache-DAG inspection approximately 115--133 ms. Removing those checks would
save only a small fraction of open time while accepting silent corruption.
Changing the in-memory identity representation or bounding generated ancestry
keys is therefore a separate topology-contract experiment. It is not included
in this optimization and would require the native-format and reference-identity
approval gates before adoption.

This change does not alter persisted bytes, cache compression, calculation
precision, topology identity, geometry side, Undo/Redo behavior or user-visible
text. Windows contracts pass. Native Linux compilation and timing remain
unverified on this host.

## Immediate close detachment

Closing removes the complete document family from `Workspace` before destroying
its immutable native history. The desktop host then retires that detached state,
plus the View's old display/reference meshes and picking indexes, on the Qt global
thread pool. No worker touches Qt widgets, the Workspace or mutable document
state. Hosts that do not provide a release callback retain synchronous
destruction. A contract verifies that a top-level Assembly and its hidden source
family are all detached and released.

## Immutable calculated packets for metadata history

A Body/history cursor, Body activation or Body visibility change modifies only
document metadata. It previously copied the complete calculated-boundary vector,
including every retained BRep, viewer mesh and reference packet, into the new
Undo state. That made the `Insert Here` transaction scale with calculated model
size despite invoking no OCCT operation.

Calculated packets are now immutable shared state. A metadata revision owns its
independent document definition and revision number but shares the exact packet
with its predecessor. Recalculation and save-analysis publication replace the
packet atomically instead of mutating shared data. Family documents retain their
existing common transaction route and copy only when a Family transaction
actually needs an independently mutable candidate.

On the representative 31-operation Skeleton, six cursor positions previously
needed 629--829 ms per metadata commit. The same positions now need
2.2--4.3 ms, a measured reduction of approximately 160--290 times. Constructing
the visible cursor scene remains separate useful work: it ranged from below
0.01 ms for an empty boundary to 102 ms for the complete Body. The probe asserts
the same calculated allocation and final source fingerprint after every move
and reports zero OCCT operations. Body metadata, Undo/Redo, no-op, save/reopen,
Family Table and independent session-copy contracts pass. No native format or
user-visible text changed.

This changes latency ownership, not model semantics. Application shutdown may
still wait for the global pool to finish owned destruction; ordinary saved-tab
close no longer needs to keep the UI thread inside that destruction.

## Rejected shortcut

A local additive contact replacement was prototyped because it avoided a full
Boolean and was substantially faster. It changed exact topology identities on
the representative result. The experiment was therefore removed from product
code. Fast geometry with different stable references is a failed result, not an
optimization.

## Gate for the proposed internal redesign

The next experiment may maintain a semantic dependency/topology graph separately
from disposable OCCT results and viewer caches. OCCT remains the authority for
exact BRep results. The graph may decide which operation, contact region and
cache are invalid, but it may not invent approximate faces or replace persisted
side/ancestry identity.

The redesign is adopted only if all of these conditions pass:

1. Representative Skeleton open, save and changed-feature workflows improve by
   at least tenfold against a recorded same-build baseline, excluding one-time
   conversion or fixture setup.
2. Complete BRep/viewer/reference packets, stable owner/semantic/occurrence
   identities, oldest-face ancestry, geometry sides and physical properties
   match the independent full evaluator.
3. Warm reuse, cold invalidation, dependent Fillet/Chamfer chains, multiple
   Bodies, external references, Undo/Redo and save/reopen all pass.
4. Memory growth is bounded and a cache miss remains correct. Background work
   never publishes a partial result as a committed model and never blocks View
   navigation with mutable cross-thread state.
5. The shared Windows/Linux implementation and OCCT 8.0.1 pin remain aligned.

If the tenfold result or any equivalence gate fails, the architecture is not
introduced. Useful isolated measurements may guide smaller changes, but a broad
rewrite is not justified by a percentage-only improvement.

## Verification

Focused Windows Release contracts cover viewer v2 round trips and malformed
input rejection, final-cache reconstruction, multi-Body/suppression/Boolean
packets, complete document-family release, rounded mirrored-Sketch deletion,
oriented Rectangle equations and the five localization catalogs. The Sketcher
matrix includes both sides and selection orders, native X/Y and oblique axes,
driving/reference and fully constrained states, repeated edits, real drags,
Undo/Redo, GUI interaction and save/reopen.

The final interaction selection ran 14 focused native/GUI suites sequentially;
all passed. The nested Body/Assembly Sketch scenario then passed seven repeated
process runs before one startup access violation. The same scenario reproduced
the same `0xC0000005` failure on the unchanged signed 2026102603 executable in
its fifth repeated process, so this is an existing intermittent GUI-test/process
lifetime limitation rather than evidence introduced by the current changes.
The pre-existing rounded-tangent-wing matrix also retains one rejected driving
height edit (`swap=0`, negative X/Y side, native unlocked underconstrained
geometry); its other 512 angled variants pass. Neither limitation is represented
as fixed by this work.

Native Linux compilation, execution and timing remain unverified on this Windows
host. Windows package publication has separate committed-source and signed-smoke
gates.

## Continued exact-calculation investigation

The representative Skeleton's changed last operation was also measured with
OCCT's oriented-bounding-box Boolean candidate filter. The ordinary, forced
serial, forced parallel, serial OBB and parallel OBB runs produced the same
386,915,509-byte calculated packet with SHA-256
`c44542f4b7b9d56c249fba906f6b3a0343d7557b23e2271e350f67e9aaa1398c`.
Observed calculation times were 4,595 ms, 4,340 ms, 4,466 ms, 4,601 ms and
4,496 ms respectively. OBB therefore did not improve this operation and the
experimental switch was removed. No Boolean tolerance or geometry precision was
changed.

Explicit GUI calculations already execute OCCT on a worker-owned candidate and
publish only after completion. The remaining apparent freeze was caused by the
global busy-input gate also consuming camera events. The gate now admits only
wheel zoom and an actual middle-button drag in the active model View while the
worker runs. MMB+RMB panning is retained. Selection, hover, commands, keys,
short MMB confirmation, middle double-click, dialog confirmation and window
close remain blocked. This changes responsiveness only: it does not shorten the
calculation, expose the mutable candidate or publish a partial result.

The operation-activity contract verifies worker-thread ownership, identical
synchronous geometry, live painting and animation, delivery of navigation
events, rejection of command input, exception cleanup and nested scopes. The
complete Windows application builds with the shared runner. The five-language
catalog contract passes; this change adds no user-visible text. Native Linux
compilation and actual Linux View navigation remain unverified on this Windows
host.

## Additive semantic-batch experiment

The same protected Skeleton exposed a long additive interval after its Shell.
Operations 6 through 18 contain thirteen additive Features and in-Body copies,
including twelve explicit Up To target faces, but no Fillet, Chamfer, Shell or
subtractive material operation. The ordinary evaluator repeatedly fuses and
unifies the growing Body. A research probe instead calculated each independent
tool and submitted the complete interval to one multi-argument OCCT Fuse,
followed by one provenance-preserving same-domain unification.

The explicit Up To targets were then captured from their original defining
faces instead of passing the complete preceding Body as the reference universe.
Every resulting tool retained an identical native BRep archive and the same
face, edge and vertex binding counts. This confirms the existing contract in
`capture_reference_face_history`: a later Boolean fragment does not replace an
original Up To source. Up To is consequently a dependency edge to a named
source region, not automatically a material-state barrier.

Representative Windows Release observations were:

| Additive interval | Independent minimal tools | Multi-Fuse | Unify |
| --- | ---: | ---: | ---: |
| operations 6--12, seven tools | 1.115 s | 0.401 s | 0.140 s |
| operations 6--18, thirteen tools | 1.31--1.47 s | 0.46--0.54 s | 0.21--0.32 s |
| operations 6--22, seventeen tools | 0.51--0.57 s in parallel | 0.59--0.77 s | 0.18--0.29 s |

Capturing the twelve targets through today's historical packet search cost an
additional 0.87--0.99 s. A semantic graph can address those immutable source
regions directly and must not pay that scan on each calculation. The probe also
calculated every tool through the current full reference universe as an A/B
oracle; those duplicate oracle calculations are excluded from the table.

The thirteen-tool result was one valid solid. Its surface area differed from
the sequential result by `9.31e-9 mm2`; its reported volume differed by about
`1.20 mm3` over `18,450,448 mm3`. The batch result contained 788 faces and 1,992
edges, compared with 884 faces and 2,280 edges in the sequential result. The
reduction is real topological simplification, not a precision reduction.
Two independent Boolean comparisons then calculated `sequential - batch` and
`batch - sequential`. Both remaining volumes were zero. The small reported
volume difference is therefore an integration result over the same material
boundary, not missing or additional material.

The longer interval includes the solid Loft at operation 19 and its three
mirrors. Its ordinary sequential calculation took 23.698--23.797 s. The batch
result was one valid solid with 824 faces and 2,056 edges, compared with 920
faces and 2,344 edges in the sequential result. Both directional material
differences were again zero. Its reported volume differed by 0.369 mm3 over
18,555,842 mm3, while the surface-area difference was `6.52e-9 mm2`.

The calculation planner classified 21 additive operations into four physical
Fuse batches, replacing 21 sequential additive Booleans with four. The final
subtractive feature and its three copied tools can likewise be submitted as one
multi-tool Cut. That Cut took 0.481 s plus 0.333 s for unification and produced
one valid solid. Both directional material differences against the sequential
four-Cut result were zero. The tools needed one explicit source face of 3,462
bytes; the current final boundary retained 102 measurement sources totalling
61,239,008 BRep bytes.

For the representative changed interval 6--18, the ordinary sequential evaluator
in deferred-analysis mode took 17.716 s. The batch path measured 0.525 s for
parallel preparation of all independent tools, 0.464 s for Fuse, 0.218 s for
unification, 0.071 s for BRep/topology persistence and 0.731 s for a complete
display packet. Reusing unchanged per-feature tools and rebuilding only the
edited first tool reduced preparation to 0.170 s, for a measured critical path
of approximately 1.65 s. This isolated hot-edit interval therefore passed the
tenfold timing threshold. It is not yet a claim that the complete document edit,
open or save workflows pass the redesign gate.

For operations 6--22, rebuilding only the changed first tool took
0.160--0.209 s. Parallel preparation of the complete tool set took
0.51--0.57 s. Fuse, unification, native persistence and a complete deferred
viewer packet put the measured hot additive boundary at approximately
2.0--2.2 s, about eleven times faster than the 23.7 s sequential interval.

Exact aggregate properties must not block ordinary display publication. On this
same batched boundary, triangulation took 0.233 s, BRep persistence 0.067 s,
face packets 0.016 s and edge packets 0.064 s. Exact volume and inertia consumed
12.638 s, including 11.465 s in the retained Gauss--Kronrod volume calculation.
The existing deferred-property contract can publish the valid immutable body and
View packet first, then complete identical exact properties for an explicit
consumer or save snapshot. This changes scheduling, not precision or values.

OCCT history associated every one of the 788 batch faces with a source face.
596 faces had one semantic parent and 192 had exactly two; none had more than
two and none lacked ancestry. The shorter seven-tool interval showed the same
shape: 496 single-parent faces and 160 two-parent faces. A direct replacement
cannot yet be published because only 745 of the sequential result's 884 current
face identities survive verbatim. Treating two parents on one OCCT face as an
ordinary alias would make the existing `TopologyReferenceIndex` deliberately
ambiguous and therefore unselectable.

The first downstream checkpoint contains two Fillet operations that request 64
persisted edge identities. Thirty-two identities propagated directly into the
batch shape. The other 32 are sequential Boolean intersection identities: eight
owned by the Loft and eight by each mirror. Exact curve-containment checks mapped
all 32 identities uniquely to 32 batch edges. Each of those edges already had
one authored Loft/mirror identity, so the correct representation is an explicit
two-name physical-edge alias, not a coordinate-derived replacement or a new
random identity.

The treatment resolver now accepts an ambiguous physical contour only when the
operation explicitly requests its complete persisted alias set. Unrequested
aliases and ambiguous tangent-contour expansion remain errors. With those exact
aliases attached to a synthetic batch checkpoint, the two real downstream
Fillets calculated in 3.819 s. Their result had the same 920 faces as the
sequential checkpoint, and both `sequential - batch` and `batch - sequential`
material differences were zero. This passes the representative dependent-Fillet
geometry gate; generalized Chamfer, partial-alias selection, save/reopen and
other models still require contracts before product batching is enabled.

The same two Fillets were also calculated on their smallest exact contact
island: additive operations 18--22, selected from every explicit parent encoded
by the persisted edge references. The finished island was then fused into the
unchanged main additive batch. This took 2.431 s, compared with approximately
4.70 s for the initial whole-batch Fuse/unification followed by both full-body
Fillets. Both directional material differences against the sequential
post-Fillet result were zero. This does not replace OCCT Fillet; it demonstrates
that the semantic edge graph can give OCCT the exact smaller body on which the
treatment depends.

Fillet and Chamfer also no longer validate a raw treatment shape that is
immediately discarded by same-domain unification and then validate the published
shape a second time. The published unified result validates itself; an unchanged
fallback result is validated once. This removed approximately 0.55--0.75 s from
the representative downstream Fillet pair without omitting validation of the
shape that becomes model state.

The remaining published same-domain validation now uses OCCT's parallel
`BRepCheck_Analyzer` mode. Serial and parallel analyzers both accepted the two
representative late additive boundaries. In repeated loaded-system observations,
serial validation took 272--373 ms while parallel validation took 173--278 ms.
The change is limited to the exact unified shape being published; small
intermediate construction checks retain their existing behavior. Geometry
controls and exactness settings are unchanged.

The same synthetic checkpoint was then evaluated through the remaining Skeleton
history. After the Fillets, additive operation 25 completed as one child-tool
Fuse in 1.845 s. The following four subtractive tools completed as one Cut in
1.119 s, and final additive operation 30 took 1.672 s. Bidirectional material
differences were zero after operation 25, after the subtractive batch and after
the final operation. Combining the 0.155 s hot changed-tool rebuild, 2.431 s
localized Fillet island and these exact suffix stages gives approximately 7.22 s
of measured geometry work, excluding final viewer publication. That is a major
improvement over the observed roughly 37 s but still does not pass the tenfold
whole-edit gate. The remaining dominant targets are the two isolated late
additive Booleans and checkpoint publication, rather than the removed
seventeen-Boolean chain. The multiple bidirectional comparison Cuts used only as
the equivalence oracle took several additional minutes and are excluded from
these interactive timings.

The proposed internal representation therefore separates these concerns:

1. Authored Features, original source surfaces, trimmed semantic regions and
   their side/ancestry identities form an immutable dependency graph.
2. Consecutive material-independent additive nodes form a calculation batch.
   Fillet, Chamfer, Shell, subtraction and other operations that consume the
   actual current material state create exact checkpoints. An explicit Up To
   source remains inside a batch when its named source region is available.
3. A calculated OCCT face has one canonical selectable identity. Additional
   source regions merged onto that face remain virtual trimmed regions, not
   duplicate selectable bindings. They still resolve exact Up To, placement,
   external-reference and side queries.
4. The native file stores the final display/body packet, compact semantic graph
   and exact barrier checkpoints. It does not store every nearly identical
   cumulative Body. Missing editable interval states are materialized from the
   preceding checkpoint on demand. Disposable checkpoint hydration may continue
   in the background while the immutable published View remains navigable.

The minimal dependency proof also quantified the semantic-source reduction.
The complete interval used twelve unique original Up To faces totalling 28,366
BRep bytes. The current final boundary carried 71 measurement-source snapshots
totalling 27,736,154 BRep bytes and 10,264 face bindings. A production graph
must retain sources by actual downstream liveness, not copy every historical
measurement source into every later boundary.

One shared PaveFiller was also tested as a shortcut for producing every prefix
cache state. Its intersection phase took 0.208 s, but building and unifying all
thirteen prefixes took 17.523 s and early prefixes contained geometry split by
later arguments. That approach is rejected for history cache construction. The
fast final batch and the exact background checkpoint/cache calculation remain
separate responsibilities.

## Public implementation comparison

The experiment was compared with current public implementations rather than
product screenshots. The inspected paths show useful incremental invalidation,
but they do not show an equivalent of the measured semantic Fuse batching or
localized treatment islands above:

- [FreeCAD's document contract](https://github.com/FreeCAD/FreeCAD/blob/main/src/App/DocumentObject.h)
  exposes touched/recompute state for dependency-ordered feature evaluation.
- [Dune 3D's document evaluator](https://github.com/dune3d/dune3d/blob/main/src/document/document.cpp)
  remembers the first group requiring solid-model work and then updates every
  following group. Its
  [OCCT accumulator](https://github.com/dune3d/dune3d/blob/main/src/document/solid_model/solid_model_occ.cpp)
  combines each feature tool with the previous accumulated shape through one
  `Fuse`, `Cut` or `Common`; Fillet and Chamfer operate on that complete
  accumulated shape.
- [ToubkalCAD's recompute engine](https://github.com/ToubkalCAD/ToubkalCAD/blob/main/src/services/RecomputeEngine.ts)
  retains clean cached nodes, marks an edited feature and its descendants dirty,
  and replays those descendants in topological order.
- [CadQuery's Workplane model](https://github.com/CadQuery/cadquery/blob/master/doc/workplane.rst)
  carries a context solid through the parent chain and combines later operations
  into that context by default.

This is not proof that no proprietary or uninspected path uses similar
techniques. It does show that dependency invalidation and cached clean prefixes
alone do not remove the representative Skeleton's expensive changed suffix. The
ZIMA experiment therefore keeps those established mechanisms and additionally
targets the measured repeated physical Booleans, exact source-region lookup and
whole-body treatment cost.

The batch evaluator is still a research probe. Product integration remains
blocked until virtual-region resolution, generalized edge/vertex ancestry,
dependent Chamfer and partial-alias behavior, save/reopen, Undo/Redo, multi-Body
and external-reference equivalence pass the redesign gate above. No native
format or user data has been changed by this experiment.

## Interactive command and Sketch response

The product-side work accompanying the research probe removes measured scene
and solver work that is independent of the proposed Boolean redesign. It does
not enable the experimental batch evaluator.

- Opening the New Feature command on the representative Skeleton copy changed
  from 1.515 s to 0.280--0.353 s. Selecting Default Origin changed from about
  5.116 s to 0.272--0.280 s. The command publishes its one required transient
  preview and no longer republishes the calculated final-Body scene that the
  rollback/input scene immediately replaces.
- Ordinary Sketch dragging no longer reconstructs the unchanged Body context on
  every mouse move. The representative rounded-profile move previously spent
  90--121 ms rebuilding that context and another 55--76 ms resolving already
  derived corner geometry before a 57--70 ms mesh build. The retained analytic
  interaction path measured approximately 2.83 ms for solving and 3.28 ms for
  the Sketch mesh, or about 6 ms total in the measured case. Confirmation still
  uses the full native solve and validation path.
- Regular polygons use their exact centre/radius/phase representation while they
  are dragged. A direction-constrained edge scales about the fixed centre when
  radius remains free; a locked-radius conflict is rejected atomically. Exact
  point-on-circle and equal-chord relations are installed as one validated
  transaction rather than repeatedly solving an incomplete relation set.
- Passive construction-axis endpoints are no longer displayed in an ordinary
  View and the obsolete toolbar filter is removed. A command that accepts those
  persisted endpoints reveals and picks them through its transient context only.
  Axis lines, Mirror/Pattern copies, endpoint identities and native persistence
  are unchanged. Cylindrical and sheet faces therefore remain selectable without
  endpoint markers intercepting ordinary picking.

The focused GUI contracts verify zero base-scene publication while New Feature
opens, transient endpoint availability during explicit command entry, hidden
endpoints after the command, independent cylindrical-face selection, copied-axis
presentation and exact Axis Up To distance. The native Sketcher matrix covers
polygon H/V and oblique direction constraints, free and locked radius, repeated
drags and value changes, both selection orders, underconstrained and fully
constrained cases, external supports, Undo/Redo and save/reopen. All five
translation catalogs validate; this change introduces no new user-visible text.

These changes address unnecessary interactive work only. An unchanged OK on the
late representative Feature still measured approximately 47.128 s because the
current product evaluator recalculates the expensive dependent history suffix.
That result remains the next hard optimization target and must not be presented
as solved by the interaction improvements above.
