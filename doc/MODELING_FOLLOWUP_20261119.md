# Modeling follow-up agreed on 2026-11-19

Status: implementation completed for Windows build 2026111905. The current
[verification matrix and explicit limits](verification/20261119-modeling-completion.md)
supersede the earlier partial status below. Windows 2026111906 is published;
committed-source packaging, signing and anonymous public download checks passed.
The release record contains the exact artifact identities and verification.
The final publication build is 2026111906; it additionally builds the new Bend
end-constraint test in both platform packagers and requires its registration.

The user authorized finishing the entire list, commit, push and a Windows GitHub
release on 2026-11-19. This supersedes the earlier wait-for-review instruction.

Post-publication reports add RIP Sketch Tree selection and the half-height Bend
in automatic conversion of `25.prtz`. Their fixes and finite tests are tracked
in the [split-boundary verification record](verification/20261119-sheet-rip-and-split-boundaries.md)
and [Windows 2026111907 release record](releases/2026111907.md).

## Completion checklist

- Cached complete Fillet routes, symmetric/two-sided current face picking and
  exact curved Sheet-from-Body continuation partitions are implemented.
- Shared unresolved-definition confirmation covers Part and Assembly modeling
  calculation paths. Actual Fillet/Chamfer GUI and eight Assembly cut variants
  verify the first/second OK transaction; every command-specific validation
  combination is not exhaustively covered.
- Angled Parallel + C survives actual GUI Trim, subsequent edits and reopening.
- Virtual curve corner rounding and the separate native Curve fillet command
  are implemented without automatically inserting a radius dimension.
- View-only Flat-end dimensions/constraints and propagation to the following
  Bend are implemented, with planar full-material Up-to limits.
- Independent branches, parallel calculation, source deletion/release, history,
  relative-zero corrections, CC and embedded Sketch repair remain covered by
  their mandatory/native/GUI regression paths.
- The broad placement and Sweep3D test fixtures now reflect the established
  first-reference orientation and nested-table resizing contracts. The protected
  placement implementation was not changed.
- Five-language coverage and catalog/UI validation pass. The stable root BAT
  continues to launch the local native C++ executable.
- Commit, push and signed Windows publication are authorized; exact artifact
  acceptance is tracked in [the release record](releases/2026111906.md).

The later Sheet Transition discussion supplied no concrete new behavior and
remains a future specification task. Linux execution belongs on the Linux host.

## Earlier repair evidence (before the completion batch)

### Two-sided Fillet route regression (`24.prtz`)

The saved production copy reproduced a preview identity defect: fused Start/End
side faces retain two valid authored aliases. The support packet previously
omitted both guides on the straight cap edges, leaving only the intermediate
Fillet arc in the preview. Its internal vertices were then incorrectly stored
as the whole route's endpoints. Resolve these guide supports using the selected
cap edge's exact Feature-side ancestry; retain both native face aliases.

After regeneration, actual GUI selection from each of the three members on
both caps of `24.prtz` calculates R = 4 mm with ZIMA. All six cases pass native
reopening, Undo/Redo and unchanged OK without another calculation. The private
GUI also verifies first/second OK for a deliberately stale terminal reference,
native reopening and Undo/Redo. Logs: `build/fillet24/gui-seed*.log` and
`stale-seed0.log`. The user's original is untouched.

The ordinary edge-treatment command regression now includes one-sided,
symmetric and unequal two-sided Features, both caps, forward/reversed routes,
three successive radii, complete preview supports/terminals, native cold
reopening and Undo/Redo. Explicit regeneration prunes missing members while
retaining a wholly unresolved route. It can rediscover new tangent interior
members only when every survivor and both persisted terminals match; partial
selections must not expand. Tests cover this repair, full reference loss and
deliberate partial selection. Removing the source corner round creates a sharp
right-angle route that the existing sweep cannot calculate; that definition
remains unresolved, and restoring the source automatically restores its arc
and successful calculation. This is not a claim that arbitrary sharp route
corners are now supported. Current native matrix log: `command4-tests.log`.
The ten rebuilt native regression suites pass (286.92 seconds total): direct
batch, support ancestry, planar Fillet/Chamfer, treatment command/query,
Sheet-from-Body exchange, Bend, Profile and unresolved confirmation. See
`build/fillet24/regression-tests.log`.

The final normal Windows build also passes all six actual-cap selections,
the stale-terminal first/second OK case, converted Bend face-intersection C
editing and all five UI languages. Logs are `build/fillet24/normal-*.log`.
The root `zima-cad.bat` launches this rebuilt local executable. These checks
do not constitute a packaged Windows release or Linux verification.

