# Default Origin shortcut

User agreement: 2026-10-07. Shared container placement offers **Default** directly
to the left of **Origin**. Default consumes the existing complete-Origin Tree
entry; it does not introduce another reference solver or a synthetic origin.

A direct Body feature uses that Body's Origin. A nested object uses the nearest
owning container's Origin. This includes the independent path container of a
3D Sweep and ordinary nested Curve Points. Body placement uses the owning Part
Origin; an Assembly construction uses its owning Assembly Origin. Selection
remains scoped to the exact active occurrence path.

The shortcut arms the first empty position row and invokes the same whole-Origin
entry used by the Tree. It retains the normal plane order, orientation roles,
side choices, offsets, reference identity, cyan preview and transaction boundary.
Manual Origin selection remains available. A completed placement has no empty
position row; Default leaves it unchanged, as complete-Origin Tree entry does.
Existing references are not silently deleted or replaced.

Flat Properties has an explicitly requested feature-specific exception: Default
resets the pending Flat attachment and then consumes the complete owning Origin,
including when its placement rows were already populated. It retires only the
pending edge-attachment mode; other container shortcuts retain the behavior
above. The user can subsequently replace the first reference with an eligible
Side A end-cap boundary edge, which restores the ordinary inherited joining face, endpoint frame and
thickness. Cancel retains the original document. This change does not modify the
shared reference solver, edge-attachment equations or general Origin policy.

Only dialogs implementing the shared placement interface receive the shortcut.
Origin inspection and ordinary reference replacement retain their existing paths.
No OCCT operation is added to Default entry. OK owns calculation and commit;
Cancel discards the pending placement.

## Verification

The focused Windows Body test compares the exact pending references from Tree
entry and Default in all five languages, including Cancel, OK, Properties,
Undo/Redo and native save/reopen. Both paths publish one scene. The nested GUI
test covers pending Part Sweep Points and Assembly Curve Points, immediate
parent identity, resolved local coordinates, button order and cancellation.
Both focused Windows GUI contracts passed, including the actual row position
and immediate-parent identity. Source-face-only Shell offers neither Origin nor
Default. Linux GUI execution remains
to be performed on the Linux host. No persistence format or template change is
introduced by this shortcut.

The requested Flat extension passed complete owning-Body Origin entry in all five
languages, repeated Default, subsequent native Bend end-cap boundary attachment,
inherited endpoint references, locked attachment plane and Cancel. The shortcut
does not broaden the existing set of eligible joining edges.
