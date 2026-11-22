# FEM pressure calculation

This extends the shared native linear statics solver after Windows 2026112201.
It uses the existing condition definitions and generated mesh regions. There
are no native format, template, library, placement-solver or Sketcher changes.

## Calculation contract

Pressure is a uniform scalar in MPa (N/mm²). Each loaded triangle contributes
the consistent nodal force integral `integral(N_i * traction * dA)`. The load is
not normalized to a total force. The existing total Force still uses its
authored value in N and retains its area-normalized distribution.

Body-face and projected-Sketch pressure act against the outward material normal;
Reverse changes the sign. Mesh face/tetrahedron incidence determines the material
side independently of Gmsh's surface winding. The incidence map contains only
requested pressure faces and is built once per solve. An ambiguous material side
is rejected rather than guessed.

A planar Sketch uses its resolved, placed plane normal, matching its pressure
arrows. Reverse changes that sign as well. A planar patch inside the solid
applies its equivalent load once to its shared mesh nodes; it does not duplicate
the load onto the two adjacent tetrahedra. It does not create a crack or contact.

Linear triangles use exact constant-traction integration. Quadratic triangles
reuse the seven-point degree-five surface quadrature already used for Force,
retaining the oriented Jacobian vector instead of its length. This follows the
local normal on curved elements and preserves consistent midside-node loads.
The solver remains small-displacement isotropic linear elasticity: pressure is
integrated on the undeformed mesh, without nonlinear follower-load stiffness.

## Verification

Passed on Windows in the Release build with OCCT 8.0.1 and Gmsh 4.15.2:
the new pressure test, the three existing FEM native suites, the complete FEM
GUI suite, and catalog/UI translation validation in all five languages. The
installed local executable also passed the startup/template GUI smoke check.
Its SHA-256 is
`7b6ff860a8be9845b4a2d3620705b8be9f053d9d9bcce9a8547810b4ff9a0a4d`.
This is a local development binary, not a replacement for the immutable
2026112201 release archive.

`zima_cpp_fem_pressure_tests` is registered with CTest when the native Gmsh
mesher is enabled. Windows Release tests cover both element orders:

- A single tetrahedron with both surface windings and both pressure directions;
  exact resultant, moment and individual nodal loads. A curved quadratic face
  `z = 0.1*x*y` is checked against analytical polynomial area/moment integrals.
- All six faces of a 20 × 10 × 5 mm solid, both directions, with independently
  known face areas and normals.
- Asymmetric bounded planar Sketches in XY, XZ and YZ, on both opposite boundary
  planes and inside the solid, in both directions. Resultants and moments use
  the authored patch dimensions and centroid.
- Bounded Sketches projected onto both opposite caps, with off-center loading.
- An annular planar Sketch, verifying total load and zero loading in its hole.
  Curved-boundary area tolerances are 2.5% for linear and 0.05% for quadratic
  meshes; these account for finite mesh boundary representation.
- Repeated pressures 2.5, 0, 5 and 1.25 MPa, both directions, on a clamped block
  with E = 210000 MPa and nu = 0. Every node matches the exact axial solution
  within 1e-10 mm. Reaction tolerance is 1e-4 N, consistent with the iterative
  solver's relative residual; simultaneous Force and Pressure balance correctly.
- A cylindrical Body face with radial normals and analytical integrated radial
  moment; a bounded projected patch away from the periodic seam, checked against
  analytical resultant and moment. The radial-moment tolerances are 2% for
  linear and 0.02% for quadratic geometry.
- Explicit rejection of negative pressure, unresolved placement, wrong region
  kind, missing mesh region and ambiguous surface incidence.
- No CAD Body calculations during pressure meshing or solving.

The Windows GUI regression additionally exercises Body-face, planar-Sketch and
projected-Sketch pressure through the real Calculation command, result display,
Cancel, magnitude/reversal edits, mesh reuse, Undo/Redo and native save/reopen.
The existing transverse-force cantilever remains the regression for total Force.

Localization review replaces the obsolete unsupported-pressure message with
the surface-normal diagnostic in Czech, English, German, French and Russian.
No other user-visible text changes.

This is finite coverage, not a convergence study or a certification of arbitrary
meshes. Contact, nonlinear deformation and spatially varying pressure are
outside this change. Subsequent stress recovery and color maps are covered in
the [result verification](20261122-fem-results.md). Linux execution remains unverified; the C++
implementation and test definitions are shared.