No new format, localized UI text, platform branch, or shared placement behavior
is introduced by these route repairs. Native aliases remain the source of
identity. Linux execution remains unverified.

The following results concern the current working changes, not an already
published release. Diagnostic logs and copies live under
`build/sheet-conversion-20261119/` and are not product dependencies.

- Native Bend end-profile matrix: 24 combinations of endpoint, native/external
  support, orientation, deleted/reference/suppressed difference dimension;
  successive numeric changes and permitted/locked drags, independent endpoint
  equations, reference identity, source immutability, persistence and Undo/Redo.
  The mandatory `zima_cpp_bend_end_constraints_contract` passes. Embedded GUI
  dimension/segment deletion, return to Properties, repair entry and Cancel
  pass. The converted-end GUI test adds an isolated preceding support Body to
  the diagnostic copy and verifies real face/plane intersection display,
  point-first C picking, independent support equations, return to Properties,
  commit and native reopening without recreating the deleted dimension or
  changing the source (`g-select16.log`). The attempted extrapolation of a
  distant finite cap intersection correctly rejects C; it is not an infinite
  construction line. This does not reproduce every possible face the user
  could have picked in the original transient draft.
- Conversion baseline: native tests cover computed/user-relative corrections,
  both endpoints, signed zero, repeated edits, source radius changes, release,
  dimension identity, cold/warm calculation and native reopening. Baseline
  changes preserve user deltas; release displays effective absolute values.
  Actual View edits of both output corrections through 0.25, -0.5 and 0,
  save/reopen and Undo/Redo pass. First-profile attachment dimensions are absent.
- Source deletion: converted-sheet tests cover source Fillet, source Extrusion
  and source Body removal, frozen geometry/frames, cold regeneration, native
  reopening and repeated Undo/Redo. RIP Sketch deletion also passes: one
  transaction, released children, unchanged volume, cold native reopening and
  two successive Undo/Redo cycles (`rip-deletion-tests.log`).
- Parallel with endpoint C: six actual GUI scenarios pass with slanted native
  and external supports, repeated drags, equations, read-only source checks,
  persistence and Undo/Redo. The native Parallel/Trim matrix and existing actual
  GUI Trim junction test pass, including parallel-fragment identity, dragging,
  persistence and Undo/Redo. The GUI fixture is horizontal; the exact angled
  gesture reported in 21.prtz has not been reproduced.
- Corner Closure: native Profile Bend combinations 00/01/10/11 and output
  corrections pass. Actual Bend View edits 10/01/00, persistence and Undo/Redo
  pass. No additional persisted corner state was introduced.
- The saved 23.prtz cursor ends at Body entry 4 while the converted group ends
  at entry 13. The actual Tree drag now restores all 13 rows and visible sheet
  surfaces. Source picking uses the approved metadata-only fallback order:
  converted child, original solid, source Fillet. Adjacent faces are no longer
  added as aliases of the child's material. Exact occlusion filtering of all
  original source surfaces is not claimed.
- Common unresolved-definition confirmation: native new/edit Fillet, Chamfer
  and Shell tests pass. Actual Fillet/Chamfer GUI tests pass for first-error OK,
  changed draft, unchanged second OK without another calculation, middle-button
  confirmation, save/reopen, Undo/Redo and unchanged valid OK. Both GUI tests
  also passed in the normal local `zima-cad-cpp.exe` launched by the root BAT
  (`confirmation-fillet-normal-gui2.log`, `confirmation-chamfer-normal-gui2.log`). Other command
  families and Assembly-owned commands remain verification/implementation
  gates; do not describe this as universal coverage yet.
- Six existing native Fillet/Chamfer suites pass after rebuilding: command,
  query, direct batch, support ancestry, planar Fillet and planar Chamfer
  (`fillet-regression-tests.log`). The new/edit confirmation test also verifies
  cold regeneration of the saved unresolved Fillet/Chamfer definition, not only
  loading its saved display cache (`confirmation-cold-tests.log`).
- Factory inventory: 1,185 native assets, 14 independent Bend assets, zero
  bound Sheet-from-Body assets, six start templates. All 14 Bend assets and
  four Part templates pass current calculation/save/reopen on isolated copies.
  Seven protected production Parts are rejected by both the previous runtime
  and the changed runtime as unsupported format; this is an existing gap, not
  verified compatibility. Verified backups exist and originals are unchanged.
  GUI template creation passes in millimetres and inches in all five languages:
  Part, Sheet, Skeleton and Assembly, active editable context and ordinary
  commands, component insertion and save/reopen (`g-template-mm.log`,
  `g-template-in.log`).
