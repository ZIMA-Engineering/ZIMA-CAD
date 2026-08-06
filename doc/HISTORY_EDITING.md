# History-container editing and rollback

This is the shared contract for editing every Part history container. It is not
specific to Fillet, Protrusion, Revolve, Sketch, or a primitive.

## Entering Properties and the tree

Opening **Properties** establishes an edit boundary immediately before the
selected container. The tree remains structurally visible in this order:

```text
Body
├── preceding history containers
├── edited container          <- green, active, still visible
├── Insert here               <- active history boundary
└── downstream containers     <- suppressed for this edit session
```

The edited container must not disappear, be struck through, or move outside
its real history position. Green identifies the item currently being edited.
Only later containers are shown as suppressed.

## View behaviour

On entry, the 3D view is rebuilt at the boundary immediately before the edited
container. It shows the real input body for that operation. The edited result,
all downstream results, and any cached final body must be absent. Picking and
stable-reference resolution operate on this rollback geometry.

An `Apply` operation may replace the input display with a transient preview of
the edited result. Repeated previews must be recalculated from the same history
boundary, not cumulatively from an earlier preview. Selection highlights use
the shared viewer conventions and must survive camera rotation. Clicking empty
space clears selection according to the ordinary view-selection rules. While a
properties window is active, a short middle click remains Apply and a middle
double-click remains OK, even when the pointer is over the view.

When editing ends through `OK` or `Cancel`, the view returns to the normally
evaluated complete history. Temporary preview geometry, edit-only dimensions,
edge highlights, selection filters, and topology-picking mode are removed or
restored to their normal state. The current camera orientation and zoom remain
unchanged unless the command explicitly requests Fit.

## Applying and leaving the edit

`Apply` recalculates the same existing container and keeps Properties open at
the same edit boundary. It must not append a duplicate container. Downstream
history remains suppressed while the window is open.

`OK` includes the same operation as `Apply`, ends the edit session, moves
`Insert here` back to the end of history, restores normal tree presentation,
and re-evaluates downstream containers from the changed result.

`Cancel` restores the state saved when Properties opened or by the most recent
successful Apply, then ends rollback and evaluates the complete history again.
Closing the internal title-bar cross has the same cancellation semantics.

## Invariants

- A container cannot consume references belonging only to its own result or a
  downstream result.
- Viewer previews are transient and never alter stored history order.
- Tree selection, view selection, and context-menu Properties enter the same
  edit session.
- A calculation failure preserves the last valid stored result and leaves
  Properties open for correction.

Feature-specific documents, such as [`FILLET_WORKFLOW.md`](FILLET_WORKFLOW.md),
only describe the additional rules layered on this common mechanism.
