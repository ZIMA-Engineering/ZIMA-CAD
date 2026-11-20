# Inspection history reordering and Loft preview follow-up

Status: local Windows verification; not a published release. These changes use
the shared C++ implementation. Linux execution remains unverified.

## Reordering across inspection records

The Tree now permits a feature to cross Body measurement and Measurement rows
when its real dependencies permit the move. Inspection records can also move to
a different history boundary. Each changed move is one Undo transaction; a
no-op is not a transaction. Native reference dependencies, including a measured
centroid consumed by another element, still reject invalid moves atomically.
An inactive or different Body does not become an editable destination.

The existing native storage places Body measurements before ordinary
measurements at the same boundary. Reordering normalizes to this presentation;
it does not introduce an unstored order between the two inspection families.
No native format change or new user-visible string is required by this fix.

Native tests cover feature and inspection moves in both directions, changed
measurement boundaries (independently expected cube volumes 64 and 1000),
read-only queries, no-ops, source and centroid dependency rejection, inactive
and cross-Body rejection, save/reopen, and Undo/Redo.

The GUI test performs mouse drags in the actual Tree, moving a Fillet across
both types of measurement and moving each measurement. It checks rejection of
an invalid source move, Undo/Redo and native persistence. The run passed in
2.88 s; evidence is under
`build/rip25-20261120/review-gui/inspection/`.

## Loft correspondence markers

The Loft definition preview now uses the existing native correspondence-point
generator. It excludes Sketch helper midpoints, circle centers and datum
markers. The View paints those markers and their order labels in azure. The
separate placed Origin presentation is retained. Opening and previewing do not
calculate a solid or change the document.

Native Loft document tests passed, including the existing dimension/drag matrix
(96 dimension states, 144 edits, 108 degree-of-freedom drags), profile-boundary
checks and persistence. The expanded GUI matrix passed in 22.03 s: all five
languages, placement and Sketcher alignment, actual View reference picks,
owned Sketch editing, point-order controls, resizing, rollback, unchanged OK,
middle confirmation, ordinary selection/inspection, Thin sides, Undo/Redo and
native reopen. Evidence is under
`build/rip25-20261120/review-gui-2/loft/`.

The dark-theme screenshot was inspected: the numbered profile markers are
azure and no auxiliary midpoint or center markers appear. This initial run
predates the two-click View point-order editor. Subsequent dark/light rendering
and mouse-interaction evidence is recorded in
[Profile correspondence direction and Loft output](20261120-profile-direction-and-loft-output.md).

## Subsequent work

The user approved persisting a correspondence direction for the View editor,
including factory assets, templates and protected Toyota copies. The linked
follow-up report records that extension, Surface Sweep verification and the
3D-curve context matrix. The disappearance was not reproduced in that matrix;
it is not presented as a diagnosed and repaired defect.
