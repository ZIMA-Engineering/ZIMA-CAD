# 2D Sweep

2D Sweep is a Part history container with a planar path and profile Sketches at
stations. It supports Add/Subtract, transitions between profiles (Loft), and Solid/Thin
results. Creation/editing use one internal Properties window. Pending Sketches and
parameters commit only on OK; Cancel restores original history. Editing displays
real input before the container.

The profile table follows the shared reference-table layout: a compact leading
row-number column, arrow/remove column, consistent row height and no whole-row
selection highlight.
The arrow opens the existing station Sketch action; removal retains its original
meaning of removing an explicit profile. Sketch editing, point correspondence
and inherited-profile status remain distinct operations with unchanged semantics.

## Placement and path Sketch

The container uses shared placement: position references, FRONT/TOP, X/Y/Z, rotations,
corrections, and Origin selection. The separate green field before **Path Sketch**
defaults to the container's own XY plane. Change it by picking only XY/YZ/XZ of
that same container in View/Tree, without changing container placement. Body faces,
Part planes and planes of other containers are excluded. Two mandatory perpendicular
path/profile planes are not required; profile planes derive from path tangents.

This internal Sketch reference follows parent placement; changing it changes no container position,
rotation, or references. Existing saved path references are not silently rewritten
when opening a document; new selection follows the own-plane restriction.

The path is one open, continuous planar curve starting at Sketch Origin, with any
initial direction. It accepts segments, arcs, elliptical arcs, and open splines,
including evaluated Sketch fillets. It may change planar direction, not necessarily
follow one axis. Sweep ends at the actual path endpoint.

An invalid path leaves the profile table empty and displays a localized explanation.
Preview status must not overwrite this error. Correcting the path repopulates the
station table through the same existing route calculation.

## Profile Sketches and Loft

Curve ends and actual Sketcher points on the path offer profile stations. Arc centers
and noninterpolating-spline control points are not stations. Profiles are normal to
local tangents; sharp corners have separate incoming/outgoing stations. Profile Sketches
have stable identities and tree access; entering one aligns camera with its solved plane.

The first station requires an owned profile. Later empty stations inherit the last
populated profile along the path; populating another enables transitions. **Point Order**,
View markers, and **C / K** matching-point constraints use the same controls as
[3D Sweep](3D_CURVE_AND_SWEEP.md).

**Solid** accepts closed regions with holes, such as concentric circles forming a
tube. Subsequent profiles need corresponding boundaries/matching points and equal hole counts.

## Thin: thickness

**Thin** accepts open/closed contours, positive thickness, and **Inward / Outward /
Symmetric**. Symmetric divides total thickness equally around the source profile.
Open-contour orientation determines side. Closed profiles create hollow sections;
open ones create strips capped at contour ends. One Loft cannot mix open/closed
profiles. Thickness is measured in profile planes; variable Loft may not maintain
constant normal thickness at sloping result walls.

Excessive offsets, topology changes, and invalid sections are rejected with explanation.
The dialog stays open for correction; offsets never overwrite source Sketches.

## Preview, calculation, and references

Sketches/previews use persisted ZIMA data even for incomplete paths, without OCCT body
calculation. Reference eyes toggle inspection; text clicks arm green input. Short MMB
ends reference entry; MMB double-click confirms OK even over View.

Only OK or explicit **Regenerate** calculates bodies. Path plane, source points,
Sketches, and matching persist in current Part format. Old two-Sketch arrangements
are not migrated. Segments/arcs calculate exactly; general planar curves adapt to
document linear tolerance. Mesh deviation controls display; no feature-specific
precision is needed. See [Numerical precision](NUMERICAL_PRECISION.md).

Derived face/edge/point identities come from source Sketches, profile regions, and
result semantics, never OCCT traversal or path-sampling order.

## Verification

`zima_cpp_sweep2d_contract_tests` and shared 3D Sweep tests cover geometry, multiple
profiles, Thin, holes, placement, and persisted references. GUI
`ZIMA_VERIFY_SWEEP2D_ONLY=1` with `zima-cad-cpp --verify-startup` checks owned Sketches,
camera alignment, path-plane changes, Properties, OK/Cancel, save/reload.

### Calculated-solid centerline

The solid publishes a dash-dot centerline derived from source curves
(`centerline:from:<source_id>`). Segments, fillets, splines, and helices retain shape;
approximated portions of one source curve share a reference. Only straight portions
also offer axis references. Display respects Axes visibility, including shaded mode.
Geometry is persisted during solid calculation; rendering/picking invoke no OCCT.
Older calculated models gain centerlines through explicit Regenerate.

### Boolean boundary visibility (2026-09-21)

A Boolean intersection may retain paired p-curves from its source surface.
That alone does not make the resulting edge a hidden parameter seam: an edge
shared by distinct faces remains visible. The existing tangent cylindrical
split suppression remains in effect. Explicit **Regenerate** refreshes the
persisted edge flags of already calculated documents; opening or displaying
them never triggers a kernel calculation.

The Sweep contract includes a circular-profile cut along a box boundary.
For a read-only recalculation check of a specific native Part, pass its path
to `zima_cpp_sweep2d_contract_tests`; this does not save the document.

Selecting the Sweep also highlights its path, even with ordinary axes hidden.
After explicit calculation, the path endpoints are visible original-reference
points for downstream placement. See
[Sweep selection and attachment endpoints](3D_CURVE_AND_SWEEP.md#sweep-selection-and-attachment-endpoints-2026-09-24).
