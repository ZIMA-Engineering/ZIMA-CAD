# 2D Sweep

2D Sweep is a Part history container with two owned Sketches: a cross-section
and an open planar path. It offers Add/Subtract and Solid/Thin in one internal
properties window used for both creation and editing. Sketch changes remain
pending until OK; Cancel restores the original history. Editing rolls back to
the feature's real input body.

The first plane locates the profile. It can use XY/XZ/YZ or a persisted original
planar face/datum reference. The default path plane contains the profile's local
X direction and normal. An optional second original plane reference must be
perpendicular to the profile plane and contain the common origin. There is no
additional offset plane. Both sketches keep their IDs when their frames change.

The path starts at the fixed sketch-origin point. Its initial tangent must be
normal to the profile plane; joined segments must meet tangentially. Lines,
arcs, elliptical arcs and open B-splines use Sketch geometry, including evaluated
corner radii. The path may bend and change direction within its plane; unlike a
helical height law, it need not be monotone along an axis. The sweep ends at the
actual path endpoint. The profile remains normal to the tangent with a fixed
planar binormal, including through inflections.

Solid requires one connected closed region and supports interior holes. Thin
accepts one open or closed contour, a positive thickness and One side / Other
side / Symmetric. Its solid is calculated from parallel profile contours, with
end closures for an open contour and a hollow section for a closed contour.
Offsets that remove/split source edges or produce invalid/self-intersecting
geometry are rejected with a diagnostic; the pending dialog remains editable.

The View preview is derived from ZIMA Sketch data without kernel calls. Actual
body calculation happens on OK or explicit regeneration. Plane picking uses the
common viewer candidate list and persisted original references at the current
history boundary. Reference inspection uses the shared eye/cell controls.
The Tree contains the two sketches; their wire can be selected and their Edit
command opens the owning Sweep's pending sketch editor.

The solid kernel shares the normal-transport sweep calculation with Helical
Sweep. Profile curves/points supply side, longitudinal and rim ancestry.
Start/end cap identities contain the source profile-region identity and remain
stable on placement/path edits and save/load. Thin uses explicit inside/outside
roles for closed profiles and two side/endpoint roles for open profiles.
Kernel traversal order and sampled path indices never define persistent IDs.

Verification is in `cpp/tests/sweep2d_contract_tests.cpp` and the focused
application contract `ZIMA_VERIFY_SWEEP2D_ONLY=1`. These cover analytic volume,
normal transport, Thin contours, placement, end-cap persistence, plane validity,
subtraction, owned Sketch entry/return, OK/Cancel and actual viewer plane picking.
