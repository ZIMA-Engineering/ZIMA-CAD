# General surface placement

The user approved extending shared container placement on 2026-09-29. For
general surfaces, interactive speed takes priority over exact surface evaluation.
This is an explicit, scoped exception to the general accuracy-preservation rule.
Planes, cylinders and cones retain their existing analytic behavior.

General placement consumes the existing persisted original-face triangles. It
does not call OCCT, use final result-body topology, create a new tessellation or
change the native file format. A reference retains the original owner, semantic
key and occurrence path. Its saved resolved position seeds subsequent resolution;
no triangle index becomes persistent identity.

The first View click seeds contact inside the picked face. The next two empty
position rows capture their measured distances once, then release their locks,
using the same interaction as cylinder placement. Subsequent plane references,
straight axes/edges and points constrain the finite patches. Incompatible
constraints fail without replacing the last valid frame. Properties disable OK
for an invalid preview. Other combinations of nonlinear positional references
are not approximated by infinite tangent planes; they remain unresolved.

Each triangle is intersected with the additional linear constraints, and the
closest feasible point to the saved position is selected. Isolated alternatives
use the existing solution picker; a continuous solution retains its free motion.
Alternative selection persists through the resolved position rather than an
index into the mesh. After a source change the nearest feasible solution is used;
if none exists the placement is invalid. Changing tessellation order does not
change face identity.

Normals come from oriented source triangles. FRONT/TOP, Flip and signed offsets
retain their existing meanings. A nonzero offset translates each finite patch
along its normal; this is a piecewise planar approximation and is not an exact
offset surface. Position and normal accuracy depend on the source mesh, and the
normal can change at a triangle boundary. The constraint residual tolerance is
1e-7 document units; it is not a claimed distance bound to the exact CAD surface.

Verification covers finite boundaries, impossible equations, multiple roots,
orientation and offset sides including signed zero, reference packet round trips,
triangle reordering, and first-reference distance capture. The surface-placement
GUI contract additionally exercises actual View picking, invalid-OK gating,
creation, save/reopen, Undo/Redo and Cancel on a curved Boundary Surface. Existing
curve, cylinder, cone, placement-command and boundary-surface tests remain in the
regression set. No user-visible strings were added; existing localized reference,
status and solution controls are reused.

## Invalid-reference confirmation follow-up

The saved `POKUS.prtz` case exposed a pre-existing confirmation gap outside
general surfaces: a point referenced an extrusion's end circle, then requested
a plane offset of -133 although the circle required -150. The solver correctly
reported an invalid placement and retained the last frame, but Properties could
still commit the contradictory definition. The shared placement panel now blocks
OK for any failed placement, including curves and analytic faces. Profile/Feature
transactions also reject an unresolved edited placement before calculation or
history commit. Unrelated downstream diagnostic states are not rejected by this
edited-object check. The original user document is not rewritten automatically.

Placement-reference face inspection now uses the same face fill, boundary and
silhouette rendering as ordinary face inspection. It preserves the prior
visible-fragment preference, original-reference fallback and exact occurrence
identity. Edge, point and datum-plane feedback remains on its existing path.
The regression checks compare face-interior pixels, adjacent-face isolation and
clearing inspection, and reproduce the circular-edge conflict by editing the
actual numeric field in Feature Properties.
