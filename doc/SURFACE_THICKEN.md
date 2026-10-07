# Thicken Surface

**Thicken Surface** (`Zesílit plochu`) is in the surface command group, beside
Surfaces from solid. Select one calculated free surface in the active editable
Body and enter a positive thickness. Selection uses the actual input boundary;
faces that have already been consumed or belong to a solid are not offered.
A face of a sewn shell or of a solid converted to surfaces is also eligible.
Selecting a patch of a connected sewn shell selects that whole surface for
thickening. A separate free face is thickened individually.

- **First side** offsets along the oriented source-face normal.
- **Second side** offsets in the opposite direction.
- **Symmetric** puts half the entered thickness on each side. The entered value
  is the total thickness, not the distance on each side.

Thickness is measured normal to the surface, including curved surfaces. This
is different from an extrusion along one fixed vector. The source normal, not
the camera orientation, defines the first side. Reversing the source orientation
reverses the physical first/second sides; the authored mode is persisted.

The operation consumes the selected free face or its complete connected sewn
shell. Independent sheets remain surfaces. The new solid uses the ordinary Add Boolean path, so
touching or overlapping preceding solids are joined without counting overlapping
material twice. Unrelated free surfaces retain their geometry and owners.
An offset that cannot produce a valid solid is rejected without committing.
Zero thickness is invalid here; Surfaces from solid remains the separate command
for a zero-volume skin.

## Editing and references

Creation and editing use the same internal Properties window. The source field
arms the common original-face picker for entry or replacement. The required
source cannot be cleared; the eye inspects the complete connected surface
identified by the stored anchor face, independently of reference entry.
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

OCCT's normal-offset solid builder operates on a copy of the selected exact
face or its connected shell. Shell calculation joins adjacent offset patches
at their intersections. Symmetric calculation first offsets by minus half the thickness,
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
Each shell patch supplies its own cap ancestry. Selecting a different anchor
patch of the same shell preserves the resulting child identities.

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

The 2026-10-07 native extension checks two-patch coplanar and right-angle sewn
shells in all three directions, selecting either patch. It checks valid solids,
independent volume equations, complete consumption of the selected shell,
anchor-independent child identities, cold regeneration and native persistence.
Viewer-data adjacency checks cover inspection of both sewn patches without a
kernel calculation. The shared GUI layout matrix verifies the compact initial
height, wrapped notes and stable anchoring in all five languages (370 cases).
Actual GUI verification also passed on the connected, multi-patch Ventilation
Window shell: common hover/click, whole-shell inspection and OK consumed every
patch and produced a positive valid solid. The complete FORM/Thicken GUI matrix
took 76.72 s, including creation, replacement, transactions and save/reopen.
Linux execution of this extension belongs on the Linux host.

The actual Ventilation Window exposed a smooth connected shell containing
parametrically C0 spline patches. OCCT's joined offset rejects those patches
before producing geometry. For that specific kernel error, thickening now uses
OCCT's supported simple whole-shell offset. It preserves the original surface
definition and must still pass the same solid, self-intersection, volume and
complete native ancestry checks. It does not approximate the source surface or
reduce display/calculation precision. Existing joined-offset behavior remains
the primary path for sewn planar corners. The real Window passed in all three
1 mm directions on Windows (35.03 s), including independent normal-offset
equations on every source patch, retained original skin vertices in both
one-sided modes, a single valid solid and native ancestry. The existing surface
regression also passed. Linux execution remains unverified.
