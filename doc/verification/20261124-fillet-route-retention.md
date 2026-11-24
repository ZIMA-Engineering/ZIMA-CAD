# Failed Fillet route retention, 2026-11-24

The edit lifecycle previously pruned a selected route when the compact viewer
packet omitted one of its edges, even though that edge still existed in the
current input's exact native edge bindings. A failed Fillet could consequently
lose part of its authored route before the user repaired its parameters.

Pruning now accepts the current input's exact native edge bindings as additional
existence evidence. It still removes genuinely missing references. The workspace
passes the existing input bindings during refresh and commit; the change does
not calculate geometry to populate a selection packet or invent replacement
identities.

## Windows verification

- Focused route tests distinguish an edge missing only from the compact packet
  from an edge removed from both current representations.
- Failed-route retention, predecessor rollback and dependent Cut identity pass.
- An isolated `POKUS3.prtz` reproduction retains all three route references and
  both terminals after failure, one confirmed unresolved transaction, native
  save/reopen and Undo/Redo. Refresh adds no geometry calculations.
- The full edge-treatment GUI verification passes. Existing two-sided and
  supporting-packet regressions pass.
- Broader native suites have baseline failures also reproduced at unchanged
  `f85a299c1`: `Final-first edit lost the identity-bearing Fillet output`,
  `Flat profile conflicts with its sheet endpoint references.`, and
  `Calculated history boundary does not match its parameters`. These suites
  are not reported as passing.

No visible text, native format, geometry-side or protected shared placement
contract changes. Linux execution is unverified. Local evidence is under
`build/fillet-over-radius-20261124/`.

## Oversized corner implementation

The requested R9 treatment across the existing R4 corner now has a separate,
exact ZIMA construction for a planar line/arc/line route with perpendicular
side supports. The two main straight edges need not be perpendicular. Other
route families retain their established implementation. No OCCT Fillet result
is silently substituted for a ZIMA calculation.

Two exact cylindrical strips of the requested radius join a rational
tensor-product corner patch. Its contact equation is
`P(u,v) = apex + cos(u) * (sourceArc(v) - apex) + R * (sin(u) - 1) * normal`.
The patch is tangent to both new cylinders and the existing corner cylinder;
it is a transition surface, not a claim of constant principal curvature over
the whole corner. The shared, kernel-independent contact frame also generates
the lightweight creation/inspection preview. Preview generation performs no
Body calculation. Tool creation copies the route before sewing so that the
source BREP cannot be modified by the private tool builder.

The following finite Windows checks pass:

- 36 native cases: main-edge angles 60/90/120 degrees, R6/R9/R12 over R4,
  original/rigidly rotated bodies, and forward/reversed route order.
- Exact BREP validity, self-intersection and curve-on-surface checks; one closed
  solid; retained source ancestry; and byte-identical source BREP.
- Independent horizontal-section volume integration, actual patch contact
  positions within 1e-7 mm and normals within 1e-6 radians at 19 interior
  samples on each of its three contact edges.
- BREP save/reopen and a subsequent through-hole with independently checked
  removed volume.
- The full existing native planar-edge Fillet suite, including additive and
  subtractive nodes, varying supports, curved routes and terminal modes.
- A private copy of `POKUS3.prtz`: R9 produces volume 964560.663734472 mm3,
  six preview wires with maximum sampled boundary deviation below 3.3e-14 mm,
  native save/reopen, and valid downstream history including the next Fillet.
- Actual GUI preview publication and source-node labels; repeated Properties
  Cancel/unchanged OK with no Body calculation or transaction; radius edits
  R6.75/R11.25/R9; Undo/Redo; native save/reopen and stable route/end identities.
- The normal development executable passes rounded-face mouse selection,
  owning-feature Tree synchronization, wire-only feedback without single-click
  dimensions, double-click radius inspection and unchanged document revision.

This extends the verified route family; it does not establish all possible
surface-support configurations or replace the independently reported baseline
failures above. Native Linux execution remains unverified. Early variational
and Coons prototypes failed accuracy checks and are not product code.

The normal `zima-cad.bat` target was rebuilt and verified locally. Its SHA-256
is `ffafbf226e2501fa727659d60958446125ce7fb0edc19e048fedb3d4bef40427`.
This is an uncommitted development build, not a published release. The previous
development executable is retained under `build/fem-20261124/`.
