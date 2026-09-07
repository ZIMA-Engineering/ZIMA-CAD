# History-container editing and rollback

This is the shared contract for editing Part history containers, not a
Fillet-specific workflow. Properties use the common in-application SubWindow
and expose only **OK** and **Cancel**.

## Entering Properties and the tree

Opening Properties establishes an edit boundary immediately before the selected
history container. The edited container remains green at its history position;
downstream containers are suppressed for the edit session. **Insert here is
hidden while the container is being edited.**

Creating a container replaces Insert here with a green, transient container row.
It displays the pending origin and current children, including a Curve/Sweep's
points and profiles. The outer container remains visible while its nested Point
or Sketch editor is open. Part and Assembly use the same presentation rule.

Tree rows read pending ZIMA data and do not commit them or calculate a body.
The initial Tree projection must not reset the active View reference picker.
Clicking a complete parent Origin fills the child's three positional references
just like clicking the document Origin. Own and downstream geometry remain
invalid references for the parent's placement.

This current transient Tree behaviour is separate from the planned
[multi-body document structure](MULTIBODY_AND_BOOLEANS.md). Independent Body
histories and standalone document-level Boolean steps have a persisted
document/kernel model and an initial Part Tree/Properties integration.
Full modeling/Sketcher frame routing and nested Assembly integration remain in progress.

## View behaviour

The View shows the real input body at the operation boundary, not a cached final
body. Selection resolves against that input. Pending analytical previews may
replace or augment the display while Properties remain open; no hidden OCCT
calculation belongs in Tree, hover, selection, or ordinary UI refresh paths.
Body calculation is explicit through OK or Regenerate.

Highlights use the shared viewer conventions and survive camera navigation.
Clicking empty space clears selection according to the active command contract.
A short middle click ends reference entry and clears temporary inspection; it
does not commit. Middle drag navigates the View. A middle-button double-click
invokes the enabled OK action, including when the pointer is over the View.

## Numeric values

Clicking a numeric spin-box value selects the complete number, including its
sign, while retaining its unit suffix. Deliberate text dragging and modified
clicks keep the normal text-selection behaviour. Keyboard arrows and spin-box
stepping remain available.

Typing a number does not publish every intermediate digit to the preview:
Enter or leaving the field commits the complete value. Decimal input accepts
both comma and dot. Return and numeric-keypad Enter confirm only the field;
they must not accept the enclosing Properties dialog.

Editable parameter dimensions in the View update the same live controls as
Properties. Read-only or constrained values must not be offered as editable
annotations. Construction dimension selection remains available for Points,
Axes and Planes as well as Curve/Sweep parameters after reference entry ends.

## Leaving the edit

OK validates, calculates and commits the pending operation, then closes the
dialog. A calculation failure preserves the last valid stored result and keeps
Properties open for correction. There is no intermediate Apply transaction.

Cancel restores the unchanged input/history and closes the dialog. The title-bar
cross has the same cancellation semantics. A nested editor's OK only accepts its
changes into the outer pending transaction; it does not commit the outer feature.

After the outer edit ends, the normal history cursor and Insert here are shown
again. Pending rows are removed or replaced by committed rows; complete history
display, selection filters and inspection are restored. Camera orientation and
zoom remain unchanged unless the command explicitly requests Fit.

## Invariants

- A container cannot consume its own result or a downstream result as a placement
  reference; dependencies remain one-way.
- Pending previews never change persisted history order.
- Tree and View Properties enter the same edit session.
- Closing the application with a nested editor also retires its hidden parent
  while workspace state is still alive.

Feature-specific documents such as [EDGE_TREATMENTS.md](EDGE_TREATMENTS.md)
describe additional rules layered on this mechanism.

## Shared Origin action in container properties

Every tracked modeling-container properties dialog uses the same `POČÁTEK`
action, including treatments without an editable placement table and the
nested Curve3D / Sweep point editor. `PropertiesSubWindow` supplies the button
when the shared placement section has not already supplied it; it never adds
a duplicate. New Body properties must use this same helper and command binding.

The action temporarily switches the common viewer candidate stream to selecting
containers whose local frames should be displayed. Leaving the mode restores
the previous reference entry or the previous command's selection contract and
filter (including Shell face and Fillet edge selection). It does not commit
parameters. Finishing or directly destroying the owning editor retires the mode;
a nested editor must not leave a dangling callback to its hidden parent.

## Standalone Boolean input snapshots

`DocumentSession::boolean_edit_inputs` returns the exact target and tool
results stored by the last explicit calculation, in document coordinates.
It performs no feature compilation or OCCT work. Missing operation or missing
calculation returns no inputs; it never substitutes the final document solid.
The Boolean step owns its result ID and leaves both source histories intact.
