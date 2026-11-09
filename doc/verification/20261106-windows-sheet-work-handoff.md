# Windows sheet-metal and pending-work handoff (2026-11-06)

Status update, 2026-11-09: the user explicitly requested completion, commit,
push and a Windows GitHub release after the later Profile and application fixes.
That instruction supersedes the inspection hold below. Edge-attached Profile now
derives the Bend-style references and thickness, creates its owned positive-X/Y
Bend/Flat route when empty, and no longer exposes material side. The finite
implementation and regression evidence is in
[the 2026-11-09 verification record](20261109-sheet-profile-and-properties-rollback.md).
The remaining text is retained as the original handoff and historical scope.

## Purpose and current state

This note preserves the user's exact intent across the requested Windows
restart and the implementation completed afterward. The focused native and GUI
regressions now pass. Do not commit, push or publish a Windows build until the
user has inspected the final `02.prtz` corner and Sheet Profile in the local
development build.

The working tree contains extensive in-progress changes. Preserve unrelated
user work and original project files. In particular, never overwrite the
user's `6HR.prtz`, `01.prtz`, `YYY.prtz`, `POKUD2.prtz`, `XXX.prtz`, or other
project fixtures. Work on copies under `build/`.

## Resolved restart defect: deleting a middle Bend and editing its Flat

Deleting a generated Bend now removes only that definition. Direct dependents
remain as repairable invalid history entries with their last valid placement,
owned Sketches and numeric frame; independent branches remain calculated.
Opening the retained dependent feature no longer enters the stale JSON/crash
path. Deleting the Sheet from Body divider restores the original solid without
invalidating unrelated later sheet definitions. Native deletion, GUI deletion,
Undo/Redo and save/reopen regressions cover these contracts.

## Sheet from Body: binding user contract

The command is an automation of the native sheet-metal operations that already
work when authored manually. It must recognize the selected outer solid skin
and create ordinary editable history features in the same way as the user
would create them:

1. ordinary Flat;
2. ordinary Bend;
3. ordinary following Flat;
4. ordinary Bend corner closure where branches meet;
5. repeat sequentially and stop at the last possible Bend without creating a
   terminal Bend or looping around a closed skin.

Do not introduce a replacement shell, a special non-developable sweep, a
parallel modeling algorithm, or a custom approximation of the finished sheet.
The generated model is acceptable only if the ordinary Unbend command actually
unfolds it. A displayed solid and a cold regeneration are insufficient proof.

The selected outside face is the authored outer guide (Side A). Material
thickness goes inward. Preserve the useful symmetric source-body behavior.
The command follows the Part sheet-metal settings by default; only the existing
explicit override releases it from those settings.

At a junction of neighboring Bend branches, use the existing Bend endpoint
dimensions and existing corner-closure behavior. Do not enable both endpoints
blindly. Choose the branch that must be shortened and calculate its endpoint
against the neighboring sheet's inner side. The rectangular/90-degree case has
a thickness-sized setback; an oblique case must use the actual intersection.
The requested corner gap is zero. This parameter calculation may use the
existing 0.05 mm sheet-joining tolerance, but persisted model geometry and
general accuracy must not be reduced.

Required fixtures include:

- a simple rectangular solid/cube with rounded Bend edges;
- the user's `6HR.prtz`, including the oblique/tapered six-branch case;
- `YYY.prtz`, where one rounded base transition divides into two upper branches;
- the simple earlier `01.prtz` Flat-Bend-Flat case.

For every fixture verify folded calculation, real Unbend, Bend Back, editing a
generated Bend, Undo/Redo, native save/reopen, reference identity, volume, and
that no branch overlaps or produces non-manufacturable sliver faces. Measure
the representative conversion and Bend edit; do not obtain speed by reducing
model precision or supported behavior.

### Wrong direction that must be removed

An in-progress experiment enabled automatic corner closure on both ends and
produced a solid that could not unfold (`Sheet state surface exceeds the
configured tolerance`). Another unused `BendReliefSetbacks` helper began a
separate geometry route. Do not continue either experiment. Reuse the existing
Flat, Bend, endpoint-extension, corner-closure, and Unbend contracts.

## Sheet from Body history divider: binding user contract

For conversion inside the same Body, `Sheet from Body` is a history divider,
not a parent container and not an atomic generated group.

- While the divider exists, preceding solid operations are hidden and excluded
  from mass and center-of-gravity calculations.
- Ordinary generated Flats and Bends follow the divider at the normal Tree
  level. They remain independently editable, suppressible, reorderable where
  dependencies permit, and independently deletable.
- Deleting one generated Flat or Bend deletes only that feature. Dependent
  downstream features remain in the Tree as invalid definitions so the user
  can repair or reattach them.
