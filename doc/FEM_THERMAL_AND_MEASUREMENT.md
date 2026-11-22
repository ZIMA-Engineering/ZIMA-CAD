# Thermal analysis and measurement points

Select **Steady-state thermal analysis** in **FEM Body properties**. The source
Part supplies the material's thermal conductivity. Enter the ambient temperature
in degrees Celsius and the heat transfer coefficient in W/(m²·K). The coefficient
applies to the entire exterior, including heated faces; its initial value is an
editable assumption, not a material property or an estimate of a particular flow.

Use **Heat source** in the right FEM toolbar to assign total power in watts to a
Body face, a bounded planar Sketch area, or a Sketch projected onto selected Body
faces. Positive power adds heat; negative power removes it. The shared plane,
Sketch and placement controls have the same reference and confirmation behavior
as other FEM conditions. Generate the mesh, then choose **Calculation**. Results
show colored surface triangles, temperatures in °C, minimum/maximum markers and
point inspection. Mechanical loads remain authored but are not thermal loads.

**Measurement point** is a separate FEM command and Tree object. Its ordinary
placement controls accept numeric coordinates and references, including a whole
Origin. A mechanical solution reports signed ΔX, ΔY, ΔZ in the Part axes and
their Euclidean magnitude in millimetres. A pure thermal solution reports temperature.
With **Calculate thermal expansion** enabled in FEM Body properties, it reports
both temperature and displacement caused by heating. This optional coupling
uses the Part material's expansion coefficient and reference temperature and
mechanical restraints; see [Thermal expansion](FEM_THERMAL_EXPANSION.md).
Values are interpolated within the containing T4 or T10 element; an exterior
point is reported as outside the volume, never silently snapped to a node.

Adding, moving, hiding or deleting a measurement point does not invalidate the
mesh or solution. A changed point resolves its element again. Remeshing creates
a fresh location cache. Lost references retain the last valid frame and display
an unresolved-placement message. Animation and visual magnification do not
change measured values. Measurement definitions survive Save/Reopen and Undo/Redo;
mesh and result buffers remain disposable and are regenerated after reopening.

## Numerical model

The thermal solver uses constant isotropic conductivity and the existing T4/T10
volume integration. It solves for the temperature rise θ = T − T∞:

`∫Ω k ∇Ni·∇Nj θj dV + ∫∂Ω h Ni Nj θj dA = Σs ∫As Ni (Ps/As) dA`.

Convection uses a consistent boundary mass matrix. Exterior faces are determined
from tetrahedron incidence, excluding internal partition faces. Heat sources use
the integrated area of their actual bound mesh regions. Reversing a face's
winding does not change its power. The Robin formulation follows the standard
[boundary mass and load treatment](https://mfem.org/fem_bc/).

All geometry is in millimetres. SI conductivity W/(m·K) is multiplied by 10⁻³;
the native material unit mm·kg/(s³·°C) is multiplied by 10⁻⁶ to obtain W/(mm·K).
The convection coefficient is multiplied by 10⁻⁶ to obtain W/(mm²·K).
Heat capacity is unnecessary for steady state. Conductivity must be positive;
ambient temperature must be at least −273.15 °C and the transfer coefficient
must be positive. Nonfinite results and temperatures below absolute zero fail.

Matrix-free conjugate gradients use a diagonal preconditioner. Before publishing
results, the solver independently recomputes the residual and checks total
applied power against convection loss. This checks algebraic convergence and
power conservation; it is not a substitute for mesh-convergence assessment.

The color field displays the mean temperature of each rendered triangle's three
nodes. The legend and minimum/maximum markers use nodal extrema. A measurement
point uses the element's interpolation functions, including quadratic functions
and curved-coordinate inversion for T10. It can therefore differ from the
displayed triangle average.

## Persistence and command lifecycle

The approved `.prtz` extension adds `steady-state-thermal`, a conditional
`thermal` settings object, `heat-source` with `heat_W`, and `measurement-point`.
Existing static definitions retain their serialized form. No new extension,
external sidecar or legacy migration path is introduced. Source geometry and
material remain owned by the Part; the analysis never adds material.

Creation and editing share the existing internal Properties windows and placement
solver. Draft changes are transient, Cancel restores the unchanged document and
unchanged OK creates no transaction. Mesh and solve are explicit worker tasks
with phase/iteration feedback. Load magnitude and analysis settings retain a
compatible mesh while invalidating results. Measurement-only edits retain both.
The shared mesher prepares only the chosen analysis type's load and restraint
regions. Changing analysis type requires remeshing when these regions differ;
inactive conditions remain authored without partitioning the current mesh.

## Limits

This is steady-state conduction with uniform ambient convection and direct
surface power. Remote radiation, prescribed temperature boundaries, transient
heating and temperature-dependent material properties remain separate work.
Temperature-induced deformation and stress require the explicit thermal
expansion option; a pure thermal solve does not calculate displacement.
Mechanical analysis retains its existing small-displacement linear-elastic
scope. Both solvers share C++ code across platforms; this delivery is verified
on Windows, with Linux execution still outstanding.

See the [finite verification record](verification/20261122-fem-thermal.md).
