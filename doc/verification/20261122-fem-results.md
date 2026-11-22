# FEM mesh validity and result presentation

This follow-up keeps the native document format unchanged. All mesh, stress and
color buffers are disposable results derived from the existing Part geometry,
conditions and material. No factory asset conversion is required.

## Mesh repair

Curving quadratic tetrahedron midside nodes onto CAD can invert a previously
valid linear element. The mesher now checks Gmsh's adaptive minimum Jacobian
determinant before publishing a quadratic mesh. An invalid mesh invokes Gmsh's
CAD-constrained HighOrder optimizer and must pass the same check afterward.
It is never downgraded to linear elements or accepted with a relaxed tolerance.

A saved twisted-profile reproduction contained 33,920 nodes and 16,709 quadratic
tetrahedra. One tetrahedron had a negative integration-point determinant
(-5.2159902583), despite a positive corner determinant. After optimization the
connectivity and element count were unchanged; independent evaluation of every
integration point found no nonpositive determinant (minimum 2.5526152983).
The existing 1000 N load solved with maximum displacement 0.167992 mm and total
reaction (2.88e-7, 7.25e-7, -1000) N. The native read/mesh/solve took 103.9 s in
one local run; the GUI run took 82.5 s. These are observations, not a benchmark.
The user's original document was not overwritten.
The final color-map GUI run completed in 64.4 s, retaining the same 0.167992 mm
maximum displacement and reporting 10.0815 MPa maximum element-averaged von Mises
stress. A preceding remesh reported 10.0665 MPa; these are approximately 10.1 MPa
and are not a stress convergence result. Both captures came from actual solves.

## Result quantities

The right FEM toolbar owns Mesh, Calculation and Results. Mesh and Calculation
are absent from the top menu bar. Results offers mutually exclusive displacement
magnitude in mm and equivalent von Mises stress in MPa, enabled after a valid
linear static solution. Each quantity has its own zero-to-maximum color scale.

Displacement colors use the mean of the displayed triangle's nodal magnitudes.
The reported maximum is the maximum over all volume-mesh nodes. Stress uses
the displacement gradients and the same isotropic elastic law as the stiffness
operator. Stress tensors are volume-averaged within each tetrahedron before
computing von Mises stress. There is no smoothing across elements. Each exterior
triangle, including every subdivision of a quadratic face, uses its owning
tetrahedron's stress. The legend maximum includes interior tetrahedra too.
The UI tooltip and maximum-stress message identify the element average.

The shader colors result triangles without lighting distortion. The legend uses
the identical palette. Changing the quantity uploads only the scalar color
buffer; it does not recalculate CAD, solve again, rebuild geometry, change the
camera or create an Undo step. Publishing another scene clears the field.
Result validity retains the existing source, condition, mesh and material checks.
Deformation is shown at its actual scale, without exaggeration.

FEM owned-Sketch plane preview now uses the resolved Sketch frame and ordinary
azure preview wire, including XY/XZ/YZ, offset and automatic reference placement.
It adds no selectable datum or document object and does not change the shared
placement solver.

## Verification scope

The native pressure test covers recovered stress for linear and quadratic
tetrahedra, curved quadratic geometry, rigid translation/infinitesimal rotation,
uniaxial tension with Poisson contraction, hydrostatic strain, pure shear,
general strain and rotated frames. Constant axial pressure cases verify every
element's equivalent stress, repeated load magnitudes/reversal, and ownership
of every displayed surface triangle. The full pressure matrix remains in the
[pressure report](20261122-fem-pressure.md).

Windows Release checks passed with OCCT 8.0.1, Gmsh 4.15.2 and Qt 6.11.0:
the pressure/stress native matrix, three existing FEM native suites, full FEM
GUI, five-language catalog/UI validation, and the drawing view-control GUI.
The GUI exercises three successive switches between both quantities, actual
framebuffer colors and both legend endpoints, identical restored displacement
values, unchanged geometry upload count/camera/revision, and clearing colors
when results are hidden. It also checks real sidebar menu opening and removal
from the top menu, pressure edits/Cancel/Undo/Redo/save-reopen, and azure plane
pixels and frame equations for the owned Sketch plane.

New result labels, units, tooltips and diagnostics are translated in all five
catalogs (cs, en, de, fr, ru). Existing menu and plane-preview strings are reused.
No new native fields or persisted side conventions were introduced.

This is a finite linear-elastic verification, not a mesh convergence study.
Element averages do not report unresolved local peaks. Thermal, contact,
plasticity, nonlinear follower loads and dynamics are not implemented by this
change. Linux execution remains a separate host verification requirement.
