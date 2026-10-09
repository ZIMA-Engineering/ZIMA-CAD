# Solid straightening

Status: kernel state calculation, native document transactions, console commands
and Modeling GUI are implemented for the supported cases in the current acceptance
matrix below. The attached H-Sweep Properties precision defect is fixed and its
strict GUI checks pass. Signed Windows build [2026100301](releases/2026100301.md)
is published and verified, including packaged GUI acceptance and public update
discovery. Earlier checkpoints describe the evidence
available at their date, not additional unresolved gates where the matrix records
a later pass. Unsupported geometry remains subject to the explicit limits below.

## Acceptance audit (2026-10-03)

The 2026-10-02 native build passed 27 tests in 484.74 seconds. That run deliberately
excluded `zima_cpp_solid_state_ui_contract`, which then failed on the attached
H-Sweep's manual rotation precision. The subsequent approved correction passed
all seven focused placement/GUI/localization tests in 146.40 seconds, including
the formerly failing main Modeling GUI gate (50.73 seconds) and the Sweep GUI
gate (53.68 seconds). The earlier native run is retained locally
in `build/solid-state-acceptance-20261002.log`. Test durations describe entire
synthetic suites and are not timings of individual interactive commands.
The subsequently expanded console transaction checks also passed (0.98 seconds):
all six state commands are exercised, including a changed Straighten coefficient
and a Restore query/unchanged set that retain both revision and data generation.

| Requirement | Evidence and current result |
| --- | --- |
| Constant-section Revolution and 2D/3D/H Sweep, exact section and centroid-path length | Straightening contract, spline and spatial document suites pass; checks include independent analytic lengths, hollow/rounded profiles and section probes. |
| Ordered states, explicit selection, Body isolation and nonaccumulating coefficients | History and kernel suites pass, including suppression, unavailable owners and separate source states. |
| End-face continuations, points/edges, curved children and opposite reference sides | Face-transfer, formed-chain, formed-point, curved/Sweep/H continuation suites pass with semantic vertices, authored references and cold reopening checked. |
| Supported holes and Fillets in both creation orders | Native hole and Fillet suites pass, including rejected invalid changes leaving document, geometry and Undo unchanged. |
| Native persistence, Undo/Redo, Family and unchanged confirmation | Document and continuation suites pass. Successful state packets remain in the native document; missing required packets are rejected. |
| Shared GUI, ordinary/Assembly selection, rollback and Properties | Main Modeling GUI, Sweep GUI and shared dialog tests pass, including the attached H-Sweep's exact authored angles, visible correction, repeated preview, Cancel and unchanged OK. |
| Drawing regeneration, live dimensions, sections/details, persistence and export | Source-only and carried-chain native/GUI suites pass, including independent DXF edge/text checks and no source Part recalculation. |
| Five-language UI | Shared dialog tests exercise all five languages, including the continuity error; catalog validation passed again in 5.38 seconds. The angular-precision correction adds no product text. |
| Native start templates | The earlier current-serializer rewrite produced identical tracked bytes and new-document GUI checks passed in all five languages; see the template checkpoint below. The angular correction does not change the file format. |
| User and console documentation | The user manual describes eligibility and continuity. CLI usage and command coverage now document all six state commands, their selection, coefficient, query and no-op behavior. |
| Completion and Windows publication | Supported geometry and focused GUI gates pass. Build 2026100301 passed fresh committed-source packaging, signed smoke/trust, eight packaged GUI scenarios, public hashes and update discovery; see the release record. |

The user explicitly approved the shared angular-precision correction on
2026-10-03. The implementation retains loaded absolute angles and manual
corrections independently of rounded fields, invalidates their retained values
on actual numerical edits, and retains full resolved orientation when removing
the last orientation reference. Initialization must not clear a loaded correction
while refreshing DOF controls before the solver's orientation result arrives.
Correction clearing therefore belongs to publishing an unreferenced orientation,
not to refreshing the controls. The reference solver and file format are unchanged.
The focused shared-control test, actual Modeling GUI, Sweep GUI, sheet attachment,
sheet state, dialog layout, placement assignment and catalog regressions pass.
The H-Sweep Properties screenshot was inspected. The test retains exact native
history and Undo assertions; no tolerance was substituted for authored equality.
The complete seven-test log is retained in
`build/solid-state-placement-acceptance-20261003.log`. Boundary Surface placement
also passed separately (11.98 seconds).

The broader shared UI executable exposed an independent automation race: pending
native cursor moves replaced synthetic hover events while painting, intermittently
failing face, Sketch-stroke or plane checks. Its harness now filters spontaneous
MouseMove events directed at MeshView; explicit synthetic events and real button
hover tests remain active. The whole UI suite, Sketch-stroke suite and face
inspection suite each passed three consecutive runs (46.67 seconds total).
The production picker is unchanged. Evidence is retained in
`build/solid-state-shared-ui-stable-20261003.log`.

## End-section material transfer checkpoint (2026-10-02)

### Curved continuations

The next acceptance audit extended the chain beyond finite Extrusions. A
continuation may itself be a constant-section Revolution or Sweep with its own
formed/straight history. Carrying only the parent's frame incorrectly treated
that continuation's developed end as its authored end. Replay now separates the
rigid movement inherited at its start from the continuation's own end-section
state. Its successor receives both transformations, in dependency order.
The same state-local plan cache supplies the exact centroid and tangent checks.

The kernel regression independently checks two tangent 90-degree bends followed
by a straight tip. The second bend is tested as both Revolution and circular-arc
Sweep. It checks every section/tip vertex, volume, coefficients 0.9 and 1.1,
individual selection of either bend, simultaneous straightening, restoration,
and unchanged authored fingerprints. The native document test additionally
verifies a Revolution continuation created on a formed or already straight
parent, coefficient editing, unchanged OK, Undo/Redo, native reopening and cold
calculation. These native cases passed in 6.04 seconds; the expanded kernel test
passed in 3.46 seconds. The related finite-chain, Fillet, hole and existing GUI
regressions also passed (six groups, 114.20 seconds).

Sweep transport includes path points, arc midpoints, Bezier controls/spans,
section outlines and normals, circular radial directions and the H-Sweep axis.
This is calculation-local rigid transformation; stored placement and identity
are unchanged. Native 2D/3D continuation coverage subsequently passed for both
formed-first and straight-first attachment, including coefficient changes,
Restore, unchanged confirmation, Undo/Redo and cold reopening. The formed-first
Revolution and Sweep fixtures additionally verify Formed/Straight Family variants.
The Revolution and 2D/3D continuation groups passed in 6.77 and 68.64 seconds;
these synthetic suite durations are not interactive performance estimates.

The curved-child GUI case passes Properties rollback, Cancel, unchanged OK and
native persistence. Its screenshot was inspected (complete suite 23.24 seconds).
An initial test failure was traced to fixture-only Extrusion parameters set on
a Revolution; the serialized document diff was empty. The fixture now sets only
the parameters meaningful to its feature, retaining the full equality check.
No production Properties or serialization change was required.

H-Sweep as the continuing child also passed the native lifecycle and Family
checks, attached on either the formed or straight parent. Its initial section
is aligned through the existing numerical rotation correction and free in-plane
position; the source's helical tangent is not confused with its rotation axis.
The extended H-Sweep group passed in 112.84 seconds, including an independent
check of the retained axis point, direction, length and identity after Restore.
The expected axis uses the predecessor's known rigid end-frame movement, not
the transformed body's cached output. H-Sweep as the parent was verified earlier.
No new user-visible text or localization keys
were added by this extension; the catalog contract passed in 8.93 seconds.

Drawing acceptance now includes a carried Revolution continuation. The fixture
joins a 20 mm centroid-radius quarter bend to a 110 mm centroid-radius quarter
bend. It checks the total developed height against `130*pi/2*coefficient` and a
live dimension of the child against `20*pi/2*coefficient`. It exercises coefficient
editing, Undo/Redo, ordinary/projected/section/detail views, breaks and crops,
native reopening, disk-only regeneration, Restore and Family variants. Drawing
regeneration must not advance the source Part's calculation generation.
The actual Drawing GUI changes the coefficient from 1.1 to 0.8 and exports PDF
and DXF. An independent DXF group-pair reader checks the child's full-length
edges and dimension text. The existing source-only regression, new chain fixture
and new GUI/export gate passed in 4.22 seconds; the rendered sheet was inspected.

Before the 2026-10-03 correction, the expanded Modeling GUI gate exposed a real precision defect when opening
the attached H-Sweep's Properties: the shared placement section reads its manual
rotation correction back from the rounded spin box (`85.45` degrees), changing
the authored correction and resolved rotation before any edit. Coordinate
precision is already retained separately; rotation correction precision is not.
The test printed the exact serialized placement diff and failed strictly.
The protected shared placement change was approved on 2026-10-03 and verified
as recorded in the current audit; no workaround or relaxed equality gate was
introduced. Sweep continuation fixtures share their native/GUI input
definition in `sweep_test_support.hpp`.

