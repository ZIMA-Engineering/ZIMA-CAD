# Thicken Surface

**Thicken Surface** (`Zesílit plochu`) is in the surface command group, beside
Surfaces from solid. Select one calculated free surface in the active editable
Body and enter a positive thickness. Selection uses the actual input boundary;
faces that have already been consumed or belong to a solid are not offered.
A face of a sewn shell or of a solid converted to surfaces is also eligible.

- **First side** offsets along the oriented source-face normal.
- **Second side** offsets in the opposite direction.
- **Symmetric** puts half the entered thickness on each side. The entered value
  is the total thickness, not the distance on each side.

Thickness is measured normal to the surface, including curved surfaces. This
is different from an extrusion along one fixed vector. The source normal, not
the camera orientation, defines the first side. Reversing the source orientation
reverses the physical first/second sides; the authored mode is persisted.

The operation consumes only the selected free face. Other faces of its source
shell remain surfaces. The new solid uses the ordinary Add Boolean path, so
touching or overlapping preceding solids are joined without counting overlapping
material twice. Unrelated free surfaces retain their geometry and owners.
An offset that cannot produce a valid solid is rejected without committing.
Zero thickness is invalid here; Surfaces from solid remains the separate command
for a zero-volume skin. Whole-shell thickening is not implied by selecting one
of its faces.

## Editing and references

Creation and editing use the same internal Properties window. The source field
arms the common original-face picker for entry or replacement. The required
source cannot be cleared; the eye inspects the exact stored face independently.
A short middle click ends input/inspection without deleting the reference.
Properties contains Name, Thickness, Direction, the source reference and
OK/Cancel; no Origin or container-placement controls are needed.

Opening an existing feature rolls back to its real input boundary before
publishing the scene. Parameter changes remain pending and do not run OCCT or
publish a calculated preview. OK performs the explicit calculation and commits
one history edit. Unchanged OK performs no calculation and creates no Undo step.
Cancel restores the complete result and leaves the stored definition unchanged.
The shared middle-button double-click confirmation also works over the View.
Additional window height remains below the top-anchored fields.

Thickness has a native dimension identifier, the shared value-lock control and
can be bound by Family/Relations while unlocked.
The source is an explicit history prerequisite, including suppression, deletion
and native save/reopen. Undo/Redo restores both parameters and calculated output.

## Calculation and identity

OCCT's simple normal-offset solid builder operates on a copy of the selected
exact face. Symmetric calculation first offsets by minus half the thickness,
then spans the full thickness. Closed-solid orientation is repaired without
changing the authored offset direction. A forward solid wrapper retains the
same cumulatively oriented shells; this avoids OCCT Fillet carrying an inverted
outer wrapper into an invalid new shell. No face, edge, point or precision is
changed by this normalization. Exact B-Rep validity, self-interference and positive volume
are checked before accepting the operand. The existing Add path preserves mixed
surface/solid context and performs the final union.

New caps are children of the native source face; wall faces and start/end rims
are children of the native source edges; start/end points and longitudinal edges
are children of native source points. The feature owner and semantic role make
each child distinct. OCCT history and exact topology adjacency locate those
predefined children; enumeration positions never define their identities.
Unsupported or ambiguous ancestry is rejected instead of inventing IDs.

The ordinary operation-only reference-mesh cache is deliberately bypassed for
this source-dependent command. Changing the source geometry must update both
the calculated solid and its persisted original-reference geometry. The full
history-prefix cache retains its established input validation.

## Verification

Focused native and actual Qt GUI verification covers all three modes, exact
planar and curved volume checks, source orientation, source edits, consumed and
unselected geometry, Boolean contact/overlap, partial solid-skin conversion,
stable children, downstream solid Fillet in all three modes, cold native
save/reopen, history prerequisites, numeric locks, Family exposure,
unchanged OK, changed thickness, invalid edits, Cancel and Undo/Redo. The GUI
checks one base-scene publication on opening, common picking, independent eye,
replacement without deletion and middle-button confirmation. Shared resizing and catalog
tests include the new dialog in all five supported languages. These are focused
checks, not a claim that the repository's inherited full suite is green.
