# Sketch face/plane intersection calculation foundation

Date: 2026-10-21. Platform: native Windows, pinned OCCT 8.0.0.
Scope: calculation foundation for the user-approved
[Sketch intersection reference scope](../SKETCH_INTERSECTION_REFERENCE_PLAN.md).
This is not acceptance of the complete new Sketch command.

## Inputs, means and output

The input is a previously calculated native Body boundary, an existing original
face identity and the actual resolved Sketch plane in the same coordinate frame.
The output is a bounded exact curve/point packet with native parent identities.
The implementation reuses `make_surface_intersection_data`, including its
non-destructive section, periodic-seam joining and ancestry validation.

Ordinary solid publication can retain its exact topology only in the current
kernel cache. Body publication changes its fingerprint for placement; its
local history boundary retains the calculation key. The service reads that
relationship and the existing rigid placement. A cold source with complete
native bindings reads its BRep archive addresses instead. Neither path invents
an identity from a mesh, screen position, traversal index or a nearest curve.
Missing or ambiguous ancestry remains an error, without history replay.

## Finite native matrix

| Cases | Checks | Result |
| --- | --- | --- |
| 12 single branches | Rectangular side and cylindrical side; three plane heights and both normal directions; independent plane, line and radius equations; exact curves; stable identities and read-only source | Passed |
| 2 empty sections | Plane outside each bounded source | Passed |
| 12 placed branches | The same sources after translation `(12,-7,20)` and 90-degree X rotation; three heights and both normals; independent world-space equations and unchanged branch identity | Passed |
| 12 disconnected sections | Concave planar face; three plane positions and both normals; warm/cold kernels with a native packet round-trip; two distinct branches with native endpoint ancestry, exact domain and combined length 40 | Passed |
| 1 branch merge | Two branches become one; the merged curve does not inherit either previous branch identity | Passed |
| 1 coincident area | Coplanar face area is rejected rather than represented as a unique curve | Passed |
| 6 spherical sections | Three plane positions, both normals; exact sphere/plane equations; no parameter-seam point; stable closed-curve identity | Passed |
| 2 tangent contacts | Both normals; one isolated point `(20,0,0)`, no curve | Passed |
| 2 disjoint sphere sections | Both normals; no point or curve | Passed |
| 1 ambiguous toroidal section | Two closed curves share identical face ancestry; the existing precise ambiguity error is required, with no arbitrary branch numbering | Passed as rejection |

The final 51-case executable passes in 0.73 seconds on this host. Evidence:
`build/sketch-face-plane-foundation-51-tests.log`. This timing was observed during
other compilation and is not a performance guarantee. The existing native
surface suite and face-measurement demand suite also pass (three-suite run:
26.78 seconds), recorded in `build/sketch-face-plane-foundation-tests.log`.
The shared archive reader's ordinary face-only path remains covered by the
measurement suite, including native save/reopen and read-only measurements.

## Remaining integration and verification

The production Sketch UI does not call the new service yet. No new reference
mode, selected-branch field, start template or factory asset is written by this
foundation. The original user document remains outside the test writes.

Before command acceptance, implement the approved persisted branch definition,
resolve multiple closed branches where their native support domain supplies an
unambiguous role, and keep unresolved cases explicit. Verify selection, cached
inspection without OCCT, source edits, disappearance/splitting/merging, native
Part/Assembly save/reopen, Undo/Redo, Cancel and the full applicable Sketch
dimension/drag matrix. Assembly occurrences, nested/repeated sources, holes,
arbitrary NURBS branches and cold solid packets without complete native edge
ancestry are not certified by this matrix. The packet round-trip above does
not substitute for the future real command's native file/GUI tests.

Localization review: the new service adds no user-visible text and reuses the
existing intersection diagnostics. The associated external-reference guidance
change passes the five-language production-source coverage and UI translation
suite (`build/external-reference-localization-tests.log`, 24.12 seconds).
No shared placement implementation or side/offset contract is modified.
Linux execution remains unverified.
