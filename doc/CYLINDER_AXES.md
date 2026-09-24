# Cylindrical face axis

The explicit **Cylindrical face axis** command creates a construction Axis
from one exact cylindrical source face. It is useful for imported STEP/IGES
geometry and also accepts native cylindrical faces. Existing automatically
generated axes are unchanged.

Creation and editing use **Cylindrical face axis properties**. The source field
uses the shared green reference arrow, active-input outline, remove button and
independent inspection eye. Removing a reference reactivates selection. The name
and reference rows remain at the top when resizing the window. No Origin button
is offered: this command derives its axis from the selected cylindrical face.
Existing user-authored object names and stored references are preserved.

In the ordinary display, a face-derived Axis shows its axis line without an
additional origin-point marker. The original point reference remains available
for reference selection; editing and explicit highlighting retain their existing
feedback. Other construction Axis and Point containers are unchanged.

Drawing axis annotations also omit the extra filled center marker. The shared
sheet layout retains the axis line and the four-arm cross for an end-on view,
so existing drawings receive the display correction without regeneration.
Canvas and printed/PDF sheet rendering use this same layout. Reference identity,
axis selection and dimensioning are unchanged.

The drawing details GUI contract checks rendered canvas and print pixels for
the absence of the filled marker, retains a visible axis stroke, and verifies
the end-on cross. Drawing UI and localization contracts also pass. Verification
images are `build/drawing-axis-canvas-no-dot.png` and
`build/drawing-axis-print-no-dot.png`; logs are
`build/drawing-axis-no-dot-tests.log` and `build/drawing-axis-no-dot-retest.log`.

Creation and editing use the same internal Properties window. Click the source
field to select or replace a face; the eye independently inspects that face.
A short middle click ends reference entry. OK, including a middle-button double
click over the View, commits the construction. Cancel discards the preview.

The construction stores `definition: cylinder_axis` and the original face's
owner, semantic key and occurrence path in its existing reference list. The
resolver reads exact cylinder data already stored in the native document. It
adapts that cylinder to the existing Axis resolver without changing general
container placement. It never fits a cylinder to triangles or invokes OCCT in
the selection, preview or Properties paths. The displayed axis is centred on
the source face's axial extent. Missing/non-cylindrical references invalidate
the datum while retaining its last resolved position.

Source placement updates resolve dependent axes during explicit calculation.
Repeated Assembly occurrences remain distinct. The generated construction
uses ordinary Axis references for subsequent modeling and mating.

Imported feature Properties also shows a transient wire built from persisted
original edges. History rollback retains the input geometry while this wire
tracks pending placement; Cancel restores the unchanged document.

## Assembly placement using cylinders

Component placement accepts a cylindrical source face on either side of an
Axis coincidence row, including a cylinder paired with an existing axis.
The persisted reference kind is `cylinder_face`; its owner, face key and exact
occurrence path continue to identify the original face. Cylinder radius is
irrelevant to coaxial placement, so shaft and bore diameters may differ.

The analytical cylinder supplies a point and direction to the existing axis
equations. Flip, zero-distance semantics, mobility, dependency ownership and
explicit regeneration remain unchanged. Coaxial placement alone leaves axial
translation and rotation free; additional existing references can constrain
them. Only rows containing cylinder references request transformed analytical
surface frames. Existing axis, point and plane rows retain their previous path.
No auxiliary construction or OCCT calculation is needed to select a cylinder.

## Verification

The construction command contract compares cylindrical-face placement with
equivalent explicit axes, checks both Flip directions, two remaining degrees
of freedom, mixed cylinder/axis references, missing references, native file
round-trips and command Undo/Redo. The existing Assembly placement matrix
continues to cover axis, plane, point, side and signed-offset behavior.

The cylinder-axis GUI contract exercises common View picking, transient Axis
preview, Cancel, middle-button confirmation, reopening and source movement.
It also places an inserted subassembly by its internal cylindrical face,
checks that its internal component placement is unchanged, and verifies
Cancel, OK, Undo/Redo and save/reopen. Imported Properties is checked for a
visible wire that follows pending numeric placement without committing it.

STEP/IGES property tests export and reimport translated/rotated imported
geometry, including a translated owning Body, and compare bounding boxes.
These controlled cases preserve export placement; they did not reproduce
the reported STEP coordinate-loss problem.

The broad console GUI contract now passes imported-feature preview, the updated
bound `LENGTH` family fixture, component insertion Cancel/OK and title-block
source editing. It subsequently stops at `Sweep Properties did not consume the
CLI reference offset`; that later Sweep issue remains unresolved. Family-table
implementation was not changed. The focused cylinder, component placement,
import and drawing contracts pass independently. See the
[checkpoint](DEVELOPMENT_2026_09_23.md) for final verification results.
