# Sketch axis endpoint contact and symmetric line distance

Date: 2026-10-18. Windows native implementation; Linux execution is unverified.

## Defects and changes

The saved `Projects/HFB-CSN021111-M10x050.prtz` profile in `Vytažení 004`
contained a tilted wall with a driving symmetric line-distance dimension.
Creation required parallel lines, but subsequent solving constrained only the
first endpoint. Rendering sampled a different point along the tilted wall.
The dimension now constrains the offset and parallelism, with matching residual
ownership in the degree-of-freedom analysis. Direct dragging of an unlocked
scalar retains parallelism through transient ordinary parallel equations; these
are removed before publishing the transaction. Invalid tilted line distances
are not rendered or silently measured as valid reference dimensions.

Native point merging retained the first selected point's coordinates even when
joining an existing sliding line contact. As for the established circular-contact
path, the merged node now starts at the contacted point. First-selected native
identity remains stable; anchored sources and contact constraints remain intact.
No placement implementation or native serialization schema was changed.

## Verification

- Native Sketcher contracts pass, including the existing 288 trim variants,
  128 FORM sweep cases with 1920 edits/drags, and 64 circular contact cases.
- Added horizontal/vertical, both material sides and endpoint orders; repeated
  edits and drags; locked/unlocked driving and reference states; fully fixed
  rejection; native serialization and independent endpoint equations.
- Added axis endpoint to sliding profile endpoint merge in both input orders,
  preserving one shared node through serialization.
- Dimension command contracts pass: factories, editable and reference values,
  atomic rejection, Undo/Redo, persistence and explicit body regeneration.
- On a disposable copy of the user's file, an explicit 1000-iteration solve
  resolves the already-invalid saved profile; both merge orders then pass.
  Additional verticality is correctly redundant once the distance owns
  parallelism. The merged copy passes actual feature regeneration, Undo/Redo,
  save and reopen (`build/sketch004-inspection/HFB-CSN021111-M10x050-CHECK.prtz`).
  The original Project was not overwritten. The earlier axis-merge input
  snapshot had SHA-256 `09280F7EBE71440ADC4DF0878F91AE97420EC985092CB8DCEB4A6FBB0332FA26`;
  the later user save used for value-edit verification is recorded below.
- Targeted GUI axis endpoint merge passes both input orders and preselection,
  saves and reopens the shared node. The broader existing Coincident GUI run
  stopped at a missing circle candidate for preselection case 2; it is not
  claimed as passing. The new targeted case does not cover that circle path.

The saved invalid profile needs explicit resolution or deletion/recreation of
its erroneous dimension; no silent document migration is introduced. Native
and external support combinations beyond the existing suites, mouse dragging
of symmetric distances in the GUI, and Linux remain verification gaps.

Localization review: no product UI text added or changed; new text belongs to
English test diagnostics and this development note only. The existing UI
translation/catalog verification passed for cs, en, de, fr and ru; evidence:
`build/sketch004-translations.out`.

Evidence: `build/sketch004-contract-tests.log`,
`build/sketch004-command-tests.log`, `build/sketch004-axis-merge-ui.out`,
`build/sketch004-coincident-ui.err`, and `build/sketch004-inspection/`.

## Coupled value editing follow-up

The updated saved user profile (11:23) exposed a second defect: dragging relaxed
both unlocked dimensions, but explicit value editing retained the other driver.
The angular projection preserved the chamfer segment length, alternating against
the symmetric wall instead of letting the chamfer endpoint slide. The ordinary
angle solver now recognizes a single anchored source contact and intersects its
angular ray with the exact support of the other endpoint's symmetric distance.
Every remaining equation is still verified by the existing transactional solver.

Both dimensions accept successive edits on a disposable copy of the actual
profile: diameter 14, 16, 12 and back; angle 25, 30, 15 and back. Added 32
orientation, endpoint-order, native/external-anchor and lock-state matrices.
Independent checks retain the source contact, wall parallelism, diameter and
chamfer angle. Existing native Sketcher and dimension command suites pass.
Evidence: `build/sketch004-value-contracts.log`,
`build/sketch004-value-command-tests.log`, and the per-dimension JSONL logs under
`build/sketch004-inspection/edit-831` and `edit-833`.

The current user save was also tested through real GUI inline value entry,
view-only parameter inspection, repeated Properties and owned-Sketch edits,
Cancel/OK, Undo and saving (`build/sketch004-coupled-value-ui.out`). The fixture
was a separate byte-identical copy of the user Project.

The user subsequently specified diameter **11 mm** and angle **30 degrees**.
Both input orders pass on separate copies of that same save. Independent checks
confirm wall radius 5.5 mm, a 30-degree chamfer and the unchanged source contact
at (5, -32) in Sketch coordinates. Undo/Redo, regeneration and save/reopen pass.
Evidence: `build/sketch004-exact-11mm-30deg/` and
`build/verify-sketch004-exact-values.py`. The preserved user save has SHA-256
`de13b1d6ba7bc3261f387a49eb610e1866d608e0c89a2654f59bf1ca81d5e934`.
