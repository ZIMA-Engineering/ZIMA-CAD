# Closed intersection branches with native radial-domain roles

Status: native calculation extension, not completion of the two planned Sketch
commands. Windows, pinned OCCT 8.0.0. This extends the
[original 51-case foundation](20261021-sketch-face-plane-foundation.md) within the
[approved intersection scope](../SKETCH_INTERSECTION_REFERENCE_PLAN.md).

## Definition and bounded implementation

Two coaxial circular sections of one toroidal face previously shared only their
source-face parents and the `:closed` role. Both were therefore correctly rejected
as ambiguous. A ring torus defines inner and outer radial domains independently
of calculation order. When the cutting plane is perpendicular to its axis, use
those predefined domains to distinguish the two closed branches.

The shared surface-intersection implementation accepts a domain role only for
analytic circles coaxial with the analytic torus, with the exact radial equation
`abs(radius - R) = sqrt(r*r - height*height)` within the requested tolerance.
The roles are `:closed:domain:torus-inner` and `:closed:domain:torus-outer`.
Their identities contain the same two source parents. No radius, coordinate,
enumeration index, screen location or sorted branch position is encoded in an ID.
The equations locate the runtime curves for those predefined native domains;
they do not fit sampled points or select whichever curve is nearest.

The extension applies only when multiple closed branches exist. Existing unique
closed branches retain their established identity. At a tangential merge, the
remaining circle does not inherit either disappearing domain identity. Disjoint
supports return no branch. Other indistinguishable closed branches, including
the two sections from a plane containing the torus axis, remain explicitly
ambiguous; this is not arbitrary multi-loop association.

Reuse the existing non-destructive Section, seam joining, exact rational curve
capture and source lookup. There are no new serialized fields, required native
data contracts, source rewrites, template changes or factory conversions in this
calculation-only extension. Previously valid branch IDs remain unchanged. The
approved future Sketch reference mode/selected-branch persistence is still pending.

## Native verification

The executable now checks 94 finite cases, including required rejections:

| Cases | Scope |
| --- | --- |
| 50 retained foundation cases | Bounded lines/circles, both normals, moved Bodies, disconnected open branches, merge, coplanar rejection, spherical curves/contact/empty results |
| 20 closed toroidal sections | Five plane heights, both normals, warm and cold services; two stable domain IDs; exact independent plane/radius equations at 101 parameters per curve |
| 16 source changes | Successive R/r = 40/7 and 25/3, translation (12,-7,20), 90-degree rotation, two heights, both normals, warm/cold services; unchanged domain identity and independent world-space equations |
| 2 tangential merges | Heights -5/+5; one circle with a new identity |
| 1 disjoint torus | Height 6; no point or curve |
| 2 ordinary surface Intersections | Same shared implementation; reversed source-face input order retains both branch identities |
| 1 cold circular solid | Native packet round-trip with the ordinary deferred-face-measurement policy; unique circular branch preserved |
| 1 cold open solid limitation | Missing persisted boundary ancestry produces the exact required error; no hidden history replay or guessed endpoint |
| 1 unresolved closed pair | A plane containing the torus axis remains explicitly ambiguous |

Source packets remain byte-equivalent after serialization; source geometry and
definition are read-only. Multiple-domain tests compare identities through source
dimension/frame changes, not merely the initial intersection result. The first
closed-domain matrix passed 74 cases, then expanded to 92 and finally 94.

The initial dependent run passed the surface and intersection suites but could
not run face-measurement demand because its executable had not been built.
After building it, both measurement demand and the final 94-case intersection
suite pass (1.23 s together). The final combined run passes all three suites in
28.69 s, including the native surface suite in 27.45 s and the 94-case matrix in
0.91 s.
Evidence: `build/sketch-face-plane-closed-domains-native.log` and
`build/sketch-face-plane-closed-domains-final.log`, with the complete rerun in
`build/sketch-face-plane-closed-domains-complete.log`. Timings are local observations.

## Remaining production integration

Ordinary solids intentionally retain face-only archive bindings under the normal
calculation policy. A fresh kernel can intersect a saved circular solid face,
but an open section also needs its native boundary/endpoint ancestry. The new
negative test records this concrete cold-source limitation. Complete that source
contract and its performance/asset review before accepting saved-source creation
of the new commands. Do not hide source regeneration behind hover or Properties.

The Sketch editor commands, selected-branch definitions, native Part/Assembly
files, factory/start-template checks and full Sketch dimension/drag/Undo matrix
remain unfinished. Arbitrary tilted toroidal loops, NURBS multi-loop domains,
every repeated/nested Assembly occurrence and native Linux execution are not
certified here. An expected rejection is not evidence of implemented editing.

Localization review: no new product UI text; existing intersection diagnostics
are reused without changes. Five-language UI labels for the future commands
remain part of their pending implementation. No shared placement, signed-offset
or geometry-side flag changes are included.
