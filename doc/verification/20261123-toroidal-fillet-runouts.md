# ZIMA Fillet through preceding rounds

## Reported geometry and scope

The reported route in a private Part crosses a cylindrical boss, a planar rib
and two toroidal faces made by an earlier Fillet. One toroidal intersection is
split at the cylinder's seam. At each outer end the two support contacts meet
tangentially and the new blend shrinks to a point.

The user requested a native ZIMA construction that preserves existing Fillet
behavior. Investigation used verified copies only. No original private model,
factory asset, shared placement solver or native document encoding was changed.
The first input copy has SHA-256
`75ac9988266bdac248f9a65125f1b59e1b40f6cbd5fa72e3b27f7a1b8e1390a2`.
A second, newer copy also contains an additional Extrusion and Fillet.

## Construction

The existing support-driven builder now handles toroidal supports. For parallel
torus/cylinder axes, an analytic circle intersection defines the rolling center
curve, including its limiting end tangent. A bounded high-order spline fit is
checked against that analytic curve. Adjacent fragments sharing the same two
supports are joined without treating a cylinder seam as a new physical end.

At these tangent ends, an artificial cutter wedge can self-intersect even though
the required rounded boundary is regular. The new path privately copies the
affected solid, trims its support patches and joins the exact rolling envelopes
to the retained boundary. It composes copying, trimming and sewing ancestry for
vertices, edges and faces. It checks closure, manifold incidence, exact BRep
validity, curve/surface consistency and self-intersection. A result that fails
these checks is rejected through a localized error.

Contact pcurves are refined against their fixed 3D curves; the algorithm does
not move the 3D contacts to satisfy a loose tolerance. The new precision and
boundary-replacement paths are restricted to routes involving toroidal supports.
Existing ordinary routes retain their established material operations.

The lightweight preview uses the same analytic support equations from the
viewer packet. Torus descriptors remain contour descriptors, not placement
surfaces. Native reopening reconnects these already persisted descriptors to
the existing edge side identities without decoding BRep or calculating a Body.
There are no additional persisted fields or required caches.
The preview trims intermediate cylinder stations to the new support junctions;
this prevents a short rail overshoot as the radius grows and the junction moves.

## Verified behavior

| Check | Coverage and evidence |
| --- | --- |
| Synthetic native fixture | A rib with two preceding R5 rounds and an R30 boss; R1/R3/R5/R8, both route orientations. Exact valid single solids, no self-intersection, closed manifold boundaries, contact tangency, ancestry, unchanged serialized input, BRep reopen and a subsequent drilled hole. |
| Private reported model | Fresh calculation succeeds. R3 increases volume from 426882.632856344 to 426990.123211840 mm³. Eleven contact boundaries were checked; maximum sampled normal-angle difference was about 1.21e-6 degrees. |
| Private native roundtrip | Authored route, endpoint choices and volume survive native save/reopen. R3 preview samples differ from the exact result boundary by at most 1.93e-10 mm before and after reopening, with zero Body calculations for preview. |
| Private preview radius matrix | R1/R3/R4/R5/R6/R8, four strokes each; maximum sampled boundary deviation below 2e-10 mm. Preparation measured 0.43–0.77 ms in this run, with zero Body calculations. These are local observations, not cross-machine performance guarantees. |
| Newer private model | Fresh complete history calculation succeeds, including the following Extrusion and Fillet. |
| GUI lifecycle | Four unchanged Cancel/OK cycles preserve the complete shaded rollback scene, document revision and calculation count. R2.25/R3.75/R3 changes, Undo/Redo and native save/close/reopen preserve authored route/end identities and valid geometry. |
| GUI presentation | Published four-stroke preview matches the shared generator exactly. Actual single-click selects the rounded feature in the Tree without dimensions or face fill; double-click shows its radius. Properties Cancel preserves the document. Standard-view screenshots were inspected. |
| Existing native Fillet suite | Convex/concave routes, branch nodes, mixed CUT/FUSE, pockets, circular and spline paths, curved supports, ancestry and rejection checks passed. |
| Existing command checks | Core command and node checks, locks, volumes, ownership, rollback, Undo/Redo and native files passed through the existing isolated driver. The support-packet check additionally covers both toroidal contact edges and verifies they remain contour-only after native reopening. |
| Existing wire suite | Thin extrusion/revolution, curved support previews, independent node wires, oblique and remote terminals, tangent caps, cylindrical terminals and Cut/NoCut variants passed. |
| Localization | The new stable rejection message is translated in cs/en/de/fr/ru; catalog and language-switch contract checks passed. |

Private models, screenshots and detailed logs remain under the ignored
`build/fillet-curved-junction-20261123/` directory. They are not release assets.

The unchanged full command test also contains the previously established
solid-to-sheet conversion failure, documented in
[the preceding verification report](20261123-cylindrical-fillet-ends.md).
The core driver runs the unchanged remaining functions; it does not disable or
weaken that repository assertion.

## Limits

The new route construction supports natural tangent runouts. It does not yet
construct a toroidal route ending at an explicitly selected remote terminal
face. The finite tests above do not establish arbitrary freeform support blends,
all intersecting multi-route combinations or nested Assembly GUI behavior.

Windows used the shared C++ implementation, Qt 6.11 and OCCT 8.0.1. Native Linux
execution was not performed. This work is separate from the immutable public
`ZIMA-CAD-2026112303` release.
