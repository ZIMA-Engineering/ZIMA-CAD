# Ordered Boolean section ancestry probe (2026-10-28)

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

The paired exact runs make the cold phase total 8.6-9.7 times faster than the
sequential block. The measured one-tool edit case is 10.2-11.3 times faster.
These are measurements of this representative history, not general performance
promises.

The sequential boundary has 920 faces and 2,344 edges; the same-domain batch
boundary has 824 faces and 2,056 edges. This is a topological simplification,
not a geometry change. Raw independently integrated volume values differ by
0.369 cubic model units because the two topologies use different integration
partitions. The authoritative two-way Boolean difference is zero.

## Exact downstream verification

The final probe performed two-way Boolean-difference checks after each tested
stage. Every listed difference was exactly zero:

| Stage | Expected minus candidate | Candidate minus expected |
| --- | ---: | ---: |
| 17-operation Add block | 0 | 0 |
| Two following Fillets plus next Add group | 0 | 0 |
| Four following Subtract operations batched once | 0 | 0 |
| Final following Add group | 0 | 0 |

The downstream batch treatment completed without an identity or calculation
error. A second localized-Fillet construction also produced zero two-way
difference, supporting a future independent treatment branch when only a small
subset of batch tools participates in a Fillet.

## Remaining production gates

This result proves the central geometry and downstream-Fillet hypothesis on one
representative model, but it is not yet safe to replace the production linear
evaluator:

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