Running the H-Sweep precision case last confirms that all seven preceding
attachment cases pass, including the new state-face-attached planar 2D and
spatial 3D Sweeps: displayed coordinates, rollback, Cancel, unchanged OK,
unchanged Undo state and exact native history. The then-current complete GUI test failed
on H-Sweep (38.10 seconds); this is not an overall acceptance pass. Localization
catalog validation passed in 4.68 seconds, with no new product text introduced.

The Properties precision and Sweep continuation GUI gates subsequently passed
on 2026-10-03, as recorded in the current audit above.

The formed-first audit found two additional defects that the earlier single
anchor checks did not detect. A continuation authored on the curved end before
the first Straighten retained world-space in-plane coordinates, and a face plus
one point did not carry the remaining numerical rotation with the section.
The new regression compares every semantic corner against an independent rigid
transform, rather than checking only the anchor. The command-local transfer now
handles these partially constrained end-face frames as well as state-owned
attachments. Authored placements and the shared solver remain unchanged.

The first corrected run passed six regression groups in 118.62 seconds:
the existing face-transfer suite, formed-first two-Extrusion chains on
Revolution and 2D/3D/H Sweep, formed-first face/point combinations on both sides,
the original-source dependent-prefix test, unsupported state-owned-chain
atomicity, and the existing Modeling GUI suite. Fully constrained original-source
face plus two points and face plus line plus point also pass the full-coordinate
check. Extending these fixtures to a second child exposed the same missing frame
transfer in that descendant. Positional end-face attachments now consistently
use the local material transfer, including joins fully constrained by points or
an edge. Point-anchored placements whose face contributes orientation only keep
their existing reference-evaluation path. No shared placement contract changed.

All three formed-first point/edge combinations with two successive Extrusions
passed on both sides (7.21 seconds), along with the original-source and rejected
state-owned regressions. The expanded GUI suite passed in 20.12 seconds; its new
formed-first Properties capture was inspected. The earlier expanded native run
also verified cold calculation after both Straighten and Restore, native reopening
and Undo/Redo (46.72 seconds for Revolution and 2D/3D/H chains). Tests retain the
authored definitions and compare every semantic corner throughout. The final
three-group native run passed in 128.75 seconds with an additional 11-degree
manual correction about the owned profile's normal. It also verifies atomic
rejection of a 2.5 mm normal offset on both sides and of an out-of-plane tilt;
those cases violate the required coincident-centroid/tangent join. The automatic
owned profile uses local XZ, so its in-plane correction is about local Y. The
initial test incorrectly used local Z, was correctly rejected, and now remains
as a separate negative case rather than weakening the continuity check.
No user-visible text or localization keys were introduced by this correction;
the five-language catalog contract passed in 4.50 seconds.
The main user manual now describes the command sequence, coefficients, supported
joins and rejection behavior, explicitly identifying this as development-build
functionality. The current Windows development executable contains the correction.
No commit, push or portable release has been made at this checkpoint.

The current implementation follows the user's refined scope: a continuation
joins the preceding profile at the same filled-section centroid and with the
same directed tangent. Its authored container placement, references, side flags
and corrections remain at their creation boundary. State replay transforms the
calculation inputs into the new material frame. No shared placement solver,
reference identity or native serialization contract is changed.

An end face may be accompanied by points or a straight edge on that same source
section. The document reads persisted reference geometry only to validate this
attachment. The transfer itself comes from the original profile trajectory,
not from the arbitrary parameter axes of a planar OCCT face. Those axes were
shown to differ between a formed circular cap and its straight counterpart.

Revolution supplies its analytic rotation; ordinary Sweep reuses its existing
section transport; H-Sweep evaluates the same discrete trihedron at the guide
ends. A state-local plan cache avoids preparing the same source and coefficient
twice. The continuation check measures the exact filled section (including
voids and thin walls), without constructing an additional swept body. The
current continuation carrier supports finite Extrusion and extrusion children
of a Feature, including their auxiliary axes, points, planes and reference
profiles. Successive finite Extrusions can continue that chain through their
physical end sections under the same centroid/tangent rule. Each descendant
inherits its predecessor's rigid movement; the exact constant-section end is
derived from the filled start centroid and extrusion vector. This does not add
an OCCT body calculation. Carried entries are excluded from the separate
target-reference placement path so the same child is not moved twice.

Reversed features keep their authored start/end cap identities. Admission allows
either semantic cap and calculation verifies the actual physical join instead
of renaming the sides. Mixed-parent attachments, Origin-only descendants and
non-continuous joins are rejected. A drafted feature may itself be carried, but
its end centroid is not inferred by translation for another descendant: that
chain is rejected until its changed end section can be evaluated exactly.
Arbitrary downstream feature types and unconstrained chains remain outside this
verified material-transfer path.

The native face-transfer test has verified rectangular and circular Revolution
ends, both reference sides, retained in-plane rotation, unchanged authored
placement, Undo/Redo, native saving/reopening and cold calculation. Face plus two
points and face plus straight edge plus point also passed, as did atomic rejection
of offset centroids and reversed tangents. A separate H-Sweep kernel case checks
the transported frame against independently calculated semantic end vertices and
verifies the continuation's auxiliary axes, points and plane. A native H-Sweep
continuation now also passes Restore, unchanged confirmation, Undo/Redo, native
save/reopen, cold calculation and earlier coefficient editing. The suite checks
that selecting an independent source leaves an unchanged off-centre attachment
alone, and independently verifies every child vertex after translating and
rotating the whole Body without rewriting its local placement.

The single-continuation suite passed in 30.90 seconds. Before its Body-transform
addition, isolated Restore measurements on the small Revolution fixtures were
50.08–64.60 ms and the two-turn H-Sweep with a continuation took 2676.96 ms.
These synthetic observations are not a general performance guarantee.

The subsequent chain expansion passed for rectangular Revolution on both sides,
H-Sweep, a planar 2D spline and a spatial 3D spline. It checks every vertex of two
successive Extrusions, retained placement and identity, editor reference input
without calculation, Restore, Undo/Redo, native save/reopen, cold calculation and
earlier coefficient changes. Off-centre second joins and a chain after a drafted
Feature are rejected; the document test verifies atomicity for the invalid join.
The Revolution/H-Sweep suite passed in 53.07 seconds. Separate 2D and 3D chain
runs reported Restore at 562.91 ms and 1284.47 ms respectively; these are small
synthetic fixtures at 0.0001 mm source calculation tolerance.
The complete expanded native suite subsequently passed in 66.96 seconds,
including the Formed and Straight Family variants of the two-Extrusion chain.
Each variant retains its own correct vertex coordinates after the earlier
coefficient is edited to 1.1. This extends the earlier original-source Family
checks to descendants carried from state-owned topology.

The expanded Modeling GUI suite passed in 18.82 seconds. It covers original-source
attachments as well as state-owned end-face attachments on Revolution and H-Sweep.
Opening the attached Extrusion's Properties shows its earlier creation boundary;
Cancel restores the exact final scene, unchanged OK retains geometry and Undo,
and saving preserves the native child definition. Both new Properties captures
were inspected. The H-Sweep fixture uses its cap directly; a circular section
does not require the corner vertex used by the separate rectangular fixture.

The related precision, timeline, kernel replay, native document, spline/spatial
Sweep, Fillet, hole, Drawing, dialog, five-language catalog, sheet-state GUI and
Sweep-state GUI regressions passed. The Drawing GUI fixture and export test
passed together in 3.23 seconds. The dialog test additionally forces the new
continuity error after switching through all five languages and checks the
displayed translation (0.26 seconds). No UI text was added by the final source
selection correction or test expansions. The normal local Windows executable
was rebuilt; no Git commit, push or release has been made for this checkpoint.
The repository tool `zima_refresh_start_templates` rewrote and reopened Part,
Skeleton and Assembly starts with the current serializer; the resulting files
were byte-identical to the tracked templates. New-document GUI checks passed in
all five languages, including the initial active Body and enabled commands;
the relation-template GUI check also passed (36.94 seconds together). The stable
root `zima-cad.bat` still launches the current local build.
Earlier investigation sections below describe previous checkpoints, not the
current implementation.

## Modeling command integration checkpoint (2026-10-02)

Modeling exposes Straighten and Restore shape after the Sweep commands. Availability
uses native history inspection in the active editable Body; Restore requires a
currently straight source. Creation and editing share `SolidStateDialog` and the
existing internal properties-window confirmation contract. Pending state metadata
appears in the Tree without inserting a history transaction.

Individual selection interprets the common viewer candidate list using persisted
state ancestry. A state containing multiple source solids is not treated as one
unambiguous source; its individual faces remain selectable. Inspection highlights
the selected source's calculated input wire in the exact active occurrence.
Properties uses the existing rollback boundary. Cancel restores the committed
scene, and OK calls the atomic native transaction.

`zima_cpp_solid_state_ui_contract` passed in 6.61 seconds. It exercises a Part and
a repeated Part occurrence in an Assembly. Coverage includes toolbar availability,
Tree entry and removal, common View filtering, short middle-click, Cancel,
creation, Tree Properties rollback, unchanged OK, Restore selection through
ancestry and middle-button double-click over the View. It also checks GUI
Undo/Redo, saving the state history and reopening its calculated restored volume.
The calculated-solid comparison retains vertex/index bytes and face identities;
it excludes temporary construction-Origin presentation packets recreated by the
existing Assembly scene builder. Both command screenshots were inspected.