- Deleting the divider restores the preceding solid. It must not delete the
  generated Flats and Bends.
- Undo/Redo and native save/reopen must preserve this exact behavior.
- Conversion into a different empty Body creates no divider: the source Body
  remains visible, the new sheet Body remains visible, and both contribute to
  mass and center of gravity.

Before the restart, the first Flat carried the material reset and
`generated_features` acted as an owned group. The working implementation now
resets the material at the divider and uses `generated_features` as conversion
provenance. A generated Flat or Bend follows the same history rules as an
ordinary feature.

The same general deletion rule applies outside Sheet from Body: deleting an
earlier feature must retain downstream feature definitions as invalid rather
than silently deleting them. A minimal two-feature dependent-body regression
was explicitly requested.

### Windows verification after the divider and reference-loss fixes

- Native history deletion verifies the stored position and orientation of a
  dependent Body, an Extrusion and a standalone Sketch. Sheet exchange verifies
  that a generated Bend keeps its start and auxiliary Sketches and endpoint
  dimensions when its source Flat is deleted. These cases cover regeneration,
  native reopening and Undo/Redo. The ordinary Flat, Bend, Sheet Profile and
  sheet state suites passed.
- The executable launched by `zima-cad.bat` passed the GUI history deletion,
  Sheet Profile, cross-Body and same-Body conversion tests. The same-Body
  test exercises both deletions from the Tree. The five-language catalog
  contract passed. These changes add no user-visible text.
- On the `01.prtz` copy, the conversion produced Flat, Bend and Flat. The
  folded volume was 154364.01702 mm³ and cold regeneration matched it. Native
  Unbend and Bend Back succeeded and the saved result reopened. The GUI
  same-Body check passed on this copy with the exact source face, including
  deletion, Undo/Redo and DXF export.
- The branched `04.prtz` case now produces six Flats and five Bends, traverses
  both branches outward from the selected face and was visually accepted by
  the user. The omitted rounded intersection is used as the branch-split
  signal rather than as the cut direction.
- The six-sided `02.prtz` case converts, unfolds and bends back. Every branch is
  calculated independently and corner closure meets the inner Bend edges. The
  final second-Bend endpoint adjustment has automated coverage but still needs
  the user's visual inspection.
- The complete native sheet exchange suite, oblique branch assertions, twisted
  sheet, state changes, FORM copies and cold reopen now pass.
- A disposable `6HR` conversion saved by the earlier experimental divider
  implementation failed to open with its stale calculated Body cache. The
  current conversion was regenerated from the unchanged solid source copy.
  Linux verification is pending.

## Sheet Profile (`Profil`): agreed design and verification

The command remains required. It is a container that authors a single planar
route and creates ordinary Flats and Bends from that route; do not introduce a
second/multi-curve material-side representation.

- A free Profile may start with a straight segment and supports the ordinary
  symmetric/one-side/other-side width choice.
- A Profile attached to an existing sheet edge starts with a prepared first
  Bend arc followed by a tangent line.
- The first Bend normally offers `R = T` and determines the extrusion width.
  If there is no first Bend, the first Flat determines the width.
- Later arcs remain user-authored. The command transfers them to ordinary Bend
  features, applying the agreed outer/inner radius interpretation with sheet
  thickness. Tangency must be preserved.
- Reuse the existing `do not follow sheet thickness` checkbox behavior.
- Properties need the existing placement/reference controls and a Sketch
  button. Do not add a redundant width field.
- Tree children remain the real ordinary Flat/Bend features. `Rozložit Profil`
  breaks the container into those ordinary features and preserves their logical
  history order.
- After drawing, the cyan preview wire appears. OK calculates and commits real
  sheet geometry. The native and GUI Profile contracts pass, including save and
  reopen; final visual inspection by the user remains the release gate.

The user approved the Profile icon after extending the azure arc to end with a
horizontal tangent. The sloping straight segment is tangent at the other end.
The Modeling Transition icon drops its vertical divider and the Helical Sweep
icon uses one spiral. The Sheet Form icon should be an azure semicircular bowl.

The user must inspect Profile before any Windows release is published.

## Other pending requests that must not be lost

### Agreed implementation order after the current sheet closure

The user confirmed the following order on 2026-11-06:

1. Optimize FORM Pattern/Mirror as a batch when one Pattern or Mirror contains
   multiple independent FORM occurrences. Reuse one prepared source and batch
   independent placement/Boolean work where the geometry permits it. Preserve
   every occurrence identity, cut/flat/formed operand, picking reference,
   Unbend/Bend Back result, Undo/Redo and native reopen behavior. Measure the
   representative FORM Pattern/Mirror model before and after; do not trade
   precision or reference coverage for speed.
