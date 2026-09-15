# 3D Curve and 3D Sweep

A 3D Curve retains original construction points with persistent IDs. Table numbers
are path positions, not reference identities.

For a polyline, **Round corners** enables the **R [mm]** column. When disabled,
the column shows non-editable zeroes without changing saved values. Open-path
endpoints have no rounding. Radius 0 retains a sharp corner. A positive radius
creates a tangent circular arc in the plane of the adjacent segments. Each segment
is trimmed by `R * tan(direction-change angle / 2)`. Overlapping adjacent rounds
and 180° reversals are invalid. Straight continuation creates no extra arc.

Sweep builds its profile table automatically:

- 1: path start, perpendicular to the first segment.
- N.1: preceding segment end, perpendicular to that segment; arc start when rounded.
- N.2: following segment start, perpendicular to that segment; arc end when rounded.
- Final N.2: path end, perpendicular to the final segment.

For an interpolating spline, outgoing stations use its tangent; incoming stations
are inactive. Rounding parameters are retained for switching back to a polyline.

**Sketch** creates or opens a station sketch. Its profile is stored by original
point ID and incoming/outgoing role. Reordering points or changing radius does not
change sketch identity. Inactive incoming profiles remain saved but do not enter
body calculation. Deleting a source point deletes its profiles in the same pending
transaction. The first station requires its own nonempty profile. Later empty
stations inherit the last supplied profile, including point order, through the
path end. Transition to another supplied profile occurs on the segment ending there.

With **Round corners** disabled, each segment has two independent stations. All
sketches, including N.1, are accessible. No connecting geometry is generated
between N.1 and N.2. Each segment has perpendicular caps and is calculated as a
sweep for equal profiles or a loft for different profiles. Intersecting segments
are united; there is no shared oblique cap on a corner-bisector plane. Perimeter
matching is checked within each segment, not across separate corner caps.

Rounding retains a continuous sweep through arcs. In this mode, zero radius keeps
the original shared sharp-corner section. Splines also form continuous paths.
Contours must be closed and have no holes.

Unrounded segment caps are named, for example, **Segment 1 → 2.1 — end** and
**Segment 2.2 → 3.2 — start**. Identity uses both original point IDs and the
start/end role, so renumbering preserves references. The complete original cap is
stored in the reference packet even if its visible remainder disappears after
union. For circular profiles, the packet retains the original centre, direction
and radius for Drill Point. Trimmed cap portions reference the same endpoint.

Edits remain pending until the shared dialog's OK. Cancel saves nothing. Path
preview and sketch placement use ZIMA data and analytical geometry; OCCT is used
only for explicit body calculation. Shared container placement is unchanged.

The standalone experimental 3D trajectory, its editor and serialization have been
removed. The old experimental format is not migrated.

Regression suites: `zima_cpp_curve3d_sweep_contract_tests`,
`zima_cpp_ui_contract_tests`, and general `zima_cpp_contract_tests`.

## Arcs and radius dimensions in the View

Rounding arcs are displayed as part of the complete path during editing and after
confirmation. Segments and arcs share rendering and selection. The path is also
visible on the resulting 3D Sweep.

`R…` dimensions appear only while Properties are open or normal parametric
dimensions are shown by double-clicking a container. Closing Properties or ending
dimension mode with the middle button hides them; arcs remain visible. The centre,
arc point and dimension plane come from the same analytical path and respect saved
object translation and rotation. Editing radius updates the preview. Disabled
rounding, zero radius, straight continuation and splines do not create rounding
dimensions. Display never calls OCCT.

### Empty 3D Sweep profiles

The first path point needs a populated profile sketch. Subsequent points with no
sketch or an empty sketch inherit the most recent populated profile along the
path. A newly populated sketch becomes the source for later empty points.
Inactive stations do not change the source. Inheritance is evaluated during
calculation without independent sketch copies, so source edits propagate forward.
The table shows the source in **Used profile**. An unfinished open contour is not
empty and causes a calculation error.

## Matching profile perimeters

**Point order** follows the green **Sketch** button. The first point is stored by
persistent ID in `Sweep3DProfile.correspondence_start_point_id`; subsequent points
follow cyclically around the perimeter. Adjacent profiles connect 1 → 1, 2 → 2,
etc. Different point counts are rejected. Inherited stations have no independent
order: edit their source sketch.

During Properties, the View shows all active profiles, including inherited ones.
Opening a profile sketch hides preview outlines and correspondence markers so they
do not overlap editable Sketcher geometry. The path remains as context. Finishing
the sketch restores the preview; clearing a later station's outline restores
inheritance from the preceding profile. The first correspondence point is labelled
**1 – start**, the others by sequence number. Changing the start updates only the
preview. Cancel restores it; Sweep Properties OK calculates and commits the change.

Circles without points use seam orientation transported along the path, preventing
local sketch orientations from causing spontaneous twisting or narrowing. One
point with a C constraint on the circle or K on its quadrant controls seam and
rotation. C and K points can coexist in a profile; suppressed constraints do not
count. Multiple points split a circle into exact arcs whose identities derive
from the source circle and point pairs. Circle-to-rectangle transition therefore
requires four circle points matching the four rectangle corners.

