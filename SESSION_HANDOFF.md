# Session handoff — 2026-08-08

## Current state

Work in this session focused on 3D selection/highlighting, container inspection,
Sketcher constraint feedback and template editing. The worktree is intentionally
not clean. In particular, `config/formats/ZE-RAZITKO.tblz` and its numbered
backups are user work and must not be reverted or deleted.

Implemented so far:

- thin, colour-only entity highlighting with cached face outlines;
- point-first picking priority in Sketcher;
- false orange point marker over lines, axes and construction geometry fixed;
- preview constraint symbols placed by the point currently being entered;
- already-satisfied simple constraints skip the numerical Jacobian/solver move;
- double-click container inspection disables unrelated view picking/highlighting;
- defining sketch is shown thin yellow and the resulting solid cyan during
  container inspection and Properties, including after view rotation;
- empty left click or middle-button double-click leaves inspection mode;
- double-click races no longer open a different hovered container;
- right-click gives priority to the currently orange hovered candidate instead
  of a stale blue selection;
- Sketch/Protrusion/Revolve placement references reject Body faces and accept
  only basic container geometry (Point/Axis/Plane);
- imported STEP selection in the tree highlights only its origin;
- obsolete Add/Subtract actions were removed from primitive context menus;
- frame/title-block templates open directly in Sketcher without camera animation;
- template Sketcher uses `Save and close`; normal sketches retain Finish;
- translations for the new command were added to cs/en/de/fr.

Verification at handoff:

- Python compilation and `git diff --check` passed.
- 72 of 73 relevant tests passed.
- The remaining Drawing-format test expects `document.file_stem`, while the
  user-edited `ZE-RAZITKO.tblz` currently contains
  `document.file_stem.&verze`. Do not solve this by reverting the user's file.

## Continue next

1. Manually verify the full container interaction matrix in the running GUI:
   double-click inspection, Properties, Apply, OK, Cancel, empty click,
   middle click/double-click and view rotation, especially in a long history.
2. Verify Sketcher snapping while entering the second point: horizontal,
   vertical, parallel and perpendicular suggestions must actively hold the
   suggested relation, not only display its symbol.
3. Continue the fast-path solver design for simple constraints and dimensions:
   detect already-satisfied equations first; otherwise choose a movable entity
   according to existing constraints before invoking the general solver.
4. Rework the title-block template requested by the user: remove its current
   constraints/dimensions and offset its geometry so insertion is truly at
   `(0, 0)` while landing at the correct frame position. Preserve a backup and
   coordinate this with the user's currently modified template.
5. Design and implement localized title-block tokens and two editable token
   classes: model-parameter-backed fields and drawing-local fields. Edits in a
   placed title block must remain in that drawing and never modify the template.
6. Review STEP import quality. Keep OpenCASCADE for STEP/B-Rep translation
   unless a measured issue justifies a different representation; inspect why
   cylinders are being tessellated/displayed poorly before changing import.
7. Finish any original items not covered above, then update `ROADMAP.md` only
   once the behavior has been verified in the GUI.

## Development rules to retain

Follow `AGENTS.md`: all feature/property editors are internal Qt SubWindows
using the shared Container Properties interaction; create/edit share one dialog;
Apply is transient, OK commits and exits, Cancel restores the last applied state;
history editing rolls back to the real preceding geometry; legacy Part/Assembly
file compatibility is not required.
