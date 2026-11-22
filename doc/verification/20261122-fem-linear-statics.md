# FEM linear statics verification

The Calculation menu uses the active generated volume mesh and the Part's
Young's modulus and Poisson ratio. Calculation and view switching do not edit
the document. A result is reused only while the FEM definition, material and
calculated source mesh still match. The deformation is shown at actual scale,
and the command reports the maximum displacement in millimetres.

## Windows cantilever acceptance

The native GUI fixture uses a 120 mm long solid with a 12 by 10 mm rectangular
section, Young's modulus of 210 GPa and a Poisson ratio of 0.30. An owned
Sketch projects a 11.6 by 9.6 mm fixed patch onto the root cap. A 100 N
transverse Force is distributed on the opposite cap. The default quadratic
tetrahedra are used with a maximum element size of 4 mm.

| Quantity | FEM | Analytical reference |
| --- | ---: | ---: |
| Mean transverse displacement of the loaded end | 0.190753 mm | 0.190476 mm |
| Total transverse clamp reaction | −100 N | −100 N |

The displacement differs by about 0.15% from Euler–Bernoulli beam theory.
The test also rejects the unrestrained model, saves and reopens the native FEM
definition, generates the mesh and solves through the GUI actions, and checks
the deformed and undeformed views and unchanged document revision. The
Calculation menu is checked in Czech, English, German, French and Russian.

The initial solver covers isotropic linear elasticity with Force and
Constraint. Pressure is explicitly rejected. The cantilever acceptance covers
quadratic elements and the distributed surface Force. Linear elements, point
and curve forces in a complete solve, multi-body contact, mesh convergence
studies, stress plots and Linux execution were outside this finite benchmark.
