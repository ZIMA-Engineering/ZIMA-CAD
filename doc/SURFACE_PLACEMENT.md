# Surface placement reference modes

The shared container-placement table has a **Type** column immediately after
**Reference**. Empty rows have an empty, disabled selector. A selected face uses
**On surface** by default. Cylindrical and conical source faces additionally
offer **On axis**. The selected mode belongs to the persisted reference, not to
the display triangle or the current dialog.
Direction-driving references without a surface interpretation display the
read-only Type label **Direction** instead of an empty cell.

## On surface

The container origin is constrained to the supporting surface, with the existing
signed normal offset. An orientation-driving reference supplies the surface
normal at the resolved origin. Cylinder and cone calculations consume the exact
surface frame stored during body calculation/import. They do not reconstruct a
surface from the selected triangle and do not invoke OCCT during picking or
Properties previews.

Trim boundaries do not constrain placement: the origin may pass beyond a face's
axial extent or angular cut while remaining on its supporting surface. A regular
surface supplies one positional equation, leaving two translations when used
alone. Its normal leaves rotation about that normal free. Additional references
must be satisfied together; a failed solve retains the last valid frame and marks
the reference state invalid. The cone apex has no unique normal.

This change supports planes, cylinders and cones. General NURBS surface placement
is deferred. Nonplanar triangle packets without an exact supported surface must
not be interpreted as their first triangle's plane. Future NURBS support requires
persisted poles, weights, knot vectors and parameter domains captured during an
explicit import/calculation, followed by ZIMA-side evaluation and projection.

## On axis

The original face identity and exact occurrence path remain the reference owner.
Its analytical axis supplies the position line and direction. The radius is not
an axial placement offset. Axial translation and rotation remain free until other
references constrain them.

Choosing this mode on the first reference seeds the axial station from the middle
of the face's complete axial extent. For a Feature of type Axis, both length fields
are initialized to half that extent. These are editable initial values, not extra
constraints or automatic length dependencies.

The public `placement.reference.set` command accepts `use_axis: true` for this
interpretation; false is ordinary geometric placement. Native references carry
the same `use_axis` field. The shared UI keeps paired orientation entries in sync
when changing modes.

## Feature commands

The green Feature icon appears immediately after Selection, followed by the
standard green separator. Point, Axis, Plane, Sketch, Extrusion and Revolution
are shortcuts into the same Feature dialog, with their existing icons. Revolution
selects the Extrusion feature type and presets both side operations to Revolution.
The Extrusion type's automatic names also use Extrusion; user-authored names remain
unchanged. The separate cylindrical-face-axis creation action is retired.

The compact Feature type selector is followed by independent Point and Text
checkboxes. These persist ordinary-view origin-marker and name visibility in
the native Feature definition; new Features enable both. Hidden markers retain
their original reference identity and remain available to reference commands.
While Feature Properties is open, its transient preview always displays both
the origin and name, regardless of those ordinary-view settings. Cancel restores
the stored settings.

Feature Tree children follow the selected type. Point, Axis and Plane expose
their result without an unused internal Sketch or Sketch-plane row. Sketch
exposes its editable Sketch and plane without a duplicate result row. Extrusion
(including the Revolution shortcut) retains its profile Sketch and plane and
lists each active operation with its side. Symmetric operations use one row
for both sides. The Feature icon follows its active operations: extrusion,
revolution, or both icons side by side. Operation rows open the shared Feature
properties. A new pending Feature starts collapsed in the tree.
Document Origin icons are white, Body Origins red, and feature Origins green.

Point and Text visibility is also available for placed extrusions, revolutions,
2D/3D/helical sweeps, Hole/Thread, imported bodies, Holes, Flat, Bend, Twisted Sheet
and Sheet Transition. Other operations, including Fillet, Chamfer, Shell and
Unbend/Bend Back, do not expose these controls. New non-Feature controls default
to off to preserve the ordinary display. Their flags persist with the history
container; changing only these flags reuses the calculated body and remains
undoable. The complete Origin is visible during Properties for every Feature type.

Editing an owned profile Sketch from the tree opens its owning operation's
transaction and then its embedded Sketcher. Finish returns to the owning
Properties; only its OK commits the edit. Cancel restores the original profile.

The first face reference picked in the View seeds placement at the ray's hit
point on that confirmed face. Analytical support geometry determines the final
point and normal. Numeric coordinate locks and the reference's distance-capture
lock retain their entry behavior. Later references and Tree reference entry do
not reseed the position. The hit is transient; no tessellation triangle identity
is stored in the native reference.
Picked coordinates retain full precision independently of the numeric fields'
display rounding. Editing a coordinate replaces that coordinate normally.

Solid-face hover fills only the exact offered face with translucent green;
confirmation and independent reference inspection use translucent azure.
The viewer reuses the calculated face triangles with depth testing and caches
the GPU upload by face/occurrence identity until either selection or mesh changes.
This does not calculate geometry, recolor the entire body, or fill datum planes.
The internal Sketch data is retained when changing types; hiding a Tree row does
not delete geometry or change reference identity. Pending and committed rows use
the same builder in Part and Assembly trees.

Two rotational sides share a maximum angular span of 360 degrees. For authored
numeric angles, the edited side takes priority and the opposite rotation is
shortened to the remainder. A full turn disables the opposite rotation; an
opposite Extrusion remains independent. Symmetric numeric rotation is limited
to 180 degrees per side. Selecting Full while symmetric returns to one full
rotational side with symmetry disabled. Inactive settings and source-side
identities are retained.

Reference-limited rotations retain their oriented target definitions. Both the
wire preview and calculation reject a resolved combined span over 360 degrees;
they never silently stop before the chosen target. Native/command validation
also rejects overlapping explicit angular definitions.

## Verification

Core regression coverage checks cylinder and cone projection, changing normals,
signed offsets and sides, axis interpretation, reference serialization, intersecting
constraints, and failed solves retaining the last valid frame. The GUI contract
checks all six shortcuts, cylinder-axis creation, full-face extent initialization,
OK, Cancel, Undo/Redo, save/reopen, and new Part/Assembly template activation.
The GUI contract also confirms an actual cylinder candidate with a pointer
click and verifies the resulting position against its analytical support.

The current placement, feature type/prototype, common UI, translation and
new-document options contracts passed. The broader dialog layout audit found
ten Linear Pattern table-resizing failures out of 300 combinations; these are
recorded separately in [the release verification](releases/2026092601.md).
A prior family-table UI run stopped at its family-row/source-order assertion;
that separate issue has not been diagnosed by this change.
