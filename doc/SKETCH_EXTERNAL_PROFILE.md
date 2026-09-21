# External line profiles and endpoint contacts

## External point contacts

The C command accepts a native Segment and an external point in either order,
including the cross projected from an axis perpendicular to the Sketch plane.
It binds a native construction point to the external reference and constrains
that point onto the Segment's supporting line. The Segment remains unsplit;
the contact may lie on its extension. Native endpoints can also bind directly
to an external point. Both the snapping hint and persisted point binding use C.
Native point merging and generated curve-keypoint K markers remain distinct.

## Reference profiles

Reference Profile (`Reference obrys`) projects a source edge into the Sketch.
A straight projected edge now creates an ordinary native Segment and two external
endpoint references. Each native endpoint has two independent constraints:

- coincidence with the corresponding source endpoint;
- incidence on the projected source edge.

The View displays this pair as **CC**. The endpoint reference retains the source
curve's persisted document, owner, semantic key and occurrence/context paths,
with an explicit `edge_start` or `edge_end` role. Endpoints come from persisted
source-curve geometry, not from OCCT enumeration or a fresh kernel traversal.
They are offered as normal external points by the shared picker.

Trimming rebuilds native survivor segments using the normal Sketch trimming
path. A moved end loses its original endpoint coincidence but retains **C** to
the source edge. An untouched end retains **CC**. Removing an external edge
also removes its derived endpoint references and their constraints; the native
profile geometry remains. Explicit source refresh updates endpoints and solves
the surviving constraints. Native save/reopen retains both roles and equations.

Coincidence on an external source is a persistent dependency even if the initial
coordinates make it numerically redundant. This is particularly important for a
rectangle drawn from the origin with one side on a Sketch axis: an offered **C**
to an external point must not be silently discarded. **M** continues to constrain
the side midpoint. Endpoint snapping still has precedence over side inference.

Curved external profiles retain their existing exact spline/analytic profile
representation; this change concerns straight line profiles. Offset curves show
an informational **O** marker; the curve itself remains the selection/edit target.
See [Sketcher offset](SKETCH_OFFSET.md).

The native external-reference record uses additional endpoint kind values; no
sidecar or separate geometry file is introduced. Empty Part and Assembly start
templates contain no projected references and remain valid with this contract.

## Verification

`zima_cpp_sketcher_contract_tests` covers CC display, native trimming, retained
source-edge support, removal of the moved endpoint attachment, persistence,
source refresh after trimming, source deletion, axis-aligned rectangle C and
visible offset provenance. Existing circle/ellipse/spline reference contracts
remain applicable.

The GUI check can be run with `ZIMA_VERIFY_RECTANGLE_EXTERNAL_CONTACT_ONLY=1`
and `zima-cad-cpp --verify-startup`. It draws rectangles with the mouse from the
origin, checks C and M on both axes, and verifies their saved constraints.