For unmarked profiles, OCCT ThruSections checks compatibility. Explicit
correspondence preserves the order specified by ZIMA data. API details:
[OCCT ThruSections](https://occt3d.com/dev/doc/refman/html/class_b_rep_offset_a_p_i___thru_sections.html).
Correspondence data was introduced in Part format 14.

## Pending container in the tree

During creation, a temporary green-text container replaces **Insert here**. It
shows the current local Origin, path, points and profiles and remains available
while a point editor or sketch is open. Part and Assembly containers use the same
presentation. Editing updates the existing item and temporarily hides the insertion
marker.

Clicking a parent container's complete Origin fills all three point-placement
references, as clicking the document Origin does. Selecting individual planes
remains available. A container's own Origin and descendants are not valid
references for positioning that container.

The tree reads pending ZIMA data without inserting it into saved history or
calculating a body. OK commits the transaction; Cancel removes its preview.
Closing Properties restores normal **Insert here** display.

### Centreline of the completed solid

The solid publishes a chain-dashed centreline based on source curves
(`centerline:from:<source_id>`). Segments, rounds, splines and helices retain their
shape; approximation pieces of one source curve share a reference. Only straight
sections also offer an axis reference for later features. Display respects the
Axes toggle, including shaded mode. Geometry is saved during solid calculation;
rendering and picking do not call OCCT. Explicit Regenerate adds the centreline
to a previously calculated model.

Completed solids show the trajectory as the standard brown chain-dashed axis,
including curved sections. The original solid-line curve does not cover the
calculated axis; editing still provides the source-path preview. The shared Axes
toggle controls result axes, as for 2D Sweep and Helical Sweep.

Start/end faces of a connected Sweep/Loft (rounded or spline path) are published
to saved reference geometry during calculation. Identity derives from the start/end
role and source path segment; the endpoint tangent defines the plane. These faces
can position later containers after reopening the document too.

Tree reference checks recognize an embedded profile's own plane
`sweep3d:profile:<id>` by profile identity and owning Sweep/Loft, instead of looking
among standalone construction planes. This removes false red warnings after
reopening; actually missing planes and broken external profile-sketch references
still report errors.

## Thin — 3D Sweep thickness

Creation and editing share one Properties dialog. Result type **Solid / Thin**
enables thickness and **Inward**, **Outward**, or **Symmetric** direction. Symmetric
places half the specified total thickness on each side of the original profile.
For an open contour, orientation defines the side. Point order can select either
endpoint to reverse open-profile correspondence. Thickness is measured in profile
planes; a varying Loft does not guarantee constant distance normal to the resulting
sloped wall.

Each closed profile creates outer and inner contours; sweeping them produces a
hollow body with open ends. An open profile creates a strip closed at its ends.
One Loft cannot mix open and closed profiles. Corresponding edge counts and
matching must agree. Excessive offsets or changes to offset-profile topology are
rejected. Source sketches are unchanged.

Offset-contour preview uses Sketcher data only. OCCT runs on OK or explicit
Regenerate. Cancel discards pending changes. `result_type`, `thickness` and
`thin_mode` are required fields of the current saved Sweep/Loft; both thickness
and side participate in the calculation fingerprint.

The exact localized Czech command labels are **2D tažení**, **3D tažení**, and
**Šroubovicové tažení** (2D Sweep, 3D Sweep, Helical Sweep). Czech tooltips explain
sweeping and profile transition (Loft), or helical sweeping. Loft remains a Sweep
property.

## 2D Sweep — planar path, multiple profiles and Thin

2D Sweep starts with a sketch containing one open path from the sketch Origin.
Its initial direction is unrestricted. The container's first planar placement
reference prepopulates a separate field before **Path sketch**. The user can
replace it with a plane or planar face of an original object from the View/tree
without changing container placement. The field uses shared green input ownership,
azure inspection and short-middle-click termination.

Curve endpoints and actual Sketcher points on the path offer profile sketches.
Arc centres and non-interpolating spline control points are not stations. Each
segment has a start and end; incoming and outgoing profiles are separate at sharp
corners. Profile planes are perpendicular to the local tangent. The first profile
must be populated; later empty stations inherit the most recent populated profile
along the path. Populating another sketch creates a Loft. Profiles have persistent
IDs and individually accessible sketches in the tree.

Perimeter correspondence, **Point order**, View markers and **C / K** constraints
share the 3D Sweep implementation. So do **Thin**, thickness and
**Inward / Outward / Symmetric**, including open contours. In a varying Loft,
thickness is measured in profile planes rather than normal to the resulting sloped
wall. Solid mode also permits closed sections with holes: two concentric circles,
for example, create a tube without Thin. Successive profiles need the same number
of corresponding holes.

The path plane, source points, sketches and correspondence are saved in the current
Part format. The old two-sketch 2D Sweep structure is not migrated. Segments and
arcs are calculated exactly; general planar curves are adaptively converted using
the calculation tolerance. Preview and Sketcher do not call OCCT; OK or explicit
Regenerate calculates. Cancel discards the complete pending feature.

Regression suites: `zima_cpp_sweep2d_contract_tests`, shared 3D/Helical and UI tests.
In-application integration check: `ZIMA_VERIFY_SWEEP2D_ONLY=1` with
`zima-cad-cpp --verify-startup`.
