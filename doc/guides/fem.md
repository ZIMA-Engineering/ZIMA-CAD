# FEM

## Before you start

Prepare a calculated solid Body in a Part and assign suitable material
properties. Choose an analysis that matches the supported physical model.

## Steps

1. Deactivate the current Body to reach the Part root and choose **Create FEM**. Select the source Body and analysis in **FEM Body properties**, then confirm **OK**.
2. Define its conditions: mechanical restraints, forces or pressure for a
   mechanical case; supported thermal conditions for heat conduction.
3. Explicitly generate the volume mesh. Review mesh diagnostics before solving.
4. Run the appropriate analysis button in the FEM panel: **Linear statics**, **Steady-state thermal analysis**, **Thermal-mechanical analysis**.
5. Select displacement, stress or temperature results as applicable. Inspect
   extrema and placed measurement points; compare results after mesh refinement.

## What to check

Mechanical analysis is small-displacement isotropic linear-elastic statics.
Thermal analysis is steady-state. Optional thermal expansion is one-way
small-strain coupling; combined mode also includes force and pressure.

Deformation magnification changes the display, not the calculated displacement.
Animation illustrates a static case, not dynamics. Contact, plasticity,
transient heating and temperature-dependent properties are outside this scope.

Edit definitions through Properties and recalculate when inputs change.
Native saving retains the supported FEM definitions and measurement references.
A successful mesh or solver run alone does not demonstrate engineering accuracy.

See [FEM capabilities and limits](../FEM_STATUS.md).
