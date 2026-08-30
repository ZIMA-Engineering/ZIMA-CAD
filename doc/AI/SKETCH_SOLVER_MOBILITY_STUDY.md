# Sketch solver mobility study

## Objective

Make dimension edits and point dragging fast and predictable while preserving
the user's geometric intent. The solver must report the correct degrees of
freedom, move a genuinely free branch, preserve unrelated radius/diameter and
axis parameters, and reject a transaction when no valid branch exists.

## Engineering model

INPUTS:

- persisted Sketch points, geometry, constraints, dimensions and references;
- the exact user action (dragged point, edited dimension or edited parameter);
- the requested cursor position or parameter value.

MEANS:

- a persisted-ID mobility graph independent of OCCT;
- component-local constraint equations and numerical Jacobians;
- context-dependent displacement weights;
- transactional candidate solving followed by full equation verification;
- idle-time caches containing graph topology and symbolic sparsity only.

OUTPUTS:

- a valid Sketch state satisfying every active equation;
- correct rank and remaining degrees of freedom;
- the minimum-cost valid movement consistent with the user action;
- deterministic conflict/redundancy diagnostics;
- interactive latency suitable for pointer dragging.

## Measured baseline

The benchmark uses independent fixed-anchor branches. Each branch has one
movable point, one horizontal equation and one distance equation. This is a
deliberately favorable case for graph partitioning because the current global
dense matrix contains no cross-branch information.

| Branches | Variables | Equations | Current dense mean |
|---:|---:|---:|---:|
| 10 | 20 | 20 | 0.302 ms |
| 40 | 80 | 80 | 7.460 ms |
| 100 | 200 | 200 | 103.298 ms |

A second run after adding connected-chain fixtures measured 114.537 ms for 100
independent branches. Debug-build variation is therefore material, but the
scaling conclusion is unchanged.

One-component chains measured 0.180 ms for 10 segments, 5.316 ms for 40 and
70.534 ms for 100. Component partitioning cannot accelerate a genuinely
connected chain by itself; local sparse structure or incremental factorization
is the later optimization for that case.

Constructing and partitioning a prototype mobility graph with 2,000 points and
1,000 independent components takes 4.1--4.7 ms. For the 200-point solve case the
graph overhead therefore projects below 0.5 ms. Solving 100 two-variable local
systems should remain in the low single-digit millisecond range instead of the
measured 103 ms global solve.

The first production optimization separates equation convergence from DOF/rank
analysis. Point coordinates, a driving dimension value, and removal of a
constraint can be transactionally solved and fully residual-verified without
recomputing a numerical rank that the operation does not consume. Adding a
driving dimension or constraint still performs rank analysis because it must
reject redundancy. On the same 100-branch debug fixture this measured:

| Interactive operation | Mean |
|---|---:|
| point drag | 4.328 ms |
| driving dimension edit | 4.171 ms |
| constraint removal | 4.391 ms |
| full solve with DOF analysis | 89.428 ms |

This is a roughly 20x latency reduction for the measured interaction paths.
It does not cache a possibly stale answer: every active equation is still
iterated and checked, and reference dimensions are refreshed before commit.
An explicit full solve remains the authority for the reported DOF count.

An exact-state, non-persisted rank cache now reuses that authoritative result
when a transaction asks for the unchanged baseline rank. Its key is the full
current ZIMA Sketch serialization, so direct changes to public geometry data,
constraints, dimensions, suppression or fixed state invalidate it without a
manual revision protocol. Numerical differentiation also restores every
variable from its saved original value instead of applying an inverse floating
point increment; repeated read-only DOF calculations therefore cause no state
drift. Measured results on 100 branches are:

| Rank-related operation | Before cache | After cache |
|---|---:|---:|
| repeated full solve | about 80--100 ms | 7.727 ms |
| add a valid constraint (before + after rank) | 176.546 ms | 87.132 ms |

Constraint insertion still spends most of its time calculating the new local
rank. Component-local differentiation remains necessary for the next reduction.

Component-coloured numerical differentiation is now implemented. Solver
variables are grouped using persisted Sketch point and geometry ownership plus
active constraint/dimension references. The same local variable position in
every independent component is perturbed in one residual evaluation. Each
equation consumes only the derivative belonging to its owning component, after
which the unchanged numerical rank calculation operates on the resulting
Jacobian. This reduced the 100-independent-branch debug measurements to:

| Operation | Before colouring | After colouring |
|---|---:|---:|
| full solve with DOF | 80.034 ms | 12.156 ms |
| add valid constraint | 87.132 ms | 20.093 ms |

The 100-segment single connected chain measured about 69 ms and therefore does
not benefit from component colouring. This is the expected boundary of the
method, not a hidden fallback: sparse differentiation/factorization inside one
large component is the next distinct optimization region.

Sparse equation-column colouring now also operates inside a connected
component. Variables that never share an equation may use the same numerical
perturbation even when a longer dependency path connects them. A hybrid policy
keeps the cheaper positional colouring for components with at most eight
variables and uses the general conflict graph for larger components. Large
Jacobian blocks below 15% density use sparse row elimination; small or genuinely
dense blocks retain the established dense pivoting path. On the 100-segment
chain this reduced the full solve again from 23.545 ms to 13.597 ms while the
full Sketcher contract suite reported identical status and DOF results.
The extended 250-segment connected-chain fixture measured 60.023 ms in the
debug build. This is suitable as the next scaling baseline; interactive drag,
dimension edit and constraint removal continue to avoid rank analysis and are
therefore governed primarily by equation convergence rather than this figure.

Point dragging now passes one or more explicit persisted point roots into the
solver.
That root is a temporary hard interaction target during the transaction: a
dimension or constraint correction propagates outward into movable neighboring
branches and may no longer pull the selected handle away from the cursor. This
applies equally to locked driving dimensions, whose value remains a hard
equation. Construction-centerline points receive a contextual stability
preference over ordinary profile points when neither side is the explicit drag
root. Tests cover a free/free dimension, a two-branch fork, centerline/profile
priority, locked dimensions, repeated forward/back motion, and circle/arc
radius invariance. For a multi-selection every selected point is a temporary
root and the gesture is evaluated from its press-time snapshot with one common
translation. This prevents incremental cursor events from accumulating drift.
Selected circular and elliptic geometry receives transient parameter equations
so a rigid group translation cannot accidentally resize it. The corrected
100-branch drag fixture measures 3.021 ms in
the debug build.

Large-Sketch interaction profiling exposed a separate quadratic lookup cost:
validation and equation evaluation repeatedly searched the point and segment
vectors by persisted ID. A transaction-scoped point index and validation-local
segment index now provide stable-ID lookup without changing persisted identity
or retaining a cache that could become stale after public-vector edits. RAII
guards remove the temporary point index on every success, conflict and
exception path. Debug measurements on 1,000 independent branches changed as
follows:

| Operation | Before indexing | After indexing |
|---|---:|---:|
| validation | 139.569 ms | 2.646 ms |
| point drag | 350.585 ms | 9.670 ms |
| driving dimension edit | 542.471 ms | 19.369 ms |

On the 100-branch fixture, drag, dimension edit and constraint removal now
measure approximately 1.4--1.6 ms. The remaining dimension-edit cost is linear
global equation convergence and is the next candidate for affected-component
execution; it is no longer hidden quadratic ID lookup.

Partitioning only the final dense elimination reduced the 100-branch full solve
from roughly 94 ms to 89 ms. This experiment establishes that repeated global
residual evaluation for every numerical Jacobian column is now the dominant
cost. The next full-solve optimization must partition or colour differentiation
before residual evaluation; further tuning of dense elimination is not useful.

The numbers are debug-build measurements and are not release performance
claims. Their scaling relationship is the relevant result.

## Compared solution regions

### A. Current procedural plus global dense Jacobian

Advantages: mature coverage of existing constraint kinds and deterministic
direct steps for several common edits.

Problems: global cubic scaling; duplicated procedural/general behavior;
operation-specific branch heuristics; a mathematically valid result need not
match the intended movable branch.

### B. One global weighted least-squares solve

Advantages: one uniform mathematical model and natural soft priorities.

Problems: retains global scaling; normal equations amplify conditioning errors;
weights alone cannot explain graph ownership or isolate unrelated geometry.

### C. Mobility components plus local weighted solves

Advantages: linear graph preparation, small numerical systems, explicit branch
ownership, useful diagnostics and natural idle-time caching. A final global
residual check prevents a mistaken partition from being accepted.