2. After the batch FORM work is complete and verified, begin the promised deep
   Radius redesign. Do not mix that redesign into the FORM optimization or the
   current sheet-metal corrections.

The FORM batch and its seven-case spatial/flat/restored copy matrix are complete.
The Radius redesign remains the next separate task after the user accepts this
build. Keep the implementations separate.

- Preserve the shared Windows/Linux C++ implementation and OCCT 8.0.1 pins.
- Keep the user's skeleton directory and factory/start templates usable. Native
  format changes require regenerating and verifying all affected factory assets.
- Complete the topology verification, including stable references after feature
  deletion and the reversed final path in `03.prtz` / `3D Sweep 01`.
- Retain real geometry in the Drawing view-placement preview. Fix the Drawing
  solid-edge display where a real visible solid edge appeared as a weak line;
  this is a Drawing-view-only issue.
- A completely new first Sketch in a completely new Tree feature must start at
  a realistic on-screen scale. If prior geometry exists, fit to the dimensioning
  bounding box. Both conditions for the clean-first-Sketch case are mandatory.
- Remove obsolete Point/Text checkboxes from every applicable property dialog,
  including 2D Sweep, 3D Sweep, Helical Sweep, Transition, Hole and Holes, and
  close the resulting layout gaps. Preserve the actual feature/container Origin.
- Dimension creation must reset for the next dimension immediately after the
  final placement click. OK closes the window. The same repeated-entry behavior
  must work through the shared middle-button double-click confirmation contract.
- Remove obsolete per-file revision/version work, including its File menu and
  file-manager branches, while preserving native document data and normal Save.
- Sheet-metal regressions remain in `POKUD2.prtz` (Bend 03 placement and Bend
  insertion before Unbend), `XXX.prtz` (Sheet from Body missed faces; deleting
  Unbend lost the model view), and sheet-edge placement must work consistently
  regardless of which eligible edge is selected.
- Bend Properties may allow OK while reporting an error; invalid geometry must
  not be committed.
- After any reference loss, retain the last valid frame of the dependent
  element, including its owned Sketches. This applies to every element and
  is now a permanent rule in `AGENTS.md`. Verify an attached Bend and ordinary
  non-sheet elements through regeneration, save/reopen and Undo/Redo.
- Hole/Holes must not crash in the skeleton and must restore the starting Origin
  and plane selection workflow. General Surface also crashed after the global
  Point/Text cleanup. Audit all affected commands rather than applying a dialog-
  specific workaround.
- Move the forming-library global setting to the Sheet Metal tab.
- Sketch line creation from an existing endpoint should be able to infer both
  Coincident and Perpendicular where applicable. Parallel must work in both
  selection orders. Apply the mandatory full Sketcher edit/drag/dimension matrix
  before claiming those changes complete.
- The earlier BOM observation was user input/model structure, not a remaining
  code defect: the imported assembly had been inserted as one Part and the user
  confirmed BOM creation works after using real Part/Assembly structure.
- The user confirmed the Body Origin was already visible. For the specific
  third container placement reference, use a work Plane through the desired
  axis that intersects the remaining placement trajectory. The user accepted
  this workflow and withdrew the proposed Origin and placement changes.

## Fillet and topology status to preserve

The `6HR.prtz` Fillet 02 red Tree state and missing cyan authored-preview wire
were traced to a stale stored route edge without current face guides. The
working tree contains a unique geometrically identical reconnect and shared
canonical resolver changes in:

- `cpp/modules/document_core/include/zima/document/edge_treatment_selection.hpp`;
- `cpp/app/reference_tree_policy.hpp`;
- `cpp/app/workspace/scene.cpp`.

Focused GUI verification previously passed and the user confirmed the cyan
preview was correct. Re-run the affected native/GUI tests after the sheet work;
do not discard this fix while removing the wrong Sheet from Body experiments.

## Completion gate

Before reporting completion:

1. build the shared C++ implementation;
2. run focused native sheet, Flat, Bend, topology and deletion tests;
3. run the affected GUI tests, including Tree deletion and Profile preview/OK;
4. verify actual Unbend on the rectangular fixture and a copy of `6HR.prtz`;
5. verify Undo/Redo and save/reopen;
6. run localization coverage/catalog validation for Czech, English, German,
   French and Russian (the icon and internal behavior add no visible text, but
   the review is still mandatory);
7. give the user a local `zima-cad.bat` build to inspect Profile;
8. only after user acceptance, commit, push, package the Windows release from
   committed Git data, validate it, and publish it to GitHub.
