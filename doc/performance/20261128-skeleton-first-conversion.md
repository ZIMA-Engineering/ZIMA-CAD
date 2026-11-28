# Skeleton-first Sheet from Body investigation

Status: Windows 2026112803 candidate; not included in Windows 2026112802.
The immutable released implementation is documented in
[the boundary graph record](../verification/20261128-sheet-boundary-graph.md).

## Purpose and boundary

The requested experiment prepares the exact nominal attachment skeleton before
calculating generated sheet solids. Its inputs are the current native source,
selected skin, RIP definitions and conversion parameters. Its output must be
the same independent Flat/Bend material, native reference ownership and editable
history as the accepted converter, with stable unfolding and ordinary attachments
after partial or complete Explode.

The existing converter obtains each dependency wave's attachment geometry by
calculating intermediate solids. The candidate instead derives finite sections,
native endpoints and analytic supports from authored Flat and Bend requests,
settles reference frames without material calculation, and calculates the final
material after freezing the nominal graph. Exact reconciliation with calculated
native attachments remains mandatory before returning the result.

Both material sides have distinct identities. A Bend's terminal rectangle comes
from its existing authored request, including corner correction; display
tessellation and interior sweep samples do not define this boundary. Resolution
also retains the immutable source reference context, so constructing the new
skeleton does not invalidate references in the source history.

Nominal boundaries and local correction values remain separate. Zero is a valid
initial correction, not an instruction to discard authored corrections or to
rebuild the source graph after every local edit. No shared placement solver or
native format contract has been changed by this experiment.

## Initial measurements

On the isolated complex RIP box, three serial preparation runs were:

| Mode | Run 1 | Run 2 | Run 3 | Native history evaluation calls |
| --- | ---: | ---: | ---: | ---: |
| Accepted converter | 28.3912 s | 29.2648 s | 28.4344 s | 210 |
| Initial skeleton prototype | 9.31871 s | 8.87521 s | 9.01053 s | 51 |

These measure reconstruction through the shared conversion preparation function,
with the source already calculated. File loading/saving, GUI events and rendering
are outside the timed interval. The counter includes nested operand evaluation;
it is not a count of complete user commands. These samples precede subsequent
terminal-section and source-context refinements and must be repeated for any
production candidate. Initial material comparison, cold regeneration, local
corrections, unfolding and release passed on this finite fixture.

The final implementation, including complete source-reference publication, was
measured again in three paired serial runs on the same complex RIP box:

| Mode | Run 1 | Run 2 | Run 3 | Native history evaluation calls |
| --- | ---: | ---: | ---: | ---: |
| Materialized preparation | 33.0598 s | 27.9511 s | 30.8599 s | 210 |
| Boundary preparation | 9.88302 s | 9.51569 s | 11.8988 s | 51 |

Median preparation time fell from 30.8599 s to 9.88302 s, approximately 3.12 times
faster (68% less elapsed time). The timed boundary and counter have the same scope
as above; this is not a claim that every complete GUI command is three times
faster. The existing focused edit gates separately cover corrections without
rebuilding the conversion. Raw final samples are in
`build/sheet-skeleton-first/final-timings.json`.

## Box verification

The user requested rectangular, trapezoidal, chamfered-corner, drafted,
drafted-chamfer and drafted-asymmetric boxes. The matrix compares ordinary and
prototype preparation on the same saved source, then edits both endpoints of
both Bend Sketches repeatedly, checks unaffected definitions and exact endpoint
equations, saves/reopens, performs Undo/Redo, unfolds and releases the conversion.
Initial, corrected and unfolded native results are compared independently.

Two assumptions in the initial extension of the focused test were inappropriate:

- A multi-branch box can legitimately invoke nested operand construction, so the
  existing simple fixture's one-call limit is not a universal bound. Its original
  release gate remains unchanged. The new matrix records and compares calls for
  the same fixture and cache state.
- Folded and unfolded volume need not be identical for a non-midplane neutral
  axis or a variable-width corner closure. Returning corrections to zero passed;
  the failing assertion was the later blanket folded/unfolded volume comparison.
  New box cases preserve their developed result through release and compare that
  result against the baseline instead. The original constant-section fixture
  retains its volume equality gate.

A separate analytical strip test checks a 60 mm wide, 2 mm thick, 90-degree Bend
with inner radius 4 mm at K = 0.318309886184 and K = 0.5. For a constant or linearly
varying width, developed volume is the base Flat volume plus average width times
thickness times angle times `(inner_radius + K * thickness)`. Folded volume uses
the mean material radius instead. All four zero/corrected cases passed to
1e-5 cubic model units. This is geometric allowance evidence, not certification
of a particular material or press/tool combination.

Whole-compound Boolean comparison proved unsuitable as the only oracle for
touching independent branches. On a valid six-solid case, both self-differences
were empty, but cross-compound Cut reported a large difference while every
individually matched branch had empty two-way difference. Comparison therefore
requires a bijection of valid branch solids, matching centroids, a dimensionally
consistent integral-volume bound and empty two-way material differences. It
also checks actual native edges and trimmed surfaces in both directions at
1e-7 mm, then repeats after cold regeneration. Display tessellation is not used.

A second diagnostic exposed a broad-phase distance limitation: a point on one
trimmed planar face reported a distance of 0.000596 mm even to its own solid.
The same value occurred against the other preparation's solid. Independent
projection onto exact native surfaces, with their trimmed UV-domain membership,
resolved this false comparison failure without changing the model or tolerance.
The asymmetric unfolded box then passed, with maximum checked separation
2.81e-8 mm. The six earlier corrected unfolded specimens had zero pairwise
branch-overlap volume. The integrated matrix adds nominal unfolded specimens.

