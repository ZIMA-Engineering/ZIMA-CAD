# Background physical analysis and reversible history cache

## Scope

This change addresses two measured sources of avoidable latency in large native
Parts and Assemblies:

- exact mass, area, centroid and inertia integration extending an otherwise
  complete modeling command or explicit regeneration; and
- rebuilding and reserializing native continuation state after returning to an
  earlier parameter value, including Undo/Redo.

The implementation remains shared between Windows and Linux. It does not change
the native document format, geometry tolerances, modeling precision, stable
references, side identity, oldest-face ancestry or Undo/Redo transactions. No
user-visible text was added, so the five localization catalogs are unchanged.

## Implemented behavior

### Geometry-critical calculation

Part and Assembly regeneration no longer requests exact aggregate physical
properties merely because Relations expose `model.mass` or because a Body
Properties row exists. Calculated physical values remain output-only; the
Relations dependency validator continues to reject direct and transitive use of
those values as geometry drivers.

A Body Properties snapshot still requests exact analysis synchronously when its
centroid origin is an actual input to later geometry. This preserves the
established dependency contract. Otherwise geometry is committed with physical
analysis marked pending.

Opening an existing Body Properties inspector no longer publishes a complete
scene immediately before replacing it with the inspector's history-input scene.
The inspector publishes that exact input directly.

### Idle background completion

After 1.5 seconds without another scene publication, the GUI schedules exact
physical analysis at low thread-pool priority. The worker owns a private
document/cache snapshot and a private OCCT kernel. Publication occurs on the UI
thread only when runtime identity, document revision and data generation still
match. A stale result is discarded and rescheduled; it never changes geometry or
creates an Undo transaction.

Part publication merges only derived physical fields and Body Properties
results. Assembly publication analyzes current source snapshots without solving
mates or replaying Assembly operations. A matching exact source analysis is
retained when ordinary source refresh publishes the same geometry fingerprint.
Save and explicit inspection remain exact synchronous fallbacks.

### Reversible continuation cache

The OCCT kernel now retains a bounded in-memory map of native continuation frames
by exact authored-prefix fingerprint. Returning to a previously visited value
reuses its already validated frame instead of creating a numerically equivalent
but byte-different B-Rep archive. The existing fingerprint, native binding and
reference checks remain the authority. The cache is disposable and is not
required to reopen a native document.

### Exact-radius Thin profile

An inward Thin offset exactly equal to a tangent corner radius may legitimately
collapse that source arc to a sharp vertex. OCCT then reports no generated edge
for the arc. The evaluator now accepts that collapse only when the remaining wire
is connected, closed when required and B-Rep-valid. Split edges and invalid or
empty offsets remain rejected.

## Representative protected-data validation

All protected-data checks used an isolated local copy. The original project file
was not modified, committed or included in a package.

The representative automotive Skeleton contains 32 history operations. On the
test workstation:

- native load was approximately 7.4 seconds;
- a cold explicit regeneration completed in approximately 68.9 seconds after
  physical integration was removed from the geometry-critical path;
- regeneration followed by exact Save analysis completed in approximately
  101.3 seconds, placing roughly 30 seconds of exact analysis outside `OK`;
- editing the last Thin operation from 5.0 mm to 5.1 mm completed in 17.27
  seconds including the 7--10 second process load; and
- editing an existing late Solid Fillet from radius 10 mm to 9.5 mm, followed by
  Undo and Redo, completed successfully. The complete process took 36.25 seconds
  including load; the dependent suffix remained approximately 26 seconds.

The Solid Fillet check preserved the native source file and did not save the
temporary edit. The exact-radius Thin regression independently covers both
windings, both normal directions, Extrusion and Sweep, analytical volume, stable
face identities, a valid offset beyond the collapsed radius and rejection beyond
the profile inradius.

## Parallel-history boundary

`Up To` is a dependency edge, not a universal serialization barrier. Fillet,
Chamfer and Shell already execute through their own explicit evaluator branches.
However, the existing experimental multi-body fuse route is not enabled for
ordinary production history. A representative late-range probe failed because
an intersection source face was absent at its required input boundary. More
generally, a faster fused result is not acceptable until it proves the same edge
aliases, oldest coplanar face identity, exact feature-input boundaries, source
operands, save/reopen data and Undo/Redo behavior.

The safe next step is a branch-local scheduler that executes independent operand
construction concurrently but publishes each feature boundary in authored order.
Fillet, Chamfer, Shell, body combinations and any operation consuming exact
preceding topology remain dependency barriers for that branch. This is the same
release gate recorded in
[`20261027-native-cache-close-and-redesign-gate.md`](20261027-native-cache-close-and-redesign-gate.md).

## Cancellation boundary

Immediate thread termination was not added. Current geometry tasks capture
private candidates whose lifetime is joined by the command, and monolithic OCCT
calls do not yet receive a cooperative progress token. Killing that worker could
outlive captured state or interrupt OCCT while it owns transient topology.

A safe Escape implementation must propagate OCCT `Message_ProgressRange` (or an
equivalent cooperative token) through every long algorithm, stop at explicit
checkpoints, discard the private candidate and preserve the live document and
Undo history. Until that is implemented, process or thread termination is not a
valid cancellation mechanism.

## Verification

Focused Windows Release checks passed for:

- model calculation, source refresh and Assembly cuts;
- Save/Save As physical analysis, Relations, no-op behavior and concurrency;
- Part and nested Assembly Body Properties, persistence and Undo/Redo;
- deferred exact face measurement and byte-stable value restoration;
- Thin/Up To Extrusion and Sweep contracts;
- native history deletion/recovery and bounded cache eviction;
- Opening, component Opening and target Opening commands;
- operation activity, edge-treatment GUI paths and translation coverage; and
- the synthetic Skeleton activation/context-menu/Up To workflow.

Linux was not built on this Windows host. The implementation contains no new
platform branch; Linux verification remains a release handoff item.
