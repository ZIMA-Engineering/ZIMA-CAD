# Ordered Boolean section ancestry probe (superseded, 2026-10-28)

## Status

This report preserves an isolated research result. A later production-route
trial contradicted its performance and downstream-geometry conclusions. The
candidate is rejected and no batch evaluator from this probe is present in the
product. The full/partial/absent identity contract remains useful input to a
future design, but every result below must be read in that superseded context.

## Purpose

This research probe tests whether an independent Add block can be evaluated as
parallel feature tools followed by one ordered Boolean publication, without
losing the persisted edge identities required by later Fillet operations. It
extends the adoption gate in
[`20261027-native-cache-close-and-redesign-gate.md`](20261027-native-cache-close-and-redesign-gate.md);
it does not install a new production history evaluator.

The representative input was a private automotive Skeleton stored outside the
repository. The probe opened it read-only. No source geometry, native document,
derived model, customer path, or customer identifier is committed here.

## Candidate execution model

The tested history contains 32 operations. A planner identifies a 17-operation
independent Add block at indices 6 through 22. The candidate path:

1. captures only the persisted reference faces required by each independent
   tool;
2. calculates independent tools in parallel and calculates copies only after
   their source tool is available;
3. performs one parallel multi-tool Fuse;
4. records exact source-face support on Boolean section edges and vertices
   before same-domain unification;
5. derives the persisted intersection-edge key from the Boolean role, source
   face pair, endpoint supports and document-history age; and
6. publishes one final native boundary for the block.

OCCT continues to own the intersection geometry. ZIMA owns the semantic
identity and never derives it from OCCT enumeration order, UUID ordering,
coordinates, length, or another measured tie-breaker. Coplanar faces retain the
oldest contributing reference according to document history.

## Edge-treatment contract

Each persisted Fillet or Chamfer edge is classified against the calculated
batch boundary:

- **full**: one unique survivor has the same extent;
- **partial**: one or more uniquely proven descendants survive with changed
  extent; treatment applies only to those surviving intervals; and
- **absent**: no proven descendant survives; that requested edge is omitted and
  is never rebound to an unrelated nearby edge.

The persisted feature definition remains unchanged. A diagnostic must report
partial or absent selections. If every requested edge is absent, the history
container becomes an explicit no-op boundary rather than silently changing its
selection. If the remaining proven edges cannot produce a valid OCCT treatment,
the operation fails visibly; the evaluator must not guess another edge.

The probe stores a vector of descendants per persisted identity, so a split
edge is not forced into a one-edge locator. The representative unchanged model
resolved all 64 downstream treatment identities as full survivors: 32 through
ordinary Boolean ancestry and 32 through recorded section-face ancestry. No
geometric fallback was needed. Partial and absent behavior is specified here
but still requires dedicated synthetic regression fixtures before production
activation.

## Measured result

Windows Release build, native C++ implementation, OCCT 8.0.1:

| Measurement | Result |
| --- | ---: |
| Existing sequential evaluation, operations 6-22 | 21.928-26.328 s |
| Parallel construction of all 17 tools | 0.576-0.598 s |
| One multi-tool Fuse | 0.715-0.842 s |
| Same-domain unification | 0.245-0.306 s |
| Native archive | 0.089-0.092 s |
| One final publication | 0.847-0.933 s |
| Cold candidate total for those phases | 2.537-2.708 s |
| Hot rebuild of one changed tool | 0.193-0.224 s |
| Hot candidate total with cached sibling tools | 2.154-2.333 s |

In this isolated harness the paired runs made the listed phase total 8.6-9.7
times faster than its separately instrumented sequential block, and the
one-tool edit case appeared 10.2-11.3 times faster. The harness did not reproduce
all production publication and downstream-consumer costs. A later
production-route measurement took 14.626 seconds for the candidate versus
11.329 seconds sequentially, so the apparent speedup did not survive the
production-equivalent gate.

The sequential boundary has 920 faces and 2,344 edges; the same-domain batch
boundary has 824 faces and 2,056 edges. This is a topological simplification,
not a geometry change. Raw independently integrated volume values differ by
0.369 cubic model units because the two topologies use different integration
partitions. The authoritative two-way Boolean difference is zero.

## Downstream verification outcome

The isolated publisher reported zero two-way Boolean difference through its
instrumented stages. The production-route trial did not confirm that result.
After the candidate Add block, the following Fillet produced non-zero Boolean
differences: expected-minus-candidate was approximately 450.9 cubic model units
and candidate-minus-expected approximately 1856.7 cubic model units. This is a
geometry failure, not an acceptable topological rearrangement.

The most likely lesson is that preserving selected section-edge ancestry alone
does not reproduce every authored intermediate boundary consumed by later
treatments. A future attempt must compare each real production consumer against
the sequential boundary and include the complete publisher, unification and
reference-rebinding route in its timing. The failed candidate was removed rather
than retained behind a production switch.

## Remaining production gates

The isolated result did not pass the production gate. Reconsideration requires
all of the following before any replacement of the linear evaluator:

- 96 final faces remain deliberately unresolved by the research publisher,
  including non-planar multi-parent cases. General face, edge and vertex
  references must be complete, not only the 64 edges used by the measured
  Fillets.
- Synthetic fixtures must cover full, split/partial and absent edges, including
  a mixture of these states in one Fillet and Chamfer, tangent-contour expansion,
  variable Fillet endpoints, and a completely absent selection.
- Equivalent topology identity must be verified after dimension edits,
  suppression, reorder, Undo/Redo, save/reopen and another regeneration.
- Cache invalidation must prove that a changed source rebuilds every dependent
  tool while unchanged siblings are reused.
- The planner must keep Fillet, Chamfer, Shell and any stateful or unresolved
  reference dependency as barriers. Copy dependencies may form a small ordered
  subgraph inside an otherwise parallel block.
- The result needs normal contract, persistence and GUI regression coverage.
  A native format change is not currently required or authorized.
- Linux verification remains outstanding. Windows and Linux must share the same
  implementation and OCCT 8.0.1 behavior before production activation.

## Localization review

The probe and this report add no user-visible application text. The `cs`, `en`,
`de`, `fr` and `ru` catalogs are therefore unchanged.