No kernel deformation or fitting precision has changed. An exploratory 1e-6 mm
Unbend request failed the existing surface-fitting bound in the materialized
baseline itself; this investigation does not claim that stricter precision is
supported for every variable-width corner. Manufacturing evidence here concerns
geometry, allowance equations and non-overlap, not a specific tooling process.

The user's subsequent folded-material audit checks all 24 retained automatic
specimens using native pairwise solid intersections. All six nominal folded
conversions and all twelve nominal/corrected unfolded specimens have zero overlap.
The six folded stress specimens deliberately apply a 0.3 mm first-Sketch endpoint
correction and a 0.2 mm second-Sketch endpoint correction. Their material overlaps
are 0.0126448 mm3 (rectangular), 0.00694717 mm3 (trapezoidal), 0.0657098 mm3
(chamfered), 0.013879 mm3 (drafted), 0.0875908 mm3 (drafted-chamfer) and
0.0804373 mm3 (drafted-asymmetric). These are actual intersections, not merely
coincident display lines. The geometric equivalence matrix also compares these
same corrected branches against materialized preparation. Arbitrary corrections
are not a guarantee of collision-free folded material; the independent-branch
contract permits intersections and does not authorize silently clamping the
authored correction or changing its neighbor. The review copies retain these
stress cases explicitly. Nominal folded non-overlap is now a required assertion
in the six-box release gate, alongside the existing unfolded checks.
The user reviewed the scale of these small corrected intersections and accepted
them as a known condition, not a release blocker. Branch independence and working
unfolding remain mandatory. This acceptance does not relax numerical tolerances,
merge overlapping branches or certify a particular press-forming process.

## Ordinary feature investigation

Existing sheet batching already prepares independent operands on private kernels
and collects them in authored order. Ordinary features can use the same principle
only after their actual inputs are resolved. An exact frame does not replace the
preceding material required by Fillet, Chamfer, Shell or geometry-dependent limits.
The earlier failed fused-history experiment remains rejected; see
[the prior history-cache investigation](20261028-background-analysis-and-history-cache.md).

A synthetic probe prepared 32 independent circular Extrusions with eight private
kernels, then used the same existing Cut operation. Median preparation time was
0.0951376 s serially and 0.0374337 s in parallel; preparation plus Cut was
0.1882728 s and 0.1362241 s. Analytical volume, exact native reference packets and
two-way material differences passed. This measures a resolved operand phase,
not an end-to-end linked-feature command, and does not enable a general parallel
history scheduler.

## Integration and acceptance

The shared workspace preparation has Automatic, Materialized and BoundarySkeleton
policies. Normal commands use Automatic; explicit policies support regression
and performance comparison. The actual authoring algorithm remains shared.
Automatic derives the boundary first for supported native polygonal Flat/Bend
chains. Imported sources, transformed target Bodies and inferred continuations
retain materialized preparation. If a lightweight boundary cannot be prepared
or exactly reconciled, the private draft is discarded and materialized
preparation supplies the authoritative result or existing error. This does not
introduce a second native format or a legacy loader.

`zima_cpp_sheet_boundary_skeleton_contract` is a required sheet-branch CTest gate.
Both platform packagers require its registration and successful execution. It
creates all six source boxes natively, verifies that Automatic actually uses
boundary-first preparation and removes intermediate evaluations, checks both
Sketch corrections and transaction/persistence contracts, and compares initial,
nominal unfolded, corrected and corrected unfolded solids against materialized
preparation. It checks cold definition identity and developed branch overlap.
The existing focused correction and constant-section volume gates remain intact.

The first integrated six-box matrix and the previous twelve sheet gates passed.
A subsequent native GUI audit found that the initial integration lost exact
source-history reference packets after conversion, even though the authored
placements remained valid. The complete-publication policy had omitted explicit
reference demands, and the resolving wrapper did not forward that policy.
The candidate now preserves those demands and uses the normal resolving wrapper.
The placement solver itself is unchanged. A focused native regression covers
cold/cached and resolved/raw publication with and without Bodies. The actual
complex-box GUI checks source Tree reference state at open, Cancel, empty-boundary
OK, Undo/Redo and material conversion OK. It passes, as does native validation of
the saved result after mouse corrections. Its measured confirmation was 6.47222 s;
this is a single GUI observation, not the paired performance result.

The final development six-box rerun, the previous twelve sheet gates, the focused
model-calculation suite and four integrated sheet GUI scenarios passed. The last
test-only addition makes nominal folded overlap an explicit matrix assertion;
the committed candidate must execute the complete thirteen-gate matrix again.
Four GUI scenarios cover complex-box corrections, ordinary conversion,
same-Body conversion and converted-feature selection. The drawing contracts and
five-language coverage also pass; see the
[drawing record](../verification/20261128-drawing-dimension-placement.md).
The user requested manual sheet-mechanism review after all work is complete and
before GitHub publication. A verified candidate must be handed over for that
review; publication is on hold until then. Native Linux execution is unavailable
on this Windows host. No user-facing preparation labels or messages
were added; preparation diagnostics are internal and the automatic fallback
retains established localized errors. The protected placement solver, document
format, templates, factory native assets and development launcher are unchanged.
Local investigation evidence remains under `build/sheet-skeleton-first/`.
