# FEM: released scope and remaining work

Reviewed on 2026-11-22. **Basic FEM is implemented and available in signed
Windows [2026112203](https://github.com/ZIMA-Engineering/ZIMA-CAD/releases/tag/ZIMA-CAD-2026112203).**
Older entries describing FEM as only a proposal are superseded by this status.
The release tag and portable package contain the complete matching C++ source;
the repository's default branch may lag product development.

## Available workflow

Create a FEM Body within a Part, select its source Body, assign the Part material
and choose the analysis in FEM Body properties. Define conditions, explicitly
generate the volume mesh and run Calculation from the right FEM toolbar.

| Area | Delivered capability |
| --- | --- |
| Mechanical analysis | Small-displacement, isotropic linear-elastic statics with restraints, forces and pressure; pressure can use Body faces or bounded planar/projected Sketch areas. |
| Thermal analysis | Steady-state conduction using the Part material's conductivity, surface heat power on Body faces or bounded planar/projected Sketch areas, ambient temperature and uniform exterior convection. |
| Meshing | Native Gmsh tetrahedral volume meshing of the actual solid, including quadratic T10 elements, validity checks and explicit mesh/solver progress. |
| Results | Displacement, von Mises stress and temperature color maps, extrema and point inspection; static deformation animation with magnification and an optional transparent original. |
| Measurement point | A separate placed FEM object with persistent position/references. Mechanical results report ΔX, ΔY, ΔZ and total displacement; thermal results report temperature. Moving the point reuses the solution and interpolates within the T4/T10 element without remeshing. |
| Document workflow | Native FEM definitions, settings, conditions and measurement points, with creation/editing, Cancel, Undo/Redo and save/reopen checks. |

## Verification and publication

- [Thermal and measurement workflow](https://github.com/ZIMA-Engineering/ZIMA-CAD/blob/ZIMA-CAD-2026112203/doc/FEM_THERMAL_AND_MEASUREMENT.md)
- [Mechanical pressure verification](https://github.com/ZIMA-Engineering/ZIMA-CAD/blob/ZIMA-CAD-2026112203/doc/verification/20261122-fem-pressure.md)
- [Displacement and stress result verification](https://github.com/ZIMA-Engineering/ZIMA-CAD/blob/ZIMA-CAD-2026112203/doc/verification/20261122-fem-results.md)
- [Deformation animation and inspection verification](https://github.com/ZIMA-Engineering/ZIMA-CAD/blob/ZIMA-CAD-2026112203/doc/verification/20261122-fem-result-inspection.md)
- [Thermal equations, measurement interpolation and GUI verification](https://github.com/ZIMA-Engineering/ZIMA-CAD/blob/ZIMA-CAD-2026112203/doc/verification/20261122-fem-thermal.md)
- [Signed Windows publication evidence](https://github.com/ZIMA-Engineering/ZIMA-CAD/blob/8f7ea2f49d1875b8ca22137d54bdcbcad5be9532/doc/verification/20261122-windows-2203-publication.json)

The published package passed native/GUI checks, loading of 1,185 factory assets,
signature/trust checks, fresh public download verification and production-updater
verification. These are finite Windows checks, not a claim that every geometry,
load combination or mesh resolution has been validated. Windows and Linux share
the implementation; native Linux execution of this delivery remains unverified.

## Current follow-up and limits

Thermal expansion is the current requested follow-up: calculate displacement
caused by the thermal result and display it at measurement points. **This is not
included in Windows 2026112203.** That release reports temperature for a thermal
analysis, not temperature-induced displacement or stress.

Contact, plasticity, actual dynamics, transient heating, remote radiation,
prescribed-temperature boundaries and temperature-dependent material properties
remain separate work. Deformation animation illustrates a static load case;
it is not a dynamic simulation. Meshing quality does not by itself establish
solution accuracy: model-specific refinement and convergence checks still matter.
