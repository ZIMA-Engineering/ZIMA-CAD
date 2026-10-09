# Form copies and measured Boolean batching

## Scope

Windows/MSVC Release, shared C++ and OCCT 8.0.0. The baseline for the corner
copy repair is `dca3fe035968e92caf9d22b9b3a60a9a65302380`. Native fixtures use
real Sketch/profile features. Personal `Projects/12.prtz` remained unchanged:
SHA-256 `83bc4bb81de4390213a31619fee60842215072dbac6de85e11cfd7556faa109b`.

## Accepted ordinary Pattern optimization

Prepare all transformed operands and aggregate their native face, edge and
point references in one compound. Disjoint copies enter the supporting Body
Boolean together. A 50-occurrence Pattern has 49 copied operands: this avoids
48 intermediate operand fusions before the final Body Boolean. Exact surface
bounds, including shape tolerances and the existing Boolean tolerance, gate
this path. Overlapping or touching bounds keep the sequential implementation.
No tessellation-based bounds, reduced precision or new persistent IDs are used.

| Cold calculation | Sequential | Batched |
| --- | ---: | ---: |
| 50 additive occurrences | 2,872 ms | 1,434 ms |
| 50 subtractive occurrences | 2,412 ms | 1,122 ms |
| Six additive occurrences | 178 ms | 180 ms |
| Six subtractive occurrences | 168 ms | 100 ms |

These are individual serial observations on the same host, including complete
native calculation, not hardware-independent thresholds. The large comparisons
ran with batching first; the small comparisons ran with sequential calculation
first. Overlap and touching fixtures intentionally select the existing path.
Both additive and subtractive fixtures compare strict BRep validity, volume,
area, centroid, inertia, complete topology-reference identities and sheet-side
metadata, axes and two-way geometric differences. Native command and GUI tests
cover reference editing, locking, source ownership, save/reopen and Undo/Redo.

## Rejected corner Form batching experiment

Three copied corner tools were collected for one Cut and one Fuse, replacing
six sequential Booleans. Exact topology/side identities, axes, mass properties
and two-way geometric differences passed. Complete cold calculation nevertheless
measured 8,188 / 9,844 ms (sequential / batch), then 8,618 / 8,820 ms with the
order reversed. Exact mass-property and edge-packet processing also contribute
to this operation. The experiment did not establish a speed improvement and
was removed from the production implementation.

Form copies retain sequential cut/add ordering, including its meaning for
overlapping features. The accepted corner repair instead removes redundant
delta Booleans and reuses a complete unchanged settling result while preserving
the existing input-fingerprint, original-reference and region guards. The
reuse measurement includes serialization for an exact packet comparison; it
must not be reported as pure kernel evaluation time.

## Personal-file interpretation

`12.prtz` contains an independently stored 24 mm definition at X=0 and a 16 mm
definition at X=60. The default 20 mm spacing overlaps copies of the larger
definition and eventually intersects the different existing definition at X=60.
Rejection must remain atomic when this separates material. A single mirrored
large definition about YZ at X=15 succeeds; an early combined diagnostic failed
during its intervening count increase, before Mirror was reached.

Raw diagnostics are retained under `build/form-diagnostic/`, including
`corner-batch-four-comparison.log`, `corner-batch-four-reverse.log`,
`ordinary-batch-large-add.log`, `ordinary-batch-large-cut.log` and GUI/native
acceptance records. Linux performance and native/GUI execution are unverified.
