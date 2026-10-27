# Native viewer cache, document close and redesign gate

## Scope and representative data

The reported private fixture is `ZE0176-0101-0000_SKELETON.prtz`, a one-Body
Skeleton with 26 calculated operations, dense external references and a large
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
are rewritten as v2 on Save.

## Immediate close detachment

Closing removes the complete document family from `Workspace` before destroying
its immutable native history. The desktop host then retires that detached state,
plus the View's old display/reference meshes and picking indexes, on the Qt global
thread pool. No worker touches Qt widgets, the Workspace or mutable document
state. Hosts that do not provide a release callback retain synchronous
destruction. A contract verifies that a top-level Assembly and its hidden source
family are all detached and released.

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

Native Linux compilation, execution and timing remain unverified on this Windows
host. Windows package publication has separate committed-source and signed-smoke
gates.
