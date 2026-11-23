# Offset contacts and virtual rounding verification

Implemented for the Render release candidate on 2026-11-23. Windows tests use
the shared C++ implementation; native Linux execution remains unverified.

## Cause and repair

Circular Offset outputs are stored as dependent rational curves. Tangency now
recognizes their exact circular support, including the finite angular domain,
and evaluates the current source during the solve. It preserves the Offset and
its reference identity. No sampled polyline substitutes for a native circle.
Numerical derivative grouping includes the source variables, and the bounded
circular seed excludes every externally owned point/radius. An unrelated derived
curve does not disable a supported circular fit; unsupported equations still
reject the fit. Editing an Offset with dependent contacts solves those contacts
inside the same atomic edit.

A pre-existing real point with C on both supports is reused when T is added.
Removing T retains that point and its independent contacts. Three tangencies
use the ordinary native circle/arc equations, including immutable Offset supports.

Virtual rounding recognizes either a shared native endpoint or an explicit
retained trim anchor ending at the other curve's endpoint. The latter keeps both
authored point identities. Coincident coordinates alone do not establish that
junction. The repair covers the supplied Offset/arc corner; ordinary arc/arc
rounding uses the existing exact curve-fillet implementation.

## Finite matrix

| Cases | Verification |
| --- | --- |
| 16 native circle/Offset cases | Both selection orders and offset sides, R/diameter, locked/unlocked drivers; three target-size, source-radius and offset-distance edits, three actual center drags, independent equations, native round-trip and T removal |
| 8 existing-contact cases | Both selection orders, internal/external contact, circle/finite arc Offset; reuse the same real point and two C relations, successive size changes and finite-domain equations |
| 48 three-contact cases | Native/Offset circles and finite arcs, both pair selection orders and all six support permutations; independently known center/radius, fully constrained target drag rejection without mutation and round-trip |
| 4 projected-source cases | Exact external arc -> owned reference profile -> Offset; both selection orders and trimmed/untrimmed support, three radius edits/drags, reference diameter, immutable source, impossible offset edit rejection and round-trip |
| 2 explicit trim junctions | Both selection orders, four successive virtual radii, preserved authored points/Offset and a single stable treatment, independent contact radii and evaluated save/reopen |
| Reported Part copy | Four circular Offset/circle pairs and six virtual Offset/arc fillets pass; the original document remains untouched |
| Offset GUI matrix | 16 cases, 26.54 s: both selection orders, existing C/new contact, native/external circle/arc source, R/diameter value editor, repeated center drags, unchanged external sources, atomic Undo/Redo and save/reopen |
| Curve-fillet GUI matrix | 38 cases, 134.24 s: Part/Assembly, five languages, two themes, reference entry/inspection/replacement, resizing, Cancel, native and virtual creation, manual R/diameter changes/drags, Undo/Redo and reopen; includes arc/arc and virtual Offset/arc |

The 78-case native matrix passed in 0.99 seconds after rebuilding the final
solver. The six dependent native suites also passed (61.62 seconds total):
native tangent contacts, native arc axes, Offset geometry, the 400-case curve
fillet contract, double-circle contacts and copied-circle contacts. Projected
point positions use a 1e-12 tolerance for trigonometric endpoint reconstruction;
their identities and flags remain exact.

Coverage is finite: mixed internal
branches of all three supports, every possible dimension combination and arbitrary
noncircular Offset/spline tangency are not established by this matrix. The two
straight-Offset contact attempts in the private diagnostic remain unsupported.
The new analytic support specifically covers circular Offsets. Neither a success
on the supplied Part nor the matrix claims universal solver convergence.

## Hole Properties after an invalid predecessor

Also reported on 2026-11-23: a Part retains visible calculated material with an
uncalculated Extrusion preceding a threaded Hole. Opening Hole Properties then
removes all material from the View, leaving only construction geometry and the
Hole preview. This defect is repaired in the Render release candidate. Rollback
materializes the actual last valid prefix through the existing recovery path;
it does not substitute the final Body or calculate material from the broken
definition. Native and GUI checks cover a synthetic case and an isolated copy
of the supplied Part, repeated opening, unchanged OK, changed-parameter Cancel
and revision/calculation invariants. See the
[verification record](verification/20261123-render-and-hole-properties.md).

## Holes axis endpoints and properties layout

The Render candidate also places the Holes Sketch button beside **In View**,
using the existing shared work-plane row. Its drilling axes now expose both
authored endpoints through the ordinary profile-centerline reference mechanism.
Endpoint positions use the Sketch segment limits, excluding the decorative axis
overhang. Native checks cover diameter edits, segment reorder, construction
geometry, rotated/translated planes, reversed blind holes, Undo/Redo and native
cache/fresh regeneration. GUI checks select each endpoint with the Point command,
including Cancel, dependent-point save/reopen and Undo/Redo.