The shared-dialog tests passed in all five languages, localization validation
passed, and the existing sheet-state GUI scenario passed after connecting the
new Tree dispatch. These checks do not prove dependent placement replay, general
spline support or complete drawing acceptance.

## Native integration checkpoint (2026-10-02)

### Earlier reference-alias investigation (superseded)

`zima_cpp_solid_state_owned_reference_tests` repeats the established attached
Extrusion/descendant lifecycle using the exact state-owned reference identities
offered by GUI selection, rather than directly referencing the original source.
The earlier prototype passed creation, initial positioning and coefficient editing,
but Restore left the child in its straight frame. That fixture is anchored at a
section corner, includes a separate Origin descendant and does not satisfy the
subsequently agreed centroid/tangent continuation condition. It now checks atomic
rejection of that unsupported chain. Accepted state-owned end-section attachments
are verified by `zima_cpp_solid_state_face_transfer_document_tests`; the original
source-reference lifecycle remains a separate positive test.

The identified cause is that target-state evaluation collects original-source
owners, while the picked reference names an earlier state's persisted topology.
Its encoded ancestry identifies the corresponding original geometry, but the
prefix dependency test does not follow that ancestry. The proposed repair would
build command-evaluation-only geometry for the existing state-owned reference
from its corresponding target geometry. It would retain the stored owner, key,
side, offset and shared solver, and would not add selectable View geometry.
Automatic approval review rejected that change as a protected shared reference
contract change. Explicit approval was requested; no part of the rejected
production patch was applied. The user subsequently chose a local material-frame
transfer with a defined continuity condition, described above.

The sheet-state comparison was verified before proceeding with any reference
change. The four existing sheet-state geometry, command, cone and transition
attachment contracts passed (172.27 seconds total). Sheets capture immutable
material contributions and their creation frames, derive the canonical parent
material region from state topology ancestry, and transform that material when
changing state. Earlier authored placement and original reference packets stay
unchanged. Solid states instead replay source operations in the target shape;
the sheet material deformation path is not a drop-in placement resolver for
arbitrary solid features.

The original exploratory test expected state-owned aliases in the shared
reference view. It has been replaced with the opposite architectural guard:
the view keeps original-source identities, does not invent state-owned aliases,
and does not mutate authored side/offset choices. Two ancestry depths, repeated
occurrences, both Flip values and signed zero remain covered. Existing tests of
missing or ambiguous original-source correspondence remain unchanged. Actual
state-owned material movement belongs to the dedicated native transfer tests.
No persisted reference or shared solver was changed for that transfer.

### Sheet-style rigid transfer feasibility (2026-10-02)

At the user's request, production reference and container-placement changes are
on hold while a command-local alternative is investigated. The standalone
`zima_cpp_solid_state_transfer_probe` changes no production implementation. It
reuses the sheet material `Transition` rigid-frame map with explicitly supplied
creation and target frames at a 90-degree Revolution's end section.

The 24 cases combine coefficients 0.8/1.0/1.2, opposite extrusion directions and
offsets -2/-0/+0/+2 mm. A 2 x 3 x 4 mm child remains a valid 24 mm3 solid.
Independent quarter-turn equations check its transformed vertices and centroid;
inverse transfer restores its vertices within 1e-7 mm. This proves rigid geometry
transfer with a known attachment frame, not automatic attachment inference.

The second probe compares the current compiled kernel inputs of three documents
with identical resolved geometry: fixed placement, point-only attachment, and
point plus FRONT attachment. Their operation fingerprints are equal. Ordinary
`HistoryOperation` carries no container-placement reference list from which the
kernel could recover those three different intents. The unchanged shared solver
confirms that a point-only attachment moves to the target point while retaining
its absolute orientation; the FRONT attachment also changes orientation, tested
with both Flip values. Automatically applying the parent's full rigid rotation
to both would change established placement behavior.

The sheet path has additional input: canonical parent material ownership and the
parent frame at creation. A solid-state-local transfer needs explicit attachment
intent from the document as well; geometric proximity cannot supply it. This may
be transient calculation input, without a native format change, but its preparation
and consumption have not been implemented or accepted. Do not replace the shared
constraint solver with a second incomplete solver. Mixed-parent constraints,
free degrees of freedom, descendant Origins, arbitrary surface locations and
3D/helical sweep frames still require proof before this can be a general fix.

The probe passed in 0.28 seconds; localization validation passed in 4.68 seconds.
There are no new product UI strings. These results do not close the failing
state-owned Restore regression or authorize a release. The previously proposed
shared reference-view patch remains unapplied.

### Single-face attachment rule experiment (2026-10-02)

The user proposed limiting transferable contours to attachment on one face. The
probe now uses actual calculated Revolution end-cap packets with state-owned
identities and compares two strategies without changing production placement:
carry the complete already-resolved child frame with the material, or resolve
its unchanged constraints again against the new cap.

The matrix covers 30/90/135-degree sources, 0.8/1.0/1.2 coefficients, both Flip
states, -2/-0/+0/+2 mm offsets, and 0/17-degree in-plane rotations. Each case is
also evaluated after rotating the entire source by 23 degrees about X and 37
degrees about Z. The XY contour uses TOP (local Z) for its face-normal constraint;
FRONT denotes local Y and is not the normal of this contour.

All **288 local full-frame transfers** preserve a valid 2 x 3 x 4 mm solid,
24 mm3 volume, plane offset, and inverse-transformed vertices within 1e-7 mm.
The experiment keeps the authored placement unchanged and round-trips its
existing JSON serialization, including reference identities, Flip and signed
zero. The test-only eligibility predicate rejects position without orientation,
orientation without position, another orientation owner and another occurrence.
This predicate is not yet used by a product command.

The comparison exposes a necessary semantic detail: all 144 axis-aligned cases
agree with full-frame transfer, but all 144 tilted cases have a different
in-plane orientation when re-evaluated by the unchanged placement solver. The
largest vertex difference is 2.76485 mm. This is not a solver defect: one normal
leaves an in-plane angular degree of freedom, whose existing absolute-parameter
convention differs from material-frame transport.

Consequently, a sheet-style command-local implementation must explicitly carry
the contour's existing in-plane position and orientation with its source face,
while retaining the earlier authored placement at its history boundary. Merely
detecting one face and re-solving its normal is insufficient. The full-frame
method is geometrically supported by this experiment; automatic derivation of
that frame and history integration remain unimplemented. Sweep frames, curved
faces, chained descendants, actual command rejection, full native document
reopening and Undo/Redo are not proven by this isolated probe. No shared
reference, solver, serialization or product command implementation was edited.

### New placement reference GUI checkpoint

The Windows command test now creates a new Point-type Feature after Straighten
through the ordinary Feature shortcut and shared placement dialog. It finds the
straightened end face in the common candidate list, cycles to that candidate with
RMB, confirms it with LMB and checks the stored position against the intersection
of the click ray with the offered face. The pending position must not revert to
the original formed face. The explicit selected reference is retained alongside
the dialog's derived orientation reference; the test does not assume that a
single click must produce exactly one reference row.

OK and native save/reopen retain the selected owner and semantic key. Undo of
the new Feature leaves the straightened solid unchanged. The surrounding basic,
Assembly-occurrence, treated-Revolution and attached-Properties GUI scenarios
also pass. This checks new reference entry and persistence; the separate native
attachment-chain tests cover restoring attached solid features. This checkpoint
does not claim that the newly created Point was retained through Restore: it is
undone before the existing Restore scenario resumes. Only tests and English
documentation changed; no new UI strings or placement-solver changes were added.

### Fillet failure atomicity and treated-source GUI

The native Fillet suite also exercises two R2 fillets on the opposite end rims
of a 90-degree Revolution with a 20-by-20 section centered at radius 110 mm.
Coefficient 0.9 accepts the straightened stock. Coefficient 0.001 would reduce
its length to approximately 0.173 mm, where the end fillets cannot fit. Both
creating that state and editing the existing state fail with the actual OCCT
Fillet calculation error. The native document, revision, data generation and
calculated-boundary storage remain unchanged. A subsequent Undo removes the
previous valid operation, and Redo restores exactly the pre-failure document;
the failed operation inserts no history step. The full native Fillet suite
passes in 3.55 s.

The Windows GUI fixture now includes a Revolution with an authored longitudinal
R2 Fillet. The real command actions pass selection, Cancel, Straighten,
Properties rollback, unchanged OK, Restore, middle-button confirmation,
Undo/Redo and native save/reopen. Reopening preserves the complete authored
Fillet and the restored calculated volume. The captured command window was
visually checked. This proves the treated Revolution scenario, not every
treated Sweep or all possible Fillet contours.

These additions change test coverage and English documentation only. They add
no user-visible strings; the localization catalog contract passes (4.36 s).

