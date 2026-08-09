# Session handoff — 2026-08-10

## Current state

Work focused on 3D selection/highlighting, edge-treatment inspection, direct
Sketch dimension updates and exact title-block rendering. Commit `a94f0d6` is
on `main` and pushed. Numbered `.frmz`/`.tblz` backups and diagnostic images
remain untracked user data and must not be deleted.

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
- the selection cycle includes supported topology and container objects under
  the cursor while reference mode highlights only the offered sub-entity;
- Fillet and Chamfer hover/selection use persisted boundary-edge identities,
  clear stale highlights when the cycle changes and expose their dimensions
  on double-click;
- a changed horizontal/vertical Sketch dimension directly translates the free
  side when the opposite side is constrained, before using the general solver;
- title blocks render directly from canonical `[Sketch]` coordinates with no
  insertion translation or bounds normalization;
- title-block text preserves semantic alignment, rotation and flip, and its
  millimetre height is consistently based on font cap height.

Verification at handoff:

- Python compilation, `git diff --check`, 63 SketchModel tests and an offscreen
  Qt render of the current title block passed.
- Some older Drawing-template assertions describe fields removed or repositioned
  by the current user-edited `ZE-RAZITKO.tblz`; do not restore deleted fields or
  coordinates merely to satisfy those stale fixture expectations.

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
4. Keep title-block insertion strictly origin-to-origin. Never reintroduce a
   bounds-derived, 10 mm or other implicit placement correction.
5. Continue localized title-block tokens and drawing-local field editing. Edits
   in a placed title block must remain in that drawing and never modify the
   template.
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
