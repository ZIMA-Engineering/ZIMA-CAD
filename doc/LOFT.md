# Loft

Loft is a standalone Modeling command immediately after Helical Sweep. It owns
planar 2D Sketches; it does not accept ordinary 3D Curves. This implementation is
later than the immutable Windows 2026102401 release.

## Create and edit

Activate an editable Body and choose Loft. The upper full-width list contains
profile Sketches; the separate lower list contains guide Sketches. Two empty
owned profile Sketches are supplied initially, attached to the Loft Origin. The
owned Sketch Default Origin button selects that same parent datum, including
when replacing existing Body references. Position corrections and Sketch
annotations are evaluated in the Loft frame before the Body/occurrence transform.
Click a Sketch name to open its
ordinary Properties, set its plane and parent-local placement, and use Edit
Sketch to draw. Finish Sketch returns to that Sketch's Properties; its OK returns
to the pending Loft. The last row adds a Sketch. Removing a row removes its owned
definition from the draft. Neither child editing nor inspection commits the Part.

Profiles require at least two Sketches. Guides are optional; a guided network
requires at least two guide Sketches, each crossing every profile in a compatible
ordered network. Use the ordering checkbox and Up/Down to move exactly one item.
The independent eye inspects a Sketch without choosing it for ordering or editing.
The output type and Thin settings are below both Sketch lists, followed by
Add/Subtract. The same bottom order is used by 2D Sweep and 3D Sweep.
Each input uses one connected contour without holes or additional regions.

Solid caps closed end profiles. Surface leaves the skin uncapped. Thin thickens
the skin with inward, outward or symmetric thickness; symmetric divides the total
between both sides. Solid and Thin support Add/Subtract when an input solid makes
subtraction meaningful. Surface uses Add. The common placement controls position
the whole Loft; owned Sketches retain their independent placement within it.
Custom precision controls the geometric tolerance, not display tessellation.

OK validates, calculates once and commits. Unchanged OK does not calculate or
create an Undo transaction. Cancel discards pending parent changes. A short middle
click does not commit; middle double-click invokes OK, including over the View.
Reopening Properties uses the input history boundary and the same dialog.

Ordinary selection shows authored input wires and synchronizes the Tree without
calculating a solid. Double-click uses ordinary inspection; Thin exposes its
thickness dimension. Inspection and editing also show native Sketch dimensions
and nonzero owned placement/profile-offset dimensions in the same resolved frame.
Double-clicking an editable annotation uses the shared numeric editor. Cancel
retains the original definition; committed edits support Undo/Redo.

Every owned Sketch dimension and placement slot, including zero values, receives
a persistent document dimension identifier. Relations can read these quantities;
Family Table and Relations can drive editable slots. Reference dimensions,
constrained coordinates and locked values remain read-only. Reordering Sketches
does not renumber their identifiers. Numeric values and units are independent
of translated presentation text.

An owned Sketch can attach to another available Origin or reference. Its resolved
placement is persisted in Loft-local coordinates. If an anchor disappears, the
last valid local placement and Sketch frame are retained with a missing-reference
warning. Restoring the anchor resumes reference following. This does not remove
the original reference or its side identity. All owned geometry and identities
live in the native Part.

## Calculation and limits

Profile-only transitions use OCCT ThruSections. Guided transitions use Gordon's
exact construction from native curves. No sampled approximate fallback is used.
Closed guide contours are split into all branches between opposite end profiles.
Every resulting branch participates; portions outside the Loft span are excluded.

A kernel Done result alone is insufficient. Contact, reparameterization, profile
and guide deviations must satisfy the requested tolerance, and the resulting
B-Rep must be valid. Some rational networks are rejected even with valid contacts:
a tested three-circle/two-closed-circle network exceeds the profile deviation
budget in OCCT 8.0.1. It is rejected rather than silently changing the profiles.
Arbitrary curve networks are not guaranteed. Thin additionally requires a valid
native offset; self-intersections and incompatible networks can be rejected.

See the [finite verification record](verification/20261024-loft.md) for tested
cases and explicit gaps. The command does not introduce legacy-file conversion.