The Windows Sweep GUI suite additionally passes a 4-by-4 profile on the cubic
parabola `x=20*t^2, z=100*t` with an authored R0.2 longitudinal Fillet. Following
the actual Straighten action with coefficient 0.9, the test saves and reopens
the calculated body. Its volume must match
`(16 - (1 - pi/4) * 0.2^2) * (sqrt(11600)/2 + 125*asinh(0.4)) * 0.9`
within 0.0001 mm3. This independently checks that the straight geometry retains
the Fillet's material removal. Restore recovers the original treated volume;
the authored Fillet parameters survive both states and native reopening.
The same GUI run covers picking, Cancel, Properties rollback, unchanged OK,
middle-button confirmation and Undo/Redo. Its captured command window was
visually checked. No product behavior or UI text changed for this extension;
the five-language catalog contract passes (4.53 s).

### Following-entry reference evaluation (verified limited checkpoint)

Calculated results now carry immutable `solid_state_reference_views`, keyed by
the state container. Each packet retains the replay's complete original-source
reference geometry separately from selectable state-owned topology. Native body
packet serialization includes this data; Body placement transforms its geometry
into document coordinates. Later calculated boundaries retain the packets by
shared ownership. The state fingerprint version changed to invalidate calculations
that predate this required reference data.

During explicit calculation, native history inspection supplies the active packet
to the shared resolver only for later entries that actually reference its owners.
Earlier entries keep their original inputs. Other Body branches retain their own
state data. Source keys, sides and offsets are unchanged, and no evaluation aliases
are appended to the View's selectable geometry.

This step covers children authored after a state and their descendants, including
an edit of the earlier state's coefficient. The following prefix-replay checkpoint
adds a later Restore scenario; neither checkpoint proves full dependent-chain
acceptance.

The eleven-test native/kernel/GUI verification run passed on 2026-10-02 in
193.26 seconds, including state document/kernel tests, reference views, ordinary
and helical Sweep contracts, sheet states, localization, start-template GUI and
state-command GUI tests. This confirms the following-entry scenarios described
above, not prefix replay.

### Transient dependent-prefix replay checkpoint

`solid_state_calculation_operations` resolves a state's earlier dependencies in
an isolated native document using its calculated target reference packet. Only
changed replay primitives are passed to the kernel. Earlier authored placements,
source reference keys, side flags and offsets remain untouched. Descendants are
resolved in history order using the existing shared placement equations.

The runtime replay inputs participate in the kernel fingerprint by content,
never allocation identity. Native save/reopen reconstructs those same inputs
from the document and persisted evaluation packets before checking the fingerprint.
Family and history-edit cache checks consume this same preparation. This adds no
required sidecar or separately authored placement data. Public Regenerate retains
boundary-specific reference inputs in its final reference refresh.

`zima_cpp_solid_state_attached_restore_tests` passed in 2.58 seconds after initially
reproducing the misplaced-child defect. It creates a Revolution, Straighten, a child attached
to the source end vertex and flipped end face, and a descendant attached to the
child Origin. After changing the straightening coefficient, it appends Restore.
The parent, child and descendant anchors return to the independently retained
formed endpoint. Authored child placements remain intact. The check passes Undo/Redo,
native save/reopen, cold calculation and public Workspace Regenerate. It compares
persisted semantic vertices in the calculated evaluation packet, not volume or
reference strings alone. The existing document, timeline and fingerprint tests
also passed. Fingerprint tests distinguish changed geometry from equal content in
a different allocation. No user-visible text changed; existing localized errors
are reused.

The extended scenario also passes Family rows enabling/disabling Restore with
the attached branch, and an edit of the original Revolution from 90 to 120 degrees.
The changed source and both attached anchors follow their new calculated endpoints,
with unchanged semantic identities. The independent Family, history-command,
placement-command, solid-state kernel, reference-view, straightening geometry and
five-language catalog regressions passed after the integration.
The application was rebuilt, and new-document/template GUI checks plus the basic
solid-state Part/Assembly GUI contract passed (three tests, 50.53 seconds). The
attached-prefix scenario itself is currently native-test coverage, not a new GUI
acceptance scenario.

This is a limited native checkpoint. More complete orientation/side, repeated-state,
multi-Body, attached cutter, Family and GUI matrices remain required,
along with the outstanding spline and treatment cases. It is not release acceptance.

### Repeated state and cached editor inputs

The native attached-branch regression now performs three more Straighten/Restore
cycles with coefficients 0.85, 1.2 and 1.0 after changing the Revolution to 120
degrees. Straight lengths are checked independently against the 110 mm section
centroid radius and authored angle; coefficients do not accumulate. Each restored
parent and both descendant anchors return to the same formed position. Earlier
authored child frames remain unchanged, including after save/reopen and a cold
calculation of the complete repeated-state history.

`solid_state_editor_reference_geometry` reads the cached reference packet before
an existing entry and stops at that history boundary. Later state packets may be
absent during rollback. It is connected to the generic primitive Properties
reference input and the native placement/reference-entry queries, before the
ordinary Body-frame conversion. The placement equations and persisted reference
choices are unchanged; this input preparation has no kernel dependency.

The native regression opens the earlier child's reference input after a later
Restore, verifies its original straight-state endpoint, prepares a reference
replacement without publishing a calculation or transaction, and permits repairing
a missing stored reference. Missing required state geometry is rejected instead
of silently using the original formed coordinates. This does not yet prove all
dialog-specific picking/inspection paths or creation of an uncommitted feature;
those remain acceptance work. No user-visible text changed.

### Expanded GUI gate: unchanged attached-profile confirmation

The expanded `zima_cpp_solid_state_ui_contract` opens an Extrusion attached after
Straighten while a later Restore is present. Its displayed coordinates match the
earlier straight boundary at the numeric controls' declared decimal precision.
The reference identity is retained, rollback displays the input, and Cancel
restores the exact calculated-solid packet. The screenshot was inspected.

The unchanged-OK gate now passes in the separate Windows verification executable
(6.48 s). The original failure combined auxiliary Extrusion `height` normalization
with referenced coordinates rounded by the editor. The dialog updates auxiliary
height only when the explicit extent changes. With explicit user approval, the
shared placement controls now retain loaded and reference-resolved coordinates
at full precision while displaying the document's decimal precision. Editing a
coordinate invalidates its retained value. A later reference solution replaces it.
The Construction/3D Sweep dialog uses the same shared constraint-state method.

The regression verifies exact final-solid restoration on Cancel, no Undo change
on unchanged OK, unchanged attachment identity and native save/reopen. The
shared control test also covers partial constraints, manual editing, returning
to the old displayed number, replacing the solution and reinitialization.
No shared profile transaction comparison change was needed; the earlier proposed
patch to `profile_operations.cpp` remains absent. Placement equations, side
choices and the native format are unchanged.

Straighten and Restore shape are native history containers with persisted source
selection and length coefficient. The workspace commits a completely calculated
document atomically as one Undo step. Unchanged properties return before calculation
or transaction creation. Console `straighten` and `restore_shape` commands expose
`get`, `create` and `set`; reads do not calculate geometry.

Native regression checks cover save/reopen, cold regeneration, exact authored-source
retention, Undo/Redo, editing an earlier state before Restore, and failed-edit
atomicity. Family presence rows independently suppress or enable Straighten, retain
source/state identities and survive native reopen and cold regeneration. These are
backend Family checks, not complete GUI or drawing acceptance.

The repository start templates were rewritten and reopened with the current
serializer. New-document and relation-template GUI checks passed. Existing empty
template bytes did not change because the new feature parameters are absent there.

## Kernel state replay checkpoint (2026-10-02)

`SolidStateRequest` is now an actual kernel history operation. It rebuilds an
authored prefix according to the state timeline, rather than restoring an old
body snapshot. Later Fillet requests are reapplied, including a Fillet created
against a preceding state's generated edge. State-owned faces, edges and points
carry length-prefixed original owner/key ancestry through `solid_state_child_key()`;
OCCT enumeration does not define their identities. Earlier original reference
packets remain unchanged. State parameters participate in the history fingerprint.

The kernel tests verify coefficient changes, state suppression, cold calculation,
Body isolation, retained Fillets and H-Sweep Straighten/Restore volume. They do
not by themselves constitute native document, GUI, Undo/Redo, Family or drawing acceptance.
The kernel still receives resolved geometry inputs; automatic replay of dependent
container placement is a required next integration step.

Finite, undrafted Extrusion cutters can be transferred within a tangent
line/arc Sweep's straight portions. Exact Boolean intersections check the complete
removed volume against a straight portion, not just the cutter center. The tool
moves rigidly: its station follows the developed-length coefficient while its
cross-section and bore radius stay unchanged. Restoration also transfers a hole
authored in an earlier straight state. A moved tool must not remove additional
material elsewhere on the target source or another preceding source. Cuts spanning
independently moving sources, transition-crossing holes, limit-driven openings and
unsupported treatments reject replay instead of silently changing the model.

The hole tests exercise an original straight-leg bore, a bore added after
Straighten, a transition-crossing rejection, and a bore in a straight portion after
an arc. The latter checks the calculated cylinder axis, developed station and
unchanged radius in the final original-reference packet (intermediate packets are
intentionally compacted by the existing kernel).