This is the recommended first production architecture.

### D. Incremental sparse factorization

Potentially fastest for very large tightly connected Sketches, but much more
complex to invalidate and diagnose. It should be evaluated only after C is
working and measured on real models.

## Mobility graph

Nodes are persisted ZIMA degrees of freedom, not OCCT topology. A point normally
owns X and Y variables. Circle/arc/corner-radius and ellipse parameters own
their explicit scalar variables. An axis has placement/direction variables only
when the data model says it is editable.

Hyperedges represent:

- intrinsic geometry relations;
- explicit Sketch constraints;
- driving dimensions and owned parameter annotations;
- fixed, Origin, axis and external-reference anchors;
- equality groups such as equal radius.

For a drag, the requested point is the root. Removing the root conceptually
exposes its incident branches. Each branch is classified by local rank as free,
partially free or anchored. Candidate movement is propagated away from the root
into free branches. This replaces rules such as "always move the second point".

## Context-dependent movement cost

Weights are preferences, not universal prohibitions. A starting policy is:

| Change | Ordinary point drag | Direct object drag/edit |
|---|---:|---:|
| dragged target error (all selected roots) | hard | hard |
| unrelated free point displacement | 1 | 1 |
| ordinary profile deformation | 4 | 2 |
| radius/diameter parameter | 1000 | 1 when directly edited |
| construction centerline/axis placement | 5000 | 1 when directly dragged |
| Sketch Origin/main axes | hard | hard unless explicitly editable |
| fixed/external reference | hard | hard |

Dimension state is two-dimensional: `driving` controls whether the value is a
solver equation, while `locked` controls whether UI/API value editing is
allowed. Both locked and unlocked driving dimensions are hard equations during
an unrelated drag. Editing a locked dimension is rejected until the same
transaction explicitly unlocks it. A reference (`driving == false`) dimension
is measurement only and cannot lock its measured value.

Absolute values require empirical tuning. Ratios and context switching are the
important parts. Multiple equal-cost candidates use stable persisted IDs only
as the final deterministic tie-breaker, never as geometric identity.

## Candidate transaction

1. Resolve the exact viewer candidate to persisted ZIMA data.
2. Load or rebuild the mobility/sparsity cache.
3. Find affected graph components from the action root.
4. Enumerate useful free-branch candidates; do not enumerate anchored branches.
5. Solve each local candidate with hard equations and weighted displacement.

6. Reject non-finite, degenerate or rank-inconsistent candidates.
7. Verify every active equation and protected parameter against the current
   uncached Sketch state.
8. Commit the lowest-cost verified candidate atomically; otherwise restore the
   exact input state and report a conflict.

## Next training batch: connected curve endpoints

The interaction layer now produces several persisted states that were not part
of the original mobility fixtures. The next solver-training pass must treat
them as first-class graph patterns rather than UI special cases.

INPUTS:

- line, circular-arc, elliptical-arc and open-B-spline endpoints connected by
  independent Coincident and Tangent constraints;
- a tangent polyline arc whose derived center is constrained to a base or
  construction axis;
- an arc endpoint coincident with a characteristic point or aligned H/V to an
  unrelated persisted point;
- rectangles whose side midpoint lies on an axis and polygons whose size and
  rotation remain simultaneously free;
- driving length, radius and angular dimensions added to each useful free,
  partially anchored and fully anchored variant.

MEANS:

- persisted point/geometry ownership and the existing mobility graph;
- exact endpoint derivatives for analytic curves and clamped B-splines;
- explicit drag roots and transactional full-residual verification;
- deterministic fixtures generated in at least the four cardinal orientations
  and both endpoint orders, so behavior cannot depend on vector order or one
  favorable side of an axis.

OUTPUTS:

- the dragged or dimension-driven root reaches its requested value whenever a
  valid branch exists;
- Coincident contacts and Tangent directions remain exact and independently
  removable;
- derived centers stay on their axes while movement propagates through the
  genuinely free neighboring branch;
- fixed or over-constrained variants reject transactionally without partial
  coordinate, constraint or dimension changes;
- serialization round trips preserve the same branch, constraint identities,
  DOF and solver status.

