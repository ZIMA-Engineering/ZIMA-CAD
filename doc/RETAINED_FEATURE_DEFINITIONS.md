# Retaining failed feature definitions

User agreement: 2026-10-07. A constructor must be able to keep a newly authored
feature when its geometry calculation fails, so its complete definition can be
edited and shared for diagnosis.

Explicit feature-definition commits validate ownership, identities, supported
parameters, locks and required input definitions using their established paths.
A new definition, including conversion of a standalone Sketch, then uses the
existing recovering history calculation. A kernel failure retains the history
entry, owned Sketches, parameters and source references in the native Part.
Calculation diagnostics remain attached to their feature IDs. The Tree uses its
existing red not-calculated state, and View displays the preceding valid geometry.
Dependent operations remain blocked; independent Body branches retain their
ordinary recovery behavior.

An already failed feature accepts successive definition corrections and can be
repaired without recreating its identity. Unchanged OK remains a no-op. Editing
an already working feature retains its established atomic rejection behavior;
an unsuccessful edit does not replace its working definition or geometry.
Structural validation failures still reject the pending command before commit.
Sketch driving edits, Body combinations, relation evaluation and regeneration
keep their established validation policies.

The shared authoring policy is consumed by profile/primitive and Sweep features,
edge treatments, Shell, openings, sheet features and surface commands. Imported
file decoding retains its independent input validation. Calculation and reference
resolution continue using the same shared Windows/Linux implementation.

## Cuts with no material change

A cut first validates and calculates its tool. With no input material it leaves
an empty Body and retains the calculated source reference geometry; it does not
introduce the tool as additive material. A cut that misses existing material also
remains a valid history entry. Its Sketch and generated tool must be valid.
The ordinary profile dialog permits choosing Cut before material exists.
Sheet Cut also retains a valid no-op definition when its calculated projection
misses existing supported sheet sides, preserving earlier material cuts. An
unsupported input body or invalid tool still produces its ordinary diagnostic.

## Verification status

The native regression covers an empty-input cut, a disjoint cut, cold native
reopening, failed Fillet creation, successive corrections, parameters and exact
preceding geometry, dependent blocking, repair and Undo/Redo. It also verifies
that unsuccessful edits of working features retain atomic rejection. The GUI
matrix covers creation OK/Cancel, red Tree state, Properties, unchanged OK,
repair OK/Cancel and native persistence. Both focused native and Windows GUI
contracts passed. The GUI also checks that opening a constant treatment does
not append empty optional route endpoints or create an unchanged OK transaction.
Dependent Windows contracts also passed for history editing, Sweep 2D/3D and
Helical Sweep, Shell, holes, openings, shaft threads, Bend, Flat, solid-state
definitions and Boundary Surface. Sweep checks preserve input-owner and missing
source-point rejection before adopting Sketches. Sheet Cut checks a miss,
unchanged earlier cuts and Undo/Redo. Hole storage-order checks require exact
original reference identities. These command checks do not constitute an
exhaustive GUI creation/failure/repair matrix for every supported feature.
The combined console GUI check passed on Windows (168.34 s) after its outdated
confirmation, unit and library-Sketch assertions were aligned with established
native Part ownership. The final focused feature-definition GUI check also
passed (4.37 s). Release packaging verification is recorded separately.
Linux execution remains a separate host verification gap. Persisted diagnostics
already belong to the native Part; this change introduces no sidecar or format.