Containment mass comparisons use OCCT adaptive integration with relative error
target `1e-10`. Fixed quadrature produced different volumes for independently
trimmed representations of the same cylindrical cut; the correction increases
measurement accuracy rather than relaxing the geometry tolerance. Verification
also rejects a tool that would cut a previously untouched source after transfer.

## Confirmed scope

### State timeline checkpoint (2026-10-02)

`solid_state_history.hpp` now evaluates calculation-side state changes against
an immutable authored operation list. It scopes each change to its Body and
history boundary, excludes suppressed/subtractive/surface/sheet/copy operands,
and rejects missing, duplicate, cross-Body or not-yet-created explicit sources.
Structural candidacy does not prove constant section: exact kernel preparation
must still validate each chosen source during calculation.

Length coefficients replace the previous state coefficient rather than multiply
it. Restore returns to the authored source and resets the coefficient to one.
Restore-all affects only currently straight sources in the same Body. Suppressed
changes are omitted when rebuilding the timeline; later changes cannot alter an
earlier boundary. Source indices always refer to the authored operation list,
which retains later treatments for replay.

The timeline contract test covers ordering, Body isolation, explicit selection,
suppression, invalid inputs and independent state changes. The kernel contract
test also drives a real Revolution and its subsequent 1 mm Fillet through
coefficients 0.9 and 1.1, then restores the formed volume with the Fillet retained.
It verifies the unchanged authored-history fingerprint. This is calculation
preparation coverage: the test explicitly constructs the transient replay.
It does not establish automatic placement/treatment remapping, state-owned
topology, persisted state containers, GUI integration, Undo/Redo or Family
acceptance. Those remain required before the commands can be released.

The Part Modeling commands are **Straighten** and **Restore shape** (Czech UI:
**Narovnat** and **Obnovit tvar**). They retain the authored feature history so
formed and straight states can be used in Family Tables and drawings.

Inputs are additive solid revolutions and sweeps, including helical sweeps,
with a constant cross-section. Sheet metal, surfaces and variable cross-sections
are outside the requested scope. Subtractive features are not independently
straightenable sources; supported holes in a source solid must nevertheless be
preserved as described below. Unsupported geometry must not be silently replaced
by a constant initial section.

The base straightened solid is an extrusion of the original cross-section,
with supported subsequent modifications retained. Its length is the length of
the trajectory of the cross-section's area centroid,
multiplied by a positive dimensionless coefficient. The reference is the section
centroid trajectory, not the centroid of the complete solid or necessarily the
authored guide curve. Cross-section dimensions remain unchanged.

- `1.0` preserves the centroid-trajectory length.
- `0.9` shortens it by ten percent.
- `1.1` lengthens it by ten percent.

This coefficient is a length multiplier, not a sheet-metal neutral-axis K-factor.
Restore shape restores the authored curved trajectory while retaining supported
modifications added in the straight state. Repeated state changes must not
accumulate coefficient multiplications or replace the current history with an
old geometry snapshot.

Representative inputs are L sections, rectangular hollow sections and wire.
Straightening preserves the complete section, including inner and outer corner
radii, wall thickness and enclosed voids. The coefficient changes only the
developed length, not the cross-section dimensions.

## Attached downstream features

The user explicitly requires downstream features attached to the final curved
end face to follow that face when the source is straightened. Their positions
and orientations must follow the corresponding target end frame. Restore shape
must carry them back through the same dependency chain. This applies to features
already present before a state change and to supported additions made in the
straightened state; neither case may leave detached geometry at its old world
coordinates.

Consume the existing placement and explicit dependency-regeneration contracts.
Retain the exact source identity, selected geometry side, orientation references
and authored offsets. Do not copy world-space positions into persistent placement
or silently redirect a reference to a nearby face. An unavailable or ambiguous
correspondence must reject the change without partially committing it.

Verification must include a source bend/sweep, a feature attached to its end face,
and another feature attached to that child. Check both state directions, changed
length coefficient, nonzero offsets, opposite sides at zero offset, save/reopen,
Undo/Redo and Family Table regeneration. The existing shared placement solver is
protected; this requirement does not authorize changing its general contract.

## Verification targets

For a circular centroid trajectory of radius `R` and angular travel `theta` in
radians, the unscaled length is `abs(R * theta)`. A constant-radius helical
centroid trajectory with radius `R`, pitch `p` per turn and `n` turns has length
`abs(n) * sqrt((2 * pi * R)^2 + p^2)`. These provide independent checks of the
eventual kernel implementation. They apply to the centroid trajectory itself;
an offset section centroid must not be mistaken for the guide-curve origin.

The implementation must verify exclusions, coefficient editing, restoration,
history suppression, Undo/Redo, native save/reopen, Family Table evaluation and
drawing source geometry. It must preserve source identities and use the existing
in-application properties and reference-entry contracts. Geometry calculation
belongs to explicit confirmation or regeneration.

## Preservation of subsequent modifications

The user confirmed on 2026-09-26 that fillets must survive straightening and
holes in straight portions must be preserved. Producing clean stock while
discarding those features does not satisfy the command's requirements.

A hole intersecting a curved portion is explicitly unsupported in the first
version. The user approved rejecting that operation with an explanation. The
operation must leave the document and its calculated geometry unchanged; it must
not remove the hole, move it speculatively, or partially commit other sources.
Classifying only a hole's center is insufficient: its complete removed-material
extent must lie in a supported straight portion. A hole crossing a straight/curved
boundary is therefore unsupported as well.

Fillets already present in the source profile remain part of that profile.
Subsequent Fillet history operations require their corresponding edges to be
resolved on the straightened geometry and their authored radii and contour
direction to be retained. A nonlinear deformation of a finished fillet surface
alone does not prove that the specified radius survived. Where an edge vanishes
or a requested radius becomes impossible, reject the state change rather than
silently suppress the fillet or reduce its radius.

This preservation also applies in the opposite direction. The user explicitly
confirmed the sequence `Source -> Straighten -> Fillet -> Restore shape`: the
new Fillet must remain on the restored curved source. Reapply the authored
treatment to the corresponding segment/edges in the target state, preserving
its parameters and ancestry. Restoring only the geometry cached before
Straighten would incorrectly discard that later edit.

The current implementation exposes the required starting information through
`HistoryContainer::edge_treatment` and `kernel::FilletRequest`: persisted edge
references, radius values, the constant/linear mode, reversal and contour-start
vertices. Sweeps and extrusions use different semantic edge roles. A later
implementation must explicitly map their ancestry; copying an old edge key or
matching edges by OCCT traversal order is not a valid correspondence.

Additional acceptance cases are required before this capability is complete:

| Case | Required result |
| --- | --- |
| Rounded source cross-section | Preserve the complete section, including its arcs |
| Fillet applied after the source solid | Retain the authored fillet definition on corresponding edges |
| Hole wholly inside a straight portion | Preserve the hole with that portion in the straightened state |
| Hole intersecting a curved portion or transition | Explain the unsupported case and commit no changes |
| Fillet invalid after straightening or coefficient change | Report the failed modification and commit no changes |
| Restore shape after an accepted state change | Recover the authored shape with its holes and fillets |
| New Fillet after Straighten, followed by Restore shape | Retain the new fillet on the corresponding restored segment |
| Save/reopen, Undo/Redo and Family Table state changes | Preserve the same modifications and reference identities |

These are implementation and verification requirements, not results of completed
command tests. The treatment of other cuts and body modifications has not been
generalized from the confirmed hole and fillet behavior.

## Native treatment regression checkpoint (2026-10-02)

### Failed-definition policy verification (2026-10-08)

The later general feature-definition agreement distinguishes creation from editing
an already successful calculation. A new, valid Straighten definition whose
coefficient makes the retained end-rim Fillets impossible is saved with its native
calculation error and the preceding valid geometry. The user can inspect and repair
the complete definition. An invalid edit of an existing successfully calculated
Straighten remains an atomic rejection; it changes neither the document nor its
calculated geometry, revision or Undo/Redo state. Neither path silently reduces a
Fillet radius or reports a failed calculation as successful.

The native Fillet-state regression verifies both paths, including retained
parameters/error, input volume, Undo/Redo and save/reopen for a failed new feature.
Undo deliberately retains allocated dimension numbers under DocumentSession's
existing identity policy. The test compares authored document content separately
and verifies that all previously assigned numbers remain unchanged and that any
new retained allocations belong to the failed feature. This updates the earlier
test expectation that every failed creation must throw. The Windows regression
passed; no product transaction implementation was changed for this correction.

`zima_cpp_solid_state_fillet_document_tests` creates native constant and linear
Fillets both before Straighten and after Straighten. The linear case retains
its authored R1/R2 endpoint and reversed direction. Restore is compared with a
separate calculation applying that same treatment directly to the original
Revolution, and must retain the complete authored source and Fillet definitions.
The fixture also checks state Undo/Redo, native save/reopen and cold regeneration.