- Mandatory native independent-branch cases pass for separation, point/edge/face
  contact, overlap, containment and common-parent branches. The actual GUI
  attachment matrix passes 24 fragment/Flat/Bend/Profile/endpoint/midpoint cases
  (`g-branches.log`). Parallel calculation remains enabled.
- Localization coverage and catalog validation pass with the new common
  confirmation notice and Corner Closure strings in all five languages.
- The 19-test native regression batch passes 18 tests. The broad UI suite stops
  in `verify_container_frame_dialog`: Edge/Plane/Point has FRONT plus a
  position-only plane, while the test expects TOP as well. The affected shared
  placement path is unchanged by this follow-up and is protected; a separate
  old-runtime reproduction has not been performed. Do not weaken the test or
  change the shared placement contract to conceal this failure.
- The separate dialog layout matrix exercises 660 combinations; five fail for
  Sweep3D surplus-height allocation at 1920 pixels, one in each language.
  Sweep3D layout was not modified by this follow-up. Grouped-history Tree tests
  pass with expanded and collapsed conversion rows (`tree6.log`).
- Windows checks use the native C++ runtime. Linux has not been built or tested
  in this Windows session. No separate platform modeling implementation exists.

The generalized Sketcher Fillet and View-only Flat-end requirements below were
subsequently implemented in 2026111905. Historical failures and incomplete
coverage above describe the earlier repair batch; the current matrix records
the completed work and remaining finite verification limits.

## Sheet from Body follow-up

- Bound Bend first-profile attachment dimensions must not be exposed as
  corrections. The attachment stays on its source edge; editable corrections
  belong to the second/output profile and propagate to the following Flat.
  Identify the profile by role, never by a zero value: output +0/-0 corrections
  retain their independent side meaning. Preserve ordinary independent Bends
  and do not silently erase existing authored offsets.
- Bound Bend output correction dimensions use a user-relative zero. Preserve
  the computed conversion allowance separately from the authored user delta;
  the effective value is their sum. A source radius/geometry edit recalculates
  the allowance while preserving the user delta. After release/explode, retain
  the identical geometry and display the effective absolute value in the native
  independent Bend. Preserve signed-zero side identity, dimension IDs,
  save/reopen and Undo/Redo. This was requested on 2026-11-19; it is not merely
  a label change. Assess the persisted data contract before implementation.
  The user explicitly approved the needed native-format change on 2026-11-19.
  Inventory and verify affected factory assets/templates and isolated copies of
  the protected production project; preserve private originals.
- Ordinary Sheet Metal picking keeps converted Flats/Bends first. A source
  Fillet should be offered on its actual rounded face, not over an adjacent
  Flat. Use existing picking data without additional geometry calculation.
  The user accepts source Solid before Fillet as a fallback if exact filtering
  requires disproportionate work.
- Saved 23.prtz reproduces a disappearing result after moving Insert here
  before Sheet from Body, adding subtractive Extrusion 02, and returning the
  marker to the end. Investigate actual history cursor, calculation and scene
  publication independently. Use only the copy under
  `build/sheet-conversion-20261119/`, never overwrite the user's original.
- Deleting a source Solid/Fillet should release the dependent Sheet from Body
  using its existing release-bindings operation. Preserve the last valid sheet
  shape and ordinary editable Flat/Bend definitions. Deletion and release form
  one Undo transaction; Undo restores both the source and associations. Verify
  regeneration, repair, repeated Undo/Redo and native save/reopen.

## Save an unresolved modeling definition

The user requested this common behavior for modeling commands:

1. The first OK attempts the ordinary calculation. On failure, keep the draft
   open, show the actual error, and explain that a second OK will save the
   unresolved definition.
2. A second OK with the exact same draft saves that definition in one Undo
   transaction, closes the dialog and marks the history feature as invalid.
3. Changing parameters or references clears this acknowledgement. The next OK
   must attempt calculation again. A changed source/input revision must also
   invalidate acknowledgement of an earlier failure.
4. The implementation retains the real input body and authored unresolved
   definition, with existing last-valid-frame handling for broken references.
   It does not persist an edited feature's former successful result as an
   additional display solid. Dependent features must recognize the unresolved
   dependency; retained input geometry must not masquerade as a successful
   new material calculation.
5. Cancel discards pending edits. Save/reopen and Undo/Redo must preserve both
   the authored definition and its unresolved state.

Trace the shared command and history transaction paths before implementation.
Reuse their error/status and last-valid-result mechanisms, preserve the protected
placement solver, and assess whether any required persisted contract changes.
If the native format must change, obtain the existing format-scope agreement
before changing it. Review all applicable modeling command families rather than
adding a Fillet-only confirmation shortcut. Localize feedback in all five
languages and verify the common middle-button confirmation path.

## Reported Fillet failure in 21.prtz

