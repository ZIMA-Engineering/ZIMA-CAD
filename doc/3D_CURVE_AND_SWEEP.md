# 3D Curve and 3D Sweep

A 3D Curve retains original construction points with persistent IDs. Table numbers
are path positions, not reference identities.

The point/direction table uses the shared reference-cell presentation, with a
leading arrow or remove control and independent inspection eyes beside Point
and Direction Axis. Inspection highlights only the selected point or local axis
without arming input. Clicking a Point opens its existing properties editor;
Direction Axis chooses X, Y or Z of that point directly. Whole-row selection is not
used to indicate input ownership. Point order, direction toggles, axis choice, Reverse
and radius editing retain their existing meaning. The profile table uses the
same leading controls while retaining Sketch and point-order actions; it does
not treat an inherited profile status as an editable geometry reference.
Removing a path point removes its row and its attached profiles. Removing an
explicit profile keeps the path station and restores the existing inheritance
rules. Tooltips distinguish these actions; no new deletion capability is added.

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

## Curve point table interaction

An independent checkbox before the remove/arrow column selects one point for ordering.
The bottom Up/Down buttons move that stable point by one position and disable at
list bounds. Editing and reference inspection do not change this selection.
Click Point once to open its editor; the redundant Edit button is removed.
A direction checkbox enables the X/Y/Z dropdown in Direction Axis, its inspection eye and
Reverse controls. These actions preserve the stored tangent when disabled.
Reverse is a checkbox; the separate Cycle axis column is no longer displayed. Headers and tooltips are localized in all five languages.
Radius uses document decimal precision. Numeric sizing subtracts actual native step-button and lock geometry from the available editor rectangle, including horizontally arranged Windows arrows, and respects the native style content size and editor clip.
The bottom table consumes additional window height while upper fields remain fixed
(Points in Curve Properties, profiles in 3D Sweep Properties).
Explicit axis inspection also paints the exact axis when ordinary axes are hidden;
it does not expose other Origins or change the tangent.

## Whole-Origin placement performance (2026-09-24)

Whole-Origin entry retains the existing sequential XZ/XY/YZ reference validation,
side/offset handling and placement solving. Only tree/mesh publication is deferred
until the complete click has been processed. A single reference assignment publishes
its preview after both the reference and definition mode are updated; the redundant
second scene refresh after acceptance is removed. No topology or file format changes.

On the local Windows Release build, selecting the active Body Origin in new Curve
Properties over `Projects/STEP-IMPORT.prtz` took 3482.34 / 3394.41 / 3128.44 ms
before and 991.291 / 989.113 / 911.958 ms after the change. Each measurement includes
synchronous tree selection and event processing, excluding file loading and dialog
opening. The mean fell from 3335.06 to 964.12 ms (3.46 times faster, 71.1% less time).
Scene/tree resets fell from seven to one. Timings are local observations, not a
cross-machine performance guarantee; reference solving remains synchronous.

`zima_cpp_whole_origin_ui_contract` verifies the shared path using a synthetic Part.
For the imported fixture, run the normal `--verify-startup` console verification with
`ZIMA_VERIFY_CONSOLE_ONLY=1` and `ZIMA_VERIFY_ORIGIN_PERF` set to its absolute path.
It asserts one scene reset, original reference ownership, Cancel restoration,
OK/reopen preservation of reference sides/offsets and placement, and creation Undo.
The benchmark never saves over the supplied document. This change introduces no
user-visible text; localization coverage remains part of the verification.

The New point cell uses green text on the ordinary background. Hovering any
enabled reference-entry cell gives only that cell a light-green fill and dark
text. Leaving restores its ordinary or inspected background. Disabled entries
do not react. The green input-ownership outline keeps its
existing meaning. Curve Properties initially requests 900 px height (bounded by the
main window), with the extra space assigned to the point table. No labels or
interaction rules change.

Visible 3D Curves always display their entered points without numbers or child Origin frames, matching Sketch presentation. Selecting the Curve in the View or Tree highlights those points; clearing selection restores their ordinary colour. Point references retain the child Point Origin identity, while ordinary selection chooses the owning Curve. This presentation is rebuilt from persisted construction data and requires no kernel calculation. Numbered points remain part of Properties editing. No localized UI text is introduced by this change.

The white editing stroke is restricted to the edited Curve owner and occurrence path. Other Curves retain their normal appearance while any feature or Sketch dialog is open. UI regression checks cover ordinary Curve picking and editing a different Curve or occurrence.

The STEP-IMPORT GUI regression also checks 85 visible Curve samples, real LMB confirmation, RMB cycling of overlapping Curves, and selection after cancelling Properties. Ordinary Body filtering includes owned Sketch identities: original-reference Sketches from a hidden or inactive Body must not steal hover from visible Curves. Ownership is resolved once per scene refresh, not on mouse movement. The source document is opened without saving.

Nested Point Properties binds reference labels after switching the active dialog
and preparing the child's reference geometry. Binding before that initialization
incorrectly showed stored references as blank red missing-reference fields,
although their persisted identities were intact. The fix changes only the nested
editor's initialization order; placement solving and serialization are unchanged.
To verify all stored Curve points in a supplied document, add
`ZIMA_VERIFY_CURVE_POINTS=1` to the `ZIMA_VERIFY_CURVE_SELECTION` console GUI
verification. It checks populated position/orientation labels and reference
identity across Point OK, reopening and Cancel, then cancels the parent Curve.
The supplied document is never saved. No user-visible strings were changed.

### Sweep selection and attachment endpoints (2026-09-24)

Selecting a 2D, 3D or Helical Sweep highlights its persisted centerline as well
as its profiles. The selected centerline remains visible when ordinary axes
are hidden, and highlighting is limited to the exact feature occurrence.

Explicit body calculation also publishes the start and end of the sweep path
as visible, selectable points in the original reference geometry. Their keys
combine the feature owner, endpoint role and authored source-curve identity;
they never depend on kernel enumeration or tessellation sample indices.
These points support the existing container placement contract without changing
its solver. They follow the placed path and survive native save/reopen.
Changing dimensions preserves their identity while the source curve remains.
Replacing that source curve intentionally does not redirect an old reference.

Previously calculated models require explicit **Regenerate** to acquire the new
points. Selection, display and opening a document do not perform a kernel
calculation. The native packet schema and localization strings are unchanged.
