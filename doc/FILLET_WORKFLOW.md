# Fillet workflow contract

Fillet now shares the broader edge-treatment implementation with Chamfer. The
common contract is in [`EDGE_TREATMENTS.md`](EDGE_TREATMENTS.md); this document
retains the Fillet-specific background and invariants.

This document is the implementation contract for Part Fillet creation and
editing. User-facing behaviour is also described in
[`UZIVATELSKY_MANUAL.md`](UZIVATELSKY_MANUAL.md).

## One window, two operation contexts

Fillet creation and editing use the single `FilletPropertiesDialog` class.
There must not be a second create-only or edit-only dialog. The same form always
contains:

- the radius field;
- the selected-edge list and edge-removal action;
- `OK`, `Apply`, and `Cancel`.

The caller supplies only initial data and callbacks. Creation starts with an
empty edge list and a default radius. Editing initializes the same widgets from
the existing feature.

## Button and mouse semantics

`Apply` validates the radius and all selected edges, calculates the result, and
keeps the window open. `OK` runs the same validation/calculation and closes the
window only after success. A failed OCCT operation leaves the window open and
preserves the last valid body. `Cancel` closes the window and restores the state
at the last successful Apply (or the state before opening when Apply was never
successful).

A short middle-button click maps to `Apply`; a middle-button double-click maps
to `OK`. These gestures work with the pointer over both the properties window
and the 3D view. A middle-button drag remains camera rotation and never applies
or accepts the dialog.

## Creation

The Fillet command starts a multi-edge `SelectionRequest`. A left click selects
an edge and Ctrl+left-click extends or reduces the selection. Removing an item
from the dialog removes the same edge from the selection. All selected edges
share one radius.

During creation, `Apply` is a transient preview only. It must not insert a
Fillet container into history. Every repeated preview is calculated from the
unchanged input body, never from the previously previewed fillet. `OK` validates
once more, creates exactly one Fillet container, stores all stable `EdgeRef`
values and the common radius, rebuilds the final body, and closes the dialog.

## Editing and rollback

Properties can be opened from the Fillet tree item or from the context menu of
a selected fillet in the view. Entering edit mode rolls history back to the
boundary immediately before the edited Fillet. Consequently the original
rounded result is not displayed while choosing replacement edges; the sharp
input edges are available instead.

This uses the common container rollback and view behaviour defined in
[`HISTORY_EDITING.md`](HISTORY_EDITING.md); it is not a separate Fillet-only
rollback implementation.

The edited Fillet stays visible in the tree between the preceding body and the
`Insert here` marker and is highlighted green. Later history items are visibly
suppressed for the duration of the edit. `Apply` modifies the existing feature
and refreshes its preview without creating another container. `OK` performs the
same operation and restores normal end-of-history evaluation.

## Selection and topology rules

Viewer indices are transient and must never be persisted. Each accepted edge is
resolved through `SelectionController` to a stable `EdgeRef`; the feature stores
the complete `edge_refs` list plus the common radius. The legacy `edge_ref`
field mirrors the first reference for compatibility.

Selected fillet boundaries are highlighted by edges only. Do not highlight the
whole face or body as a substitute. A single left click selects/highlights the
fillet, a left double-click exposes its editable radius dimension, and
Properties from the context menu opens the unified dialog.

## Failure invariants

- No selected edges means Apply/OK cannot complete.
- An invalid radius or incompatible edge set must not damage history.
- Failed recalculation keeps the dialog open and reports the kernel error.
- Apply during creation never creates a history object.
- Apply during editing never duplicates the edited feature.
- Cancel never leaves a transient preview as the document result.