The user reports that a formerly working case fails with ZIMA and OCCT Fillet.
Their failed pending definition cannot currently be saved because OK fails;
the two-step confirmation above addresses that reproducibility obstacle.

A read-only copy of the file saved at 13:05 on 2026-11-19 was obtained. Its
SHA-256 is
`65a739ae6e6e27b0396b36a578d7cb1ceeebd4fd039137d384da4e7f005a1222`.
It contains an Extrusion and ZIMA `Zaoblení 01`, radius 6 mm, with both route
ends set to Cut. Three warm
and three cold recalculations of this saved definition passed, preserving
volume 3541629.6975073777 mm3 and reference status, including native reopen.
This does not reproduce or disprove the unsaved failed attempt. The failing
radius/route/other pending changes remain unknown. Do not invent them or
rewrite the original user model.

Private diagnostic artifacts remain outside Git under
`build/sketch-followup-20261119/`. Before accepting a repair, compare the
previously working Fillet/Chamfer catalog, dependent reference behavior and
transaction recovery as well as the exact newly captured failed definition.

## Sketcher contact, Trim and rounding

### Bend end-profile editing and intersection reference

The user became trapped in the embedded Bend end-profile Sketch of 23.prtz
after deleting an endpoint dimension and attempting C contact with an
intersection of a Flat face. Finishing the Sketch was reproduced in the live
application: `Bend endpoint difference dimension is missing.` The outer window
also refused to close until the edit was completed or cancelled. A Save As copy
was captured under `build/sheet-conversion-20261119/23-end-profile-pending.prtz`;
this saves the committed model, not necessarily its transient embedded draft.

- An endpoint may be driven by native Sketch geometry/constraints instead of
  the generated difference dimension. Preserve the segment/point identities
  and actual solved endpoint; do not recreate a deleted driving dimension.
- An incomplete or invalid embedded Sketch must allow return to its owning
  Properties and Cancel. Validation belongs to the attempted model commit;
  an error must not trap the user in a hidden-parent edit session.
- The face/Sketch-plane intersection must appear as an ordinary reference
  curve in the Sketch plane and participate in C contact through its stable,
  read-only external reference. Verify display, picking and solving separately.
- Verify draft Undo/Redo, repeated endpoint edits and drags, replacement of a
  dimension with C on native/external support, regeneration and save/reopen.
  Add the missing-dimension exit case to GUI regression coverage.

The user separately reported, while constructing the angled outline in 21.prtz:

- Finishing a segment with endpoint C on a support line should still offer
  Parallel to another slanted segment. Contact inference must not suppress a
  compatible directional relation.
- Parallelism was reportedly lost after Trim. Existing native Parallel Trim
  tests already cover several cases; reproduce the actual gesture and graph
  before changing the restoration path or assuming the relation was persisted.
- Preserve established virtual corner rounding. Generalization to line/arc,
  spline and elliptical-arc supports is separate follow-up work.

The accepted separate Sketcher Fillet command takes two curves and a one-time
initial radius, trims the curves and inserts a real tangent arc. Do not create
an automatic driving radius dimension. The user may add dimensions or Equal
Radius later. Preserve native shared-point incidence and independent contacts;
reject unsuccessful attempts atomically. Use one Undo transaction.

Follow the mandatory Sketcher matrix: independent equations, repeated numeric
edits and point drags, applicable dimension/lock/DOF states, both selection
orders, orientations, contact sides, native/read-only external supports, Trim,
save/reopen, Undo/Redo and actual GUI interaction. Record unverified variants.
After the agreed Sketcher work, revisit the sheet transition with the user.

## View-only Sheet Profile end correction

The earlier discussion remains design work: control the end of a generated Flat
using ordinary View dimensions and stable dimension IDs consumed by sequential
calculation. Respect the target surface, profile angle and full material
thickness so material does not protrude through the target. The following Bend
must inherit the corrected endpoint. No additional Properties fields were
requested. This is not included in release 2026111904.

## Bend corner closure in the View

The user clarified and approved the exact View interaction on 2026-11-19:
show `CC = 0` or `CC = 1` beside each of the Bend's two endpoint locations
(`CC` means Corner Closure). Zero disables and one enables that endpoint's
existing corner flag. Double-click edits the value through the ordinary
parameter interaction. Hover explains the full localized Corner Closure name.
Reuse the existing First end / Second end settings and calculation; do not add
a new Properties control or duplicate state. Make this work for generated
Profile Bends as well, preserving both endpoint identities, user corrections,
downstream propagation, native persistence, Cancel and Undo/Redo. Localize the
full label/tooltip in cs/en/de/fr/ru; the agreed CC abbreviation is shared.
