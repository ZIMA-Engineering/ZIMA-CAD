# Thermal expansion and measurement points

Implemented for Windows 2026112204. The preceding immutable 2026112203 release
does not include this feature. See the [finite verification record](verification/20261122-fem-expansion.md)
and [release acceptance](releases/2026112204.md).

In **FEM Body properties**, select **Steady-state thermal analysis** and enable
**Calculate thermal expansion**. Define heat power and ambient convection as in
the [thermal workflow](FEM_THERMAL_AND_MEASUREMENT.md), and add mechanical
restraints to prevent rigid-body motion. Generate the mesh and run Calculation.
The calculation first obtains the temperature field, then solves the heating
deformation on the same mesh. Forces and pressure belong to the separate linear
statics analysis and are excluded from this heating result.

The source Part material supplies Young's modulus, Poisson ratio, the linear
thermal expansion coefficient and its reference temperature. The reference
temperature is the stress-free temperature of the authored model; it is
independent of ambient temperature. A body may therefore expand or contract even
without an applied heat source if its ambient and reference temperatures differ.
Zero and negative finite expansion coefficients are valid. Missing or invalid
material inputs are reported instead of being replaced with guessed values.

The result selector offers **Temperature**, **Displacement** and **Von Mises
stress**. A Measurement point displays temperature together with ΔX, ΔY, ΔZ and
total displacement in millimetres, regardless of the selected color map. It
samples the existing T4/T10 volume element at its original position. Visual
magnification and static deformation animation do not change measured values.
The optional transparent original remains available.

Disabling expansion restores pure thermal analysis, which needs conductivity
and thermal boundary conditions but no elastic properties or mechanical
restraints. Turning expansion on/off changes the active restraint regions and
invalidates a mesh when those regions differ. Changing the coefficient,
reference temperature or elastic properties invalidates the coupled result,
while measurement-point edits reuse the existing solution.

## Numerical model

The one-way coupling uses the small-strain isotropic eigenstrain
`epsilon_th = alpha * (T - T_ref) * I`. The thermal load is assembled as
`integral(B^T C epsilon_th dV)`. Stress recovery uses
`sigma = C (epsilon(u) - epsilon_th)` and reactions use the same thermal load.
This follows the [linear thermoelastic weak formulation](https://bleyerj.github.io/comet-fenicsx/tours/multiphysics/thermoelasticity_weak/thermoelasticity_weak.html)
and the [constant-coefficient eigenstrain definition](https://mooseframework.inl.gov/source/materials/ComputeThermalExpansionEigenstrain.html).

The existing elastic stiffness integration is preserved. Thermal loading and
stress recovery use a centroid for T4 and positive 4×4×4 Gauss-Legendre quadrature
under a Duffy transform for T10. The higher-order rule is needed because a
quadratic temperature field times a linear shape gradient already exceeds the
degree of the ordinary four-point stiffness rule on an affine T10. The curved
element geometry uses the same Jacobian and orientation checks.

The optional native `thermal.expansion` flag defaults to false and is stored
when enabled. Coefficient and reference temperature reuse the existing native
Part material properties `THERMAL_EXPANSION_COEFFICIENT` and
`THERM_EXPANSION_REF_TEMPERATURE`; no duplicate FEM material or required sidecar
is introduced. The user approved this extension, template/library review and
verification on protected project copies on 2026-11-22. The shared placement
contract is unchanged.

## Limits

This is steady-state, one-way, small-displacement thermoelasticity with constant
isotropic properties. It does not model transient heating, temperature-dependent
materials, plasticity, contact or feedback of deformation into heat transfer.
Animation illustrates the static result; it is not a time-dependent simulation.
Von Mises stress is insensitive to hydrostatic stress, so a fully constrained,
uniformly heated isotropic body can have nonzero compressive normal stress and
zero von Mises stress. Model-specific mesh refinement remains necessary.