Sanity measurement for every generated family must include residual magnitude,
reported DOF, coordinate drift after a forward/back cycle and debug-build
latency. A visually plausible result is not sufficient verification.

### Connected-arc batch 1 result

The first four-orientation tangent-arc matrix exposed two missing mobility
transitions. A Tangent between a Segment and an Arc with one persisted shared
endpoint could only translate the complete driven curve; the shared endpoint
correctly blocked that translation, but the solver then reported a conflict
instead of rotating the free Segment arm onto the exact endpoint tangent.
Endpoint ownership now selects that analytic branch before nearest-contact
search. A fixed opposite Segment endpoint still rejects transactionally.

Dragging the other Arc endpoint may also change radius and move the shared
contact as a dependent point. Previously the connected Segment's opposite end
did not receive the same translation, so the subsequent tangent correction
preserved a newly enlarged length and a forward/back cycle accumulated
`1.489 mm` drift in the reproducing fixture. Dependent Arc endpoint motion now
propagates the same translation into connected free Segment arms before the
constraint solve. The reproducer returns below `1e-6 mm`, preserves constraint
identity, survives serialization, and covers both base axes and both sides.

The post-change debug baseline remains within run-to-run noise for connected
chains: 100 Segments measured `11.362 ms` (previous `11.820 ms`) and 250
Segments `47.229 ms` (previous `47.603 ms`). The generic 100-branch full solve
measured `9.966 ms` versus `8.959 ms`; no scaling-regime change was observed.

### Connected elliptical-arc and B-spline batch 2 result

An exactly tangent Segment sharing an Elliptical Arc endpoint was initially
reported as redundant. The endpoint tangent residual used an absolute cross
product; symmetric numerical differentiation of `|f|` at the valid state
`f = 0` produced a zero Jacobian column and hid the constraint rank. Persisted
endpoint tangency now contributes the signed normalized cross product to the
equation system. Absolute magnitude remains only a convergence criterion.

Rotating an Elliptical Arc major handle exposed a second ownership alias: the
major/minor characteristic point may also be the persisted start/end point.
Dependent endpoint propagation may therefore not skip a point merely because
the same ID is the drag root. Connected Tangent arms now preserve their
original orientation sign relative to the press-time exact tangent and map
that sign onto the new tangent. This prevents an ambiguous 90-degree rotation
from selecting the opposite branch on return. The four-step rotation/return
fixture now has less than `1e-6 mm` drift.

The B-spline matrix verifies the complementary ownership direction: moving a
free Segment end rotates the adjacent spline handle, preserves the shared
contact, returns without drift, and rejects atomically when that handle is
fixed. A repeated debug benchmark measured 100 connected Segments at
`13.024 ms` and 250 at `44.128 ms`; the earlier slower run (`15.158/66.002 ms`)
tracked whole-system load and was not reproducible as a scaling regression.

## Idle-time preparation

Allowed background work is limited to Sketch data:

- adjacency and connected-component construction;
- anchor/free-branch classification;
- symbolic Jacobian sparsity;
- local equation ownership;
- rank/DOF diagnostics;
- optional local factorization seeds.

Every Sketch mutation increments a revision and invalidates affected cache
components. Background work never changes geometry and never invokes OCCT or
dependent Part/Assembly regeneration.

## Required verification matrix

- free/free, fixed/free, fixed/fixed and external/free point pairs;
- chains, forks, loops and disconnected components;
- rectangle and polygon dimensions from every endpoint order;
- shared point used by segment, arc center/end and circle center;
- point-on-circle dragging with invariant radius;
- direct circle/arc center translation with invariant radius;
- arc endpoint dragging with editable radius, and locked-radius angular motion;
- rigid multi-selection translation with invariant internal shape;
- explicit radius edit and equal-radius propagation;
- construction axis versus ordinary profile branch;
- redundant, conflicting, zero and near-degenerate equations;
- repeated forward/backward dragging without accumulated drift;
- deterministic results under persisted-container reordering;
- latency percentiles for small connected, large disconnected and large
  connected Sketches.

## Current conclusion

Component-local weighted solving is the best measured next step. It escapes the
global dense scaling region without replacing every existing equation at once.
The current solver can remain the local numerical verifier during migration,
while procedural branch heuristics are removed as each interaction moves onto
the mobility graph.
