# 2D Sweep

2D Sweep is a Part history container with a planar path and profile Sketches at
stations. It supports Add/Subtract, transitions between profiles (Loft), and Solid/Thin
results. Creation/editing use one internal Properties window. Pending Sketches and
parameters commit only on OK; Cancel restores original history. Editing displays
real input before the container.

## Placement and path Sketch

The container uses shared placement: position references, FRONT/TOP, X/Y/Z, rotations,
corrections, and Origin selection. The first plane reference prepopulates the separate
green field before **Path Sketch**. Change it by picking an original plane/planar face
in View/Tree without changing container placement. Two mandatory perpendicular
path/profile planes are not required; profile planes derive from path tangents.

Path-plane selection also accepts XY/YZ/XZ of the container's own Origin. This internal
Sketch reference follows parent placement; changing it changes no container position,
rotation, or references. Main Part planes and preceding-object planes remain available.

The path is one open, continuous planar curve starting at Sketch Origin, with any
initial direction. It accepts segments, arcs, elliptical arcs, and open splines,
including evaluated Sketch fillets. It may change planar direction, not necessarily
follow one axis. Sweep ends at the actual path endpoint.

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
