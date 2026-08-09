# ZIMA-CAD development rules

## Property and parameter dialogs

- Every newly created property, feature-parameter, or editing dialog must use
  the same in-application `Qt.WindowType.SubWindow` presentation and visual
  style as the existing Container Properties window.
- Such dialogs must be parented to the main application window, remain inside
  its bounds, use the shared internal properties title/style conventions, and
  be positioned with the existing properties-window helpers.
- Do not introduce free-floating native/system dialogs (`QInputDialog` or a
  top-level `QDialog`) for feature properties or editable model parameters.
- Reuse or extract the shared properties-dialog implementation instead of
  independently recreating its window flags, styling, movement, confirmation,
  and placement behavior.
- Creation and later editing of the same feature must use one dialog class and
  one interaction contract. Do not maintain separate "create" and "edit"
  property windows; inject only the operation callbacks and initial values.
- Feature dialogs that support staged calculation always expose the same
  `OK`, `Apply`, `Cancel` controls. `Apply` validates/calculates and keeps the
  dialog open, `OK` performs the same validation/calculation and then closes,
  and `Cancel` discards changes made since the last successful Apply.
- A short middle-button click invokes Apply and a middle-button double-click
  invokes OK even while the pointer is over the 3D view. Middle-button drag is
  reserved for view navigation and must not confirm a dialog.

## History container editing

- Rollback is a general container-editing rule, not a Fillet-specific feature.
  Opening Properties for any history container evaluates and displays the model
  at the boundary immediately before that container. The edited container
  remains visible in the tree as the active green item; downstream containers
  are suppressed only for the edit session.
- Apply may update the edited feature and its preview, but must not create a
  duplicate history container. OK is Apply plus ending the edit session.
- Creation previews must likewise remain transient: repeated Apply operations
  recalculate from the unchanged input body and only OK inserts the new history
  container.
- During rollback the 3D view shows the real input geometry before the edited
  container, never the cached final body. Picking resolves against this input.
  Apply may replace it with a transient preview; OK or Cancel restores normal
  full-history display, selection mode, and highlights.

## File-format compatibility

- Backward compatibility with legacy Part and Assembly files is not required.
  This includes old `.prt`, `.prtz`, `.asm`, and `.asmz` documents.
- Do not add migration branches, legacy topology fallbacks, compatibility
  adapters, or duplicate old/new execution paths for those formats.
- Prefer the simplest, fastest, and most reliable current data model even when
  that intentionally makes legacy documents unsupported.
- When redesigning topology or serialization, remove obsolete compatibility
  code instead of preserving it behind conditionals.

## OCCT boundary

- Use OCCT only as the solid-modeling kernel for calculating body geometry.
- Application UI, property dialogs, Sketcher entry and interaction, picking,
  highlighting, topology identity, feature references, external sketch
  references, and their projections must use ZIMA-CAD's persisted data model
  and viewer data, not live OCCT traversal or reconstruction.
- Resolve and persist all reference data needed by later editing when a body
  calculation is explicitly performed. Opening or editing an already
  calculated feature must consume that persisted data without invoking OCCT.
- Do not hide OCCT work behind refresh, selection, overlay, tree, toolbar,
  properties, or hover code paths. A user action may invoke OCCT only when it
  explicitly requests a body calculation such as Apply, OK, or model
  regeneration.
