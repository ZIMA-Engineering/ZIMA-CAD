# Mandatory sheet-metal branch regression gates

Independent sheet-metal branches retain their geometry and references even when
they touch or intersect. This is the modeling contract in AGENTS.md, confirmed
on 2026-11-19. The tests below are release requirements, not evidence that every
case already passes. An applicable failure or verification gap blocks release.

## Required matrix

| Dimension | Required cases |
| --- | --- |
| Spatial relation | Separation; point, edge and face contact; partial volume overlap; one branch inside another |
| Authorship | Flats and Bends from one common parent; compound Sheet Profiles; Sheet from Body |
| Contact side | Both material skins, opposite orientations and meaningful +0/-0 offsets |
| Selected support | Original boundary; each surviving fragment after trimming; first endpoint, last endpoint and midpoint |
| Dependent feature | Flat, Bend and Profile on each branch |
| Changes | Repeated extension/width/angle edits through separation, contact, overlap and back; changed upstream dimensions |
| History | Warm incremental calculation; explicit Regenerate with a live kernel; fresh-kernel full calculation |
| Persistence | Native save/reopen using saved packets without calculation; subsequent cold regeneration |
| Transactions | Undo and Redo of creation and successive edits; rejected edits and Cancel leave the document unchanged |
| Sheet state | Applicable Unbend and Bend Back preserve separate branch ownership and material |
| Parallel calculation | Assert actual independent branch jobs execute on the parallel path; compare their output with complete sequential history and record representative timings |
| Interaction | Actual hover and click identify the selected branch; Tree synchronization; Properties and dimension editing; replacement and inspection of references |

Use deterministic fixtures with independently known dimensions, material volumes
and expected owners. Check both branches, not only the most recently added one.
Compare unaffected branch geometry and reference keys before and after each
action. Check exact current trimmed supports rather than accepting an original
untrimmed fallback. Include the saved user failure as an isolated local copy;
never overwrite the original or make a private user file a CI dependency.

Assertions must detect an unintended union even if all placements report valid.
Check preserved material contributions, independently identifiable branch edges
and faces, stable side choices, dependent frames and their authored references.
Warm and cold paths must agree. Saved packets must already contain the necessary
reference data; a fresh calculation must not be required merely to repair them.

## Execution and release policy

Register deterministic native cases in the standard CTest suite with a dedicated
sheet-branch label. GUI acceptance uses the existing native verification harness
and must record actual input, selection and document revision checks. Required
cases must not depend on a developer-only environment variable to enter the
native suite. Release validation must run the applicable gates and retain their
results against the exact source commit and binary.

Do not mark known failures as expected, disable tests or relax identity checks.
Document any unsupported case explicitly and keep release blocked until the
agreed behavior is implemented and verified. Record Windows and Linux execution
separately; shared source alone is not evidence of execution on both platforms.

## Current evidence

The boundary-first conversion candidate adds
`zima_cpp_sheet_boundary_skeleton_contract` as the thirteenth required gate.
It creates rectangular, trapezoidal, chamfered-corner, drafted,
drafted-chamfer and drafted-asymmetric sources and compares automatic boundary
preparation against materialized preparation. Required checks include actual
use of the optimized path, fewer history evaluations, both Bend Sketches,
native save/reopen, cold identities, Undo/Redo, ordinary attachments after
Explode, allowance equations and nominal/corrected unfolded branch overlap.
Both packagers reject an absent or failed gate. Its acceptance status and the
limits of the geometry comparison are recorded in the
[boundary-first investigation](performance/20261128-skeleton-first-conversion.md).

The same boundary gate also covers repeated inherited thickness edits on
corrected rectangular and asymmetrically drafted boxes, fixed conversion
thickness, source solid Fillet edits, native wall thickness, correction identity,
Undo/Redo, cold reopening, Unbend/Bend Back and Explode. These material-input
changes must invalidate boundary reuse even when source geometry is unchanged;
see the [material-default regression](verification/20261128-sheet-material-defaults.md).

Build 2026111903 is withheld: the saved 18.prtz case still contains invalid
Tabule 03 and Ohyb 03 placements after touching branches. Earlier local-face
attachment tests did not establish this independent-branch contract. The broader
matrix above is required for the repair; passing earlier tests does not waive it.

Build 2026111904 implements independent branches. The eleven mandatory CTest cases
include the six spatial relationships, common-parent attachment matrix, native
GUI matrix, overlapping Line Bend with a following cut and state changes,
Sheet exchange and Transition commands. Both packaging entry points
verify their registration and successful execution before assembling an archive.
See the [finite Windows evidence and limits](verification/20261119-independent-sheet-branches.md).
The user accepted new models instead of legacy union-reference repair on
2026-11-19. Keep unsupported multi-branch deformation work explicit; these tests
do not authorize silently fusing independent inputs in another command.

Build 2026111905 adds the Bend end-constraint regression to this label. Its final
local Windows run passes all 12 gates (166.78 s), with actual parallel jobs and
sequential-equivalence assertions retained. Committed-candidate results and
public artifact acceptance are recorded in the
[release record](releases/2026111906.md). Both platform packagers in 2026111906
explicitly build the Bend test executable and reject registration without this
twelfth gate before any archive is assembled.

The exact committed Windows 2026111906 candidate passes all twelve gates in
149.93 s. Its signed public archive and anonymous download have matching
identities and passing runtime acceptance; see the
[publication evidence](verification/20261119-windows-1906-publication.json).