`zima_cpp_solid_state_hole_document_tests` creates a transverse 1 mm diameter
Extrusion cut in a straight leg of a native rounded 3D Sweep, both before and
after Straighten. It checks the removed volume, actual cylindrical radius and
axis, developed material station, restored station, authored definitions,
Undo/Redo and cold native reopen. A separate cut whose removed material crosses
the straight/curved transition must reject the new state without changing the
document, revision, calculation generation or calculated packet allocation.

This native fixture exposed an overly strict volume-only equivalence check:
independently calculated Sweep faces produced about 0.000002 mm3 difference for
the same bore. If the existing volume comparison fails, replay now subtracts
the two removed-material shapes in both directions at the operation's existing
Boolean tolerance. Both residual volumes must be at most 1e-10 mm3. This is a
geometric equivalence check; the volume acceptance budget, source precision,
bore size and whole-hole containment checks are not loosened. Existing regressions
continue to reject transition-crossing holes and unintended cuts in another
source. No new user-visible text is introduced; existing localized errors remain.

These cases extend native treatment acceptance. They do not prove every contour
split, every limit-driven cutter or actual GUI treatment selection.

## Kernel feasibility check (2026-09-26)

A disposable native C++ probe exercised the existing kernel operations without
adding state commands or changing document data. It manually constructed both
the formed and straight definitions, so it does not verify automatic conversion,
state persistence, GUI behavior or source-reference remapping.

- An L section with 20 mm legs and 4 mm thickness has area 144 mm2. Its centroid
  was calculated independently from two rectangles. For a quarter-turn about the
  test axis, straight extrusion volumes at coefficients 0.9, 1.0 and 1.1 agreed
  with `area * centroid-trajectory length * coefficient` within 0.00001 mm3.
- The same persisted longitudinal-edge reference and a 1 mm Fillet request
  evaluated successfully on both the extruded and revolved versions. This proves
  reuse for that specific original-edge case, not a general mapping for every
  generated, split or treatment-created edge.
- A two-segment L Sweep with a straight leg followed by a quarter-circle and its
  manually straightened counterpart retained the hole in the straight leg.
  Each subtraction removed `4 * pi` mm3 within 0.0001 mm3. The same subsequent
  Fillet request also evaluated on both definitions.
- A 20 x 20 mm hollow section with a 16 x 16 mm void retained the independently
  expected 144 mm2 section area. A circular wire of radius 4 mm retained its
  expected section area. Their straight extrusion volumes agreed with the
  independent length/area calculations within 0.00001 mm3.

The probe did not test automatic exclusion of curved-region holes, arbitrary
Sweep transport, Helical Sweep, multiple bodies, or newly created Fillet topology
referenced through a state container. Those remain required implementation work.

The user identified a general spherical surface as a subsequent Part feature.
It is outside this straightening implementation and outside the 2026092604
Drawing maintenance release.

## Approved reference evaluation across a state boundary

The calculation-side `solid_state_reference_view()` builder now filters a
calculated source packet to exact owner/occurrence pairs and checks each required
placement reference against the authored and target semantic-key inventories.
A missing key, changed topology kind, or key shared across topology kinds is
rejected before any document mutation. It does not search for nearby geometry.
Multiple triangles of one face remain one semantic identity. An unused missing
reference may be omitted; an explicit empty owner override prevents the consumer
from falling back to old geometry. Mesh indices are validated before compaction.

The existing real Revolution/Extrusion attachment test now consumes this builder
for the end point and cap references, including the descendant Origin chain and
twenty round trips. Packet tests cover all four reference kinds, occurrence
isolation, malformed meshes and missing or wrong-kind targets. This builder is
not a topology-ancestry producer or a general remapper for treatment-created
edges. It also does not validate geometric solvability (for example a lost
symmetry axis); the existing placement solver and atomic state transaction must
still reject such cases during full calculation.

The existing `calculate_part_reference_state()` in
`cpp/modules/workspace/src/model_calculation.cpp` supplies calculated original
reference geometry to `PartDocument::resolve_constructions()`. Existing sheet
state operations publish their own derived topology while earlier original
reference packets remain immutable. Reusing that mechanism alone would leave a
feature attached to the original curved end face at its original frame.

The proposed extension is a history-boundary-specific reference view for solid
state operations. It would resolve the same persisted source/semantic identity
to the corresponding straight or restored end frame before the existing placement
solver evaluates downstream containers. Descendants would then follow through
the existing dependency passes. Authored source geometry and reference keys,
side choices, offsets, input controls and the placement solver equations must
remain unchanged. The reference view must be derived from native history and
calculated reference data, including after a cold reopen; opening Properties
must not invoke OCCT.

This changes the geometry supplied to the protected shared placement contract.
Explicit user approval was granted on 2026-10-02 after the proposed change was
explained: resolve the same stored reference at its history boundary, using the
straightened or restored geometry, and let descendants follow that frame. This
approval covers the reference geometry supplied to the shared placement solver;
it does not authorize changing solver equations, reference identity, side choice
or authored offsets. The downstream
chain, side, zero-offset, cold-reopen and Undo/Redo checks listed above remain
acceptance gates, together with unchanged placement behavior in documents that
contain no solid state operation.

## Kernel preparation checkpoint (2026-10-02)

`OcctKernel::prepare_straightening()` currently prepares replacements for solid
Revolution, single-section transported Sweep, and ordinary Sweep requests made
from connected tangent straight, circular and spline segments with a rigid constant section.
It does not modify the
authored request or insert a history container. Revolution produces an Extrusion;
transported Sweep retains its source path and section identities on a straight
path. These are calculation helpers, not complete user commands.

Revolution uses the filled section's area centroid, including voids and wall
offsets, and the authored rotation axis and angle. Transported Sweep samples the
actual transported sections with the same discrete transport as H-Sweep and
refines an offset centroid trajectory against the feature tolerance. When the
filled section centroid coincides with the guide start within 1e-12 mm, rigid
single-profile transport makes that trajectory the guide itself. This case
reuses OCCT curve-length integration instead of repeatedly constructing sampled
section faces. The integration budget is one hundredth of the feature linear
tolerance, divided across source segments. Authored source station identities,
profile definitions and the offset-centroid sampling branch are preserved.
It does not use
the display mesh or assume that the section centroid lies on the guide curve.

The dedicated `zima_cpp_solid_straightening_contract_tests` test currently checks:

- L-section area and circular centroid length at coefficients 0.9, 1.0 and 1.1,
  both axis directions and two initial angles.
- Exact circular and hollow sections, source-request immutability and retention
  of authored profile identities.
- Reapplication of the same 1 mm longitudinal Fillet on separately calculated
  straight and curved L-section definitions. This is not a Restore shape test.
- An H-Sweep fixture against an independent constant-radius helix length.
- A transported quarter-circle with its section centroid offset from guide
  radius 10 mm to radius 12 mm, in both directions, with solid and hollow wire
  sections. The independent expected developed length is `6 * pi` mm.
- A thin circular section with radii 0.4 and 0.6 mm, retaining its wall area and
  straightened volume. Calculated straight volumes match area times developed
  length and coefficient within the test tolerances.
- Rejection of surface Revolution and nonpositive/nonfinite coefficients.

The dedicated test and the five-language translation/catalog contract pass.
These preparation tests alone do not establish native command acceptance. The
native, replay and Modeling checkpoints above describe the subsequent integration
and its remaining limits. General spatial spline acceptance, sharp-corner Sweep conversion and
complete drawing/dependency acceptance remain outstanding. The full acceptance
requirements remain mandatory before release.

### Ordinary Sweep development

Ordinary Sweep preparation retains the original Sweep request type and its path,
station, profile, curve and point identities. Each straight/circular segment has
an exact rigid path frame. Profiles are compared in that frame using their entire
definition, including polygon vertices, arc definitions, ellipse axes and radii,
spline poles/knots/weights and holes. Merely equal areas are insufficient. Variable
sections are rejected rather than replaced by the first section.

Straight portions contribute their translation distance; circular portions
contribute the angle multiplied by the section centroid's radius about the
actual arc axis. Development works away from the global principal planes and
supports both shared stations and the independent incoming/outgoing station
identities emitted by the native 2D Sweep adapter. Thin offsets continue through
the existing profile builder.

The dedicated test additionally covers a straight leg followed by a tangent
quarter-circle, coefficients 0.9/1.0/1.1, spatial rotation/translation, hollow and
thin sections, and actual native 2D Sweep line/arc definitions. A hole in the
straight leg retains its removed volume and the authored longitudinal Fillet can
be reapplied to the prepared geometry. These checks do not implement automatic
hole classification or state replay. An equal-area rectangle-to-square loft is
rejected; explicitly congruent station profiles are accepted. Existing 2D/3D
Sweep contracts and localization coverage pass.

Spline segments now consume the corrected-Frenet transport used by ordinary
`MakePipeShell::SetMode(false)`. Their end frames participate in the same full
profile comparison. The actual filled-section centroid is transported along
the spline and its length is refined independently of display tessellation.
Two consecutive refinements must differ by at most one eighth of the feature
linear tolerance divided by the segment count; failure to converge is an error.
Straight and circular segments retain their analytic calculations.

