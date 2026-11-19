# Independent sheet branches — Windows 2026111904

## Calculation contract

Authored attachment ancestry determines material branches. A single continuation
shares its parent's branch; a fork starts independent child branches. Contact,
intersection and containment never create another connection. The partition is
derived in memory, included in calculation fingerprints and not added to the
native document schema. The existing placement solver is unchanged.

Add and Cut calculate each branch separately and collect the results without a
Boolean union between branches. Independent batch jobs still execute concurrently.
Unbend and Bend Back retain the same material contributions and branch ownership.
Form treatment retains its existing parent material. Line Bend deforms independent
branches in separate parallel jobs and retains branch-specific material regions
and immutable deformation snapshots for subsequent cuts and state changes.
The same C++ implementation serves Windows and Linux.

Two publication defects found during verification are also repaired: direct
batch Cut now publishes its named current boundary, and compound sheet groups
complete the ancestry of vertices created where their panels join. Downstream
attachment consumes current edges, faces and endpoints rather than an untrimmed
original operand.

## Executed Windows checks

| Check | Evidence and result |
| --- | --- |
| Six spatial relationships | Separation, point/edge/face contact, overlap and containment pass. Two authored sheets retain two solids and additive volume 10,200 mm³, with both owners' six faces and twelve edges. |
| Two consecutive cuts | All six relationships pass complete-history and final-first batch paths. Independently computed removed volumes and four/six surviving solids agree; both material skins retain ownership. The parallel job counter increases on the batch path. |
| Common-parent branches | Two adjacent Bends and their Flats retain three solids through extensions 2, 3, -1, 0 and 3 mm. A positive intersection volume proves the overlap case is real. Expected volume is checked independently. |
| Dependent attachments | 36 native cases pass: two branch walls, two material skins, Flat/Bend/Profile, first/last/midpoint anchors. Exact selected identities survive Undo/Redo, warm regeneration and native save/reopen plus cold regeneration. |
| Branch GUI | 24 native Qt interaction cases pass: both walls and skins, Flat/Bend/Profile, endpoint/midpoint. Hover, cycling, click, dimensions, OK, reopening and Cancel are exercised. |
| Trimmed-edge GUI | 12 cases pass on two current cut fragments, with Flat/Bend/Profile and endpoint/midpoint entry. The fixture is a local copy; no user source file was overwritten. |
| Profile GUI | Dialog, attachment, dimensions, Tree, Explode and Undo/Redo pass. |
| Transition with a dependent Bend | Current-rim attachment, source edit, native reopen and cold calculation pass. Unbend/Bend Back preserve the two branches produced by that fork and their material volume. |
| Line Bend across overlapping branches | Two overlapping 100 × 80 × 2 mm sheets retain two solids and 32,000 mm³ within the existing 0.01 mm³ deformation budget per sheet. Parallel execution, warm/cold calculation, saved packets, Unbend and Bend Back pass. A subsequent 4 mm slot retains four solids and 30,720 mm³ through both state changes. The original combined path incorrectly reduced the initial material to about 18,000 mm³. |
| Existing native behavior | Flat, Bend, Sheet Profile, Sheet exchange/from Body, sheet state, sheet state commands, Form geometry/commands and model calculation suites pass. |
| Packaging guards | Sixteen packaging unit tests pass with two existing platform-specific skips. Missing or failing mandatory sheet tests reject packaging. |

The eight native branch cases, GUI case, Sheet exchange and Transition command
suites form the mandatory release label. Windows and Linux builders require all
eleven registered cases and stop
before archive assembly if any are missing or fail. Archive acceptance must rerun
these tests against the exact committed release source and binary.

Representative local inspection of the saved touching-wall 18.prtz copy passed
three warm/cold cycles and native reopen: six independent jobs per cycle,
approximately 258–266 ms warm and 217–232 ms cold. Measured volume was
94,739.152847966427 mm³ versus the saved 94,739.152847966412 mm³. These are finite
workstation observations, not universal performance claims.

## Scope and remaining limits

An older 17.prtz copy contains references to fragments created by the previous
cross-branch union. Those references do not identify the new independent branch
boundary. On 2026-11-19 the user explicitly accepted creating new test models
rather than adding compatibility workarounds. The old copy remains a diagnostic
artifact; it is not silently retargeted or rewritten. Missing references retain
the established invalid-dependency/last-valid-frame behavior.

This record does not claim every possible multi-branch deformation, trimming
arrangement or mouse gesture is exhaustively verified. The multi-branch Line Bend
fixture has a shared line extent on two overlapping sheets; arbitrary dissimilar
branch extents and every downstream attachment combination are not covered by
that fixture. Existing single-branch Line Bend variants also passed. Linux
execution is pending on the Linux host; dependency versions and shared source
are unchanged.

Localization review found no new product text: existing translated validation
messages are reused. Only test diagnostics, comments and English documentation
were added. The previously verified five-language catalogs remain unchanged.

The proposed correction of a generated Flat's end through View dimensions,
including target angle and material-thickness clearance, is design only. It is
not implemented in this build.
