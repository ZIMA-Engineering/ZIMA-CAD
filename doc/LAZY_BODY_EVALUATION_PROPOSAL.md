# Lazy Body evaluation proposal

## Status

This document records the optimization direction agreed for investigation on
2026-10-28. It is a design proposal, not an implemented native-format contract.
Any persistence change still requires the separate format review, conversion
plan and verification required by the project rules.

### 2026-10-28 validation outcome

An isolated ordered-section experiment initially appeared to make one
representative Add block substantially faster while retaining the downstream
Fillet edges. A later production-route trial failed both required gates: it took
14.626 seconds versus 11.329 seconds sequentially, and the following Fillet had
non-zero two-way Boolean differences of approximately 450.9 and 1856.7 cubic
model units. That implementation was removed and is not a product path.

This proposal therefore remains research direction only. The next experiment
must preserve every authored intermediate boundary required by actual production
consumers, measure the complete publisher and reference route, and fall back to
sequential evaluation whenever equivalence is not proven. No native-format
change is introduced or authorized by this document.

## Objective

Avoid constructing and caching a complete sequential Boolean result after every
additive history operation. Preserve visible behavior, exact final geometry,
reference identity, side identity, oldest-face ancestry, Undo/Redo and native
document self-containment while reducing regeneration time and derived cache.

## Proposed representation

A Body remains one logical selectable modeling object while its evaluation may
temporarily consist of several exact feature-local solids.

The evaluator keeps:

- authored feature definitions and their existing stable identities;
- one exact local solid for each calculated additive feature when reuse is
  beneficial;
- exact pairwise intersection curves between contributing feature solids,
  including source ownership and complete/partial/missing state;
- materialized aggregate B-Reps only at required topology barriers;
- one final exact Body solid for immediate display, Assembly use, save and
  export;
- a bounded set of recent materialized frames for editing and Undo/Redo.

Old full history-prefix B-Reps are disposable. Feature-local solids and
intersection data must be invalidated from their actual inputs. Derived frames
may use a bounded LRU policy but must never become the sole authority required
to reopen the native document.

## Evaluation model

An additive Extrusion, Revolution, Sweep or similar operation calculates its
own local solid. Instead of immediately executing a sequential Fuse against the
entire preceding Body, the evaluator records its contribution and calculates
the required intersection curves with affected contributions.

Independent additive features may therefore calculate concurrently. A group is
materialized into a precise aggregate B-Rep only when a consumer requires the
resulting trimmed topology. Likely barriers include:

- subtraction or another Boolean that consumes the aggregate boundary;
- Shell or an operation requiring the final connected skin;
- a reference to a newly created result face or edge;
- exact sectioning, manufacturing output or exchange export;
- validation that the Body is one valid connected solid;
- any unsupported geometry for which the lazy result cannot prove equivalence.

Materialization should operate on the smallest dependency cluster that satisfies
the consumer, not automatically on the entire Body. The final exact Body result
is produced once after the affected suffix rather than after every additive row.

## Fillet and Chamfer integration

Custom edge treatments can consume the stored intersection curve directly,
without first requiring a complete sequential Fuse.

For each treatment the model records:

- the two owning source surfaces or feature solids;
- the exact intersection path and its stable semantic identity;
- whether the path is complete, partial or missing after regeneration;
- the end conditions supplied by the terminating faces;
- the explicit user choice between an intersection path and a surviving
  original source edge.

Chamfer is the preferred first implementation experiment because its offset
curves and ruled transition are simpler. Constant-radius Fillet can then use the
intersection as its spine, the adjacent surfaces as tangent supports and the
terminating faces as end conditions. A shortened intersection creates only the
valid partial treatment; a missing intersection must not silently switch to a
whole original edge.

The patched result may remain part of the lazy topology graph. Sewing and solid
construction are deferred until a topology barrier requires them.

## Display and selection

The user continues to see and manipulate one Body.

- Selecting the Body selects all of its current feature-local solid
  contributions as one logical object.
- Selecting a Feature keeps the established Tree synchronization and uses that
  Feature's authored preview wire.
- Ordinary hover, click and double-click inspection continue to use authored
  feature preview wires rather than result edges extracted from a calculated
  aggregate solid.
- Shaded local solids may be displayed together, but internal overlap edges,
  internal faces and coincident-face artifacts must not be exposed as final
  Body topology.
- Exact result-face selection materializes the smallest required cluster and
  must never offer an occluded internal overlap face as an external Body face.
- An Assembly occurrence treats the logical Body as one component with one
  occurrence transform regardless of its internal lazy representation.

## Expected performance effect

The current sequential form repeatedly constructs near-duplicate full B-Reps:

`(((A Fuse B) Fuse C) Fuse D)`

The proposed evaluator calculates local contributions and intersections first,
then performs one required aggregate construction for the affected cluster. The
expected savings are:

- fewer expensive OCCT Boolean calls in additive histories;
- concurrent calculation of independent local features;
- less repeated topology serialization and hashing;
- substantially fewer full history-prefix B-Reps in memory and native derived
  cache;
- smaller invalidation scopes after a parameter change;
- direct reusable input for custom Fillet and Chamfer.

These are hypotheses until measured. No production path should be enabled from
timing alone.

## Required experimental gates

The experiment must compare the existing evaluator and the lazy evaluator on
the same representative models and operations:

- cold open and explicit regeneration;
- changes near the beginning, middle and end of history;
- multiple successive edits and Undo/Redo;
- final B-Rep validity, solid count, volume, area, centroid and inertia;
- exact face/edge identity, oldest coplanar ancestry and side identity;
- selection, inspection, sectioning, Drawing, Assembly and export behavior;
- save/reopen with no required external cache or sidecar;
- memory, native file size, cache size and wall-clock timings;
- complete, partial and missing edge-treatment intersections.

Production use requires geometrically and semantically equivalent output. Any
case that cannot prove equivalence falls back to the existing exact sequential
materialization path. The existing path must not be removed until the supported
matrix passes and the user approves any required native-format change.