The spline regression uses the exact cubic representation of the parabola
`x=20*t^2, z=100*t`, with section centroids at offsets 0, +2 and -2 mm and both
solid and hollow circular sections. Its independent length is
`sqrt(11600)/2 + 125*asinh(0.4) - offset*atan(0.4)`.
Checks cover developed length, section area, source identity, straight volume,
source transport volume and state replay through Restore.

Additional regressions split the same parabola into two tangent cubic segments
with an offset rectangular section, both shared and separate station identities,
and a rigid rotation/translation of the complete model. Source and developed
volumes, section area, identity and Restore are checked. A genuinely spatial
cubic (`x=20*t^2, y=10*t^3, z=100*t`) with a centered circular section is checked
against independent Simpson integration of its analytic derivative. This
spatial case does not prove noncircular section preservation under torsion.

`zima_cpp_solid_state_spline_document_tests` exercises the real 2D Sweep adapter
and workspace transactions: owned spline/profile Sketch creation, Straighten,
native save/reopen with calculated packets, cold regeneration, Restore and
Undo/Redo. The source Sketches and feature identity must remain unchanged across
state operations. It also changes the source spline through its normal editor
API, retaining its curve identity, and requires the restored and subsequently
straightened geometry to follow the changed source rather than a stale cache.

`zima_cpp_solid_state_spatial_sweep_document_tests` extends native transaction
coverage to an interpolating 3D spline with a circular section and H-Sweep with
both circular and rectangular sections. Independent cubic-derivative integration
and an analytic helix length check the expected volumes. Each fixture covers
source preservation, unchanged state confirmation without a new revision or
calculation generation, native save/reopen, cold regeneration, Restore and
Undo/Redo. These checks use the existing Sweep workspace transaction rather
than inserting raw kernel operations into the document.

On the Windows development host, this complete three-fixture lifecycle test
took 193.70 s with sampled centered sections and 40.93 s with direct guide-length
integration. Both runs passed the same independent volume, persistence and
transaction checks. This measures the fixture suite (including repeated native
load and regeneration), not the latency of one user command or all H-Sweep
models. Offset-centroid, hollow and Thin regression cases continue to pass in
the separate geometry contract.

The geometry contract additionally exercises a centered rectangular section on
the genuinely spatial cubic, with its explicit end profile transported by the
pipe's corrected-Frenet frame. Its area and developed length are checked against
the independent derivative integral, its source and straight volumes agree with
area times length, Restore recovers the original volume, and authored frames
remain unchanged. Widening the end rectangle in its own plane is rejected as a
changing section. This verifies an explicitly constant transported section; it
does not establish that every inherited station profile on a torsional guide is
constant throughout the ordinary multi-section pipe calculation.
The expanded geometry contract passed in 23.09 s and the translation coverage
and catalog contract passed in 4.63 s on the Windows development host.

`zima_cpp_solid_state_sweep_ui_contract` covers actual application creation and
editing for a 2D parabola, an interpolating 3D spline and H-Sweep, using circular
sections. It checks Tree and common View selection, Cancel, unchanged state
properties, Restore selection through state ancestry, middle-button confirmation,
Undo/Redo and native save/reopen. The suite passed in 23.78 s; screenshots of all
three property dialogs were inspected for layout and displayed geometry.
The expanded Sweep suite also places a 2D Sweep and an H-Sweep on an endpoint
after Straighten, then adds Restore. Their Properties now consume the cached
reference view at the edited history boundary before ordinary Body conversion.
The existing placement solver and stored reference choices are unchanged.
Both dialogs display the earlier straight-state position, Cancel restores the
exact final solid, and unchanged OK preserves the Undo state and persisted
container. The subsequent expanded Windows verification also covers an attached
3D Sweep. Its private preview document resolves constructions against the cached
history reference views and supplies the edited boundary to the existing solver.
All three attached Sweep dialogs pass coordinate, identity, rollback, Cancel,
unchanged OK and native persistence checks. Creation-time reference inputs remain
separate acceptance work. The attached Extrusion gate above also passes.

Localization review: this correction adds no user-visible text. The five-language
catalog contract, dialog-layout contract and Windows numeric-lock contract pass.
The Windows numeric-fields contract also passes (5.33 s), after correcting its
expectation for the Sweep kernel tolerance: that field intentionally supports
nine decimal places independently of ordinary document dimension precision.
Its clipping, sizing and native-control checks remain enabled. Overall command
acceptance still includes the separate remaining cases below.
These runs use the separate verification executable and do not
establish that the user's running development executable has been replaced.

This remains partial acceptance. Noncircular ordinary spatial Sweep now passes
the native and GUI checkpoints below. Spline treatment/reference chains and all
editor reference paths still require verification. Discontinuous tangents and smooth loft requests
remain unsupported by this preparation branch. These test additions introduce
no user-visible text; existing five-language catalogs remain applicable.

## Drawing integration checkpoint (2026-10-02)

`zima_cpp_solid_state_drawing_tests` exercises the native Drawing command host
with a calculated Revolution, Straighten and Restore shape. It creates an
ordinary view and its projected child from the straight state, then attaches a
vertical dimension to a persisted, state-owned longitudinal edge. Editing the
Straighten coefficient from 0.9 to 1.1 must update the projected length and the
dimension during explicit Drawing regeneration without changing its attachment
identity. The test requires resolved attachments and direction, not a cached
fallback dimension value. It checks Drawing Undo/Redo and native save/reopen.

The open unsaved Part is authoritative even while its saved file contains the
previous coefficient. Drawing creation and regeneration must leave the source
Part's calculation generation unchanged. With no open Workspace, explicit
Drawing regeneration consumes the current saved Part packets and retains the
dimension. After Restore, the view returns to the independently known formed
extent. Formed and developed Family instances are also projected simultaneously
and regenerated from their generic native file with no live source tabs.

The new test, existing drawing-view command contract and five-language catalog
contract pass. This checkpoint adds no UI strings or production drawing changes.
It covers backend view/dimension/Family integration; actual GUI acceptance,
sections, cropped/detail views and treated Sweep drawing cases are not proved
by this fixture and remain part of overall acceptance.

## History reference view checkpoint (2026-10-02)

`PartDocument::resolve_constructions()` now has an explicit calculation-only
overload accepting `HistoryReferenceViews`. The existing overload supplies no
views and retains ordinary reference evaluation. Each view identifies the exact
original owners and occurrence paths to replace before one history entry; its
geometry keeps their persisted semantic identities. The placement equations,
authored references, offsets and side flags are unchanged.

An entry without an override returns to the original reference input for the
affected owners. It never inherits the last visited entry's state. An explicitly
empty correspondence removes that owner's references at the selected entry,
rather than falling back to stale geometry. Other occurrences and freshly
resolved downstream Origins remain available. Owned Sketch reframing consumes
the same owner's boundary view. Body-local evaluation transforms these views
through the ordinary Body frame.

`zima_cpp_history_reference_view_tests` exercises source-attached entries before
and after a supplied geometry change, a descendant attached to the changed
container Origin, owned Sketch frames, Body translation and rotation, both Flip values,
positive/negative offsets and signed zero. It also checks occurrence isolation,
repeated switching without mesh-data accumulation and invalid-input rejection
before document mutation. The solid straightening test additionally supplies
actual calculated Revolution/Extrusion reference packets, checking endpoint
position and orientation through two attachment levels, both sides and length
coefficients, followed by repeated restoration within numerical tolerance.

This verifies the reference consumer. The native integration checkpoints above
describe automatic view construction and atomic transactions tested separately.
The helper tests alone do not prove full native-state or GUI acceptance.

## Properties editor checkpoint (2026-10-02)

`cpp/app/solid_state_dialog.hpp` provides one pending-data editor for Straighten
and Restore shape. It consumes the shared internal `PropertiesSubWindow` and
reference-entry controls. It offers a name, all eligible elements or an individual
selection, independent inspection eyes and, for Straighten only, a dimensionless
length coefficient. Selecting an occupied input replaces that source; removing
an item deletes its selection row and re-arms entry. Mode changes clear temporary
inspection without discarding the remembered individual selection.

The lower reference table consumes extra window height while row heights and
upper form spacing remain stable. The coefficient stays compact and retains its
original full-precision value until edited. Restore shape does not rewrite it.
Only OK invokes the injected transaction callback; Cancel discards pending edits.
The shared middle-button double-click confirmation also works over the owning
View. The controller uses `end_entry()` for short middle-click input release.

`zima_cpp_solid_state_dialog_tests` covers both modes, create/edit pending values,
eligible and duplicate picks, replacement/removal, independent inspection,
resizing, confirmation, missing-source rejection and Cancel. It loads the real
application catalogs to verify titles, controls, help, validation errors and
Cancel in Czech, English, German, French and Russian. The dedicated test and the
overall translation coverage/catalog contract pass.

The editor is connected to Modeling and Tree actions through the native atomic
transaction. See the Modeling checkpoint above for actual GUI coverage; complete
geometry and dependency acceptance remains required before release.

## Drawing detail and break checkpoint (2026-10-02)

The native `zima_cpp_solid_state_drawing_tests` scenario now creates a detail
through `edit_drawing_view`, with a circular crop, an explicit 2:1 scale and
an inherited vertical break. It verifies the current calculated geometry after
an unsaved straightening-coefficient change and after disk-only regeneration.
The detail retains its parent, crop and break identity. Its paper gap scales
from 3 to 6 mm, while the model-space displayed extent is independently checked
as the unbroken length minus 17 mm (20 mm removed with a 3 mm equivalent gap).
The ordinary attached dimension continues to report the complete model length.
Preparing precise output retains real edge and measurement geometry.

The further expanded native scenario passes in 0.94 s, including its existing
Restore, Family, Undo/Redo and native-source checks. It now creates a longitudinal
Section through the public `section.create` command in the active source Part
and creates its Drawing view through `drawing.view.create`. Section creation
reports no solid calculation. The test checks the source and Section identities,
calculated view extent and nonempty cut-face hatching in prepared output for the
initial developed state, an unsaved coefficient edit, the restored formed state
and disk-only regeneration. All six ordinary, projected, detail, section and
Family views survive regeneration from native sources.

`zima_cpp_solid_state_drawing_ui_contract` now opens a native snapshot generated
by its CTest fixture dependency. It changes the source coefficient from 1.1 to
0.8 and triggers the real Drawing window's Regenerate action. The attached
dimension and rendered sheet update without advancing the source Part's
calculation generation. View identities, detail hierarchy, crop and Section
links remain intact. The test saves the result and produces nonempty PDF/DXF
exports. The Windows CTest fixture and GUI test pass together in 2.13 s.

Before/after sheet images were inspected: the ordinary and projected geometry,
dimension, hatched longitudinal Section and enlarged cropped end detail are
visible on the sheet. The fixture's detail crop uses the displayed coordinates
after the parent break, as interactive selection does. Independent export
content validation is described below. The treated spline Sweep GUI checkpoint
is recorded in the native integration section above.
No product code
or user-visible strings changed in this checkpoint; localization review found
only test diagnostics and English documentation.

### Independent vector export content check

Fresh Windows GUI exports from the regenerated coefficient-0.8 fixture were
read independently. Poppler reports one A4 page, and its rendered PNG was
visually checked: all four views, the break, readable 138.23 mm dimension,
section hatching and cropped detail are present. PDFPlumber finds vector lines
and curves and no raster images. Five vertical PDF lines measure 138.23008 mm,
matching the analytical length `110 * pi / 2 * 0.8` to the export's precision.
This checks actual PDF content, not just the intermediate viewer image.

The DXF uses millimetres and contains LINE geometry and TEXT annotations.
An independent group-pair reader measures the same five full-length lines and
reads the dimension `138,23`. The GUI regression now also parses its exported
DXF directly and requires at least four full-length vertical edges within
0.0001 mm and dimension text within its two-decimal rounding tolerance. Thus
a nonempty file containing stale coefficient-1.1 geometry cannot pass.
This fixture validates the ordinary, projected, section and detail export
paths for this source; it is not exhaustive coverage of all drawing options.

## Native rounded spline profile checkpoint (2026-10-02)

### Native spatial-rectangle frame correction

The spatial document suite now also tests a two-level attachment chain on this
rectangular spline source. A child Extrusion references the source's persisted
final vertex and end-cap plane; a descendant references the child's Origin.
Changing the coefficient from 0.9 to 1.1 moves both actual attachment positions.
Restore returns both calculated semantic anchor points to the independently
retained formed source endpoint without rewriting the authored child definitions.
Both plane-side choices are exercised at zero offset: the child's actual end
planes have opposite axes, and each axis and point identity survives Undo/Redo,
native save/reopen and cold regeneration. The complete expanded spatial suite
passes in 42.99 s; localization validation passes in 4.38 s. This checkpoint adds
test diagnostics only and does not modify the shared placement contract.

The expanded `zima_cpp_solid_state_spatial_sweep_document_tests` now includes a
centered 2-by-1 rectangle on the same spatial interpolating spline as its circle.
It passes the independent area-times-length volume check, Straighten, unchanged
confirmation, native save/reopen, cold regeneration, Restore and Undo/Redo.
The complete spatial document suite passes in 33.68 s.

The failure exposed a frame mismatch: inherited Sweep stations used the shortest
rotation from the original normal to each tangent, while Straighten normalized
them with accumulated corrected-Frenet transport. A single inherited profile on
a smooth spline route now uses that accumulated transport in both calculation
and validation. Explicitly authored multiple station profiles, sharp routes,
fixed frames and lofts retain their existing paths. A centered circular profile
also retains its existing calculation because in-plane orientation does not
change its shape. Full boundary and hole comparisons remain mandatory; equal
area alone never establishes a constant section.

Independent BREP plane sections now probe each cubic span at parameters 0.25,
0.5 and 0.75. Across nine sections the maximum perimeter difference from the
2-by-1 rectangle's 6 mm perimeter is 0.0000575935 mm. The requested linear
tolerance is 0.0001 mm. These samples provide independent geometric evidence;
they do not certify every section along an arbitrary spline.

The same nine sections now also check four edges, chord side lengths, adjacent
edge directions and midpoint bow. Maximum side-length error is 0.0000158442 mm,
maximum absolute right-angle cosine is 0.0000202591, and midpoint deviation from
the edge chord is below 0.00000000005 mm. The test requires each side and bow
error, and the right-angle error converted to displacement over the 2 mm side,
to remain within the requested tolerance. Total perimeter error is bounded by
four times that tolerance.

The Sweep3D calculation fingerprint is revised so previously calculated results
cannot conceal the changed inherited-frame calculation. Authored profile data,
reference identities and document extensions are unchanged. This checkpoint adds
no user-visible strings; the translation contract passes. Ordinary 2D Sweep,
Curve3D/Sweep and Sweep command regressions also pass.

The separate Windows verification application also passes the expanded GUI
scenario with the 2-by-1 spatial rectangular profile, alongside its existing
circular 2D/3D and H-Sweep scenarios. It checks Tree and View selection, Cancel,
Straighten, Properties rollback, unchanged OK, Restore through state ancestry,
middle-button confirmation, Undo/Redo and native save/reopen. The same run checks
the existing attached Sweep properties scenarios. This does not certify all
possible spline treatment chains or replace the user's development executable.

Scope check: native 3D Sweep still disallows holes in its authored profile through
the existing `fill_sweep_sections` input policy, unlike native 2D Sweep. A probe
with two profile holes therefore failed before reaching Solid Straightening.
This checkpoint does not expand that separate source-command capability. The
supported 2D hollow-profile and post-source subtractive-hole checks remain
applicable; they must not be described as native 3D hollow-profile coverage.

### Verified planar rounded profiles

The native 2D spline lifecycle now covers a centered rectangle, a rectangle with
four R0.2 Sketch corner fillets, and the same rounded profile with a centered
R0.1 circular void. Independent areas are multiplied by the analytic parabola
length; the centered symmetric sections require no centroid-offset correction.
Each profile passes Straighten, unchanged-state confirmation, native save/reopen,
cold regeneration, Restore and Undo/Redo while retaining its authored Sketch.
Together with the circular source-edit scenario, the suite passes in 14.75 s.

The rounded profiles initially failed before body calculation because profile
correspondence obtained materialized corner vertex IDs from the extrusion
adapter but searched for their positions in the unevaluated Sketch. The shared
Sweep correspondence function now evaluates active corner fillets through the
existing `evaluated_profile_sketch()` implementation before building both the
outline and its point map. It retains the existing corner/parent identities,
does not alter the authored Sketch, and leaves profiles without active corner
fillets on their existing path. This also applies to ordinary Sweep preparation;
it is not a Solid Straightening-specific alternate identity scheme.
The ordinary 2D Sweep, Curve3D/Sweep and native Sweep command regressions pass
against the changed library (three tests, 28.40 s).

This verifies planar spline profiles through native commands. The separate
spatial rectangular-profile GUI checkpoint is described above; the native
integration section also records the treated spline Sweep GUI checkpoint.
Localization review: no new user-visible strings or error messages were added.

## Command icons

`resources/icons/straighten.svg` depicts a curved profile becoming a straight
profile. `resources/icons/restore-shape.svg` depicts the reverse transformation.
They are registered as `:/zima/icons/straighten.svg` and
`:/zima/icons/restore-shape.svg` in the application's Qt resources and used by
the commands and history tree.

Both use the existing 24-unit SVG grid, rounded 1.75-unit strokes and azure
`#00D1FF` destination geometry. Source geometry uses `currentColor`, resolved by
the existing shared icon renderer from the Qt palette. No new palette handling
or fixed light/dark background is introduced.

The icons have been rendered with Qt SVG at toolbar size and enlarged on light
and dark backgrounds. The Windows application builds with the new resources.
Localization review: the assets add no action labels, tooltips or other runtime
UI strings; their English SVG titles are asset metadata. The existing five-language
translation coverage and catalog contract passes. Command and dialog text is
localized in all five supported languages.
