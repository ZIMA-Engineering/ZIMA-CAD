# Steady-state thermal FEM and measurement points — finite verification

The user approved the thermal `.prtz` extension and subsequently the independent
measurement-point container, including factory/template and protected-copy checks.
See the [workflow and numerical model](../FEM_THERMAL_AND_MEASUREMENT.md).

## Native equations and integration

`zima_cpp_fem_thermal_tests` verifies:

- A T4 solution against an independently assembled, analytically integrated
  4 × 4 stiffness/Robin system solved by Gaussian elimination.
- Uniform temperatures in T4 and T10 meshes for heating, cooling and zero power,
  both face windings, and applied/exchanged power balance.
- Two tetrahedra sharing a face: the interior face contributes no convection.
- Doubling power doubles temperature rise; changing ambient shifts temperature.
- SI and native conductivity conversions, invalid conductivity/settings and
  conditional thermal-definition serialization.
- Displacement components, total distance and scalar interpolation at successive
  positions in T4/T10 elements, reversed orientation and a curved T10 cell.
  A quadratic scalar field independently rejects accidental linear interpolation.
  Exterior points are rejected without snapping. Measurement definitions roundtrip.

`zima_cpp_fem_pressure_tests` retains the mechanical T4/T10, winding, pressure,
stress, curved-cylinder and planar/projected-region matrix after extracting the
shared integration functions. On the unchanged saved-model diagnostic mesh,
all 33,920 nodal displacement/reaction vectors and all 16,709 element stresses
are exactly equal to the preceding accepted implementation. The same 13,122
iterations are used; this comparison is equivalence evidence, not a speed claim.

`zima_cpp_fem_condition_tests` exercises 96 Force/Heat combinations of applicable
Curve/Surface regions, direct/projected Sketches, radius/diameter, driving/reference,
locked/unlocked and fixed/free states. Each performs three dimension edits and
three drags, checking exact region length/area, persisted Sketch identity,
read-only external context, unchanged OK, Undo/Redo and native reopening.
Reference loss retains the last valid measurement frame; valid replacement repairs
it. `zima_cpp_fem_operations_tests` retains source-sharing and transaction checks.

## GUI and presentation

The FEM GUI contract includes actual shared placement, whole-Origin entry,
middle-button confirmation, independent inspection and owned Sketch interaction.
The thermal/measurement extension checks separate right-toolbar commands,
repeated point moves without remeshing or solving, outside-volume reporting,
magnification-independent values, unchanged OK, Cancel, Undo/Redo and Save/Reopen.
Thermal cases cover Body faces, planar Sketch areas and projected Sketch areas,
load doubling, result invalidation, cached repeat calculations and unchanged CAD
calculation counts. Coincident mechanical and thermal regions are retained in the
document but only the selected analysis partitions the mesh.

The added controls and messages have Czech, English, German, French and Russian
catalog entries. GUI checks exercise all five languages and both themes; the
production catalog validator also passes. The final local FEM GUI run passed
all of these cases in 52.303 seconds. Visual inspection confirmed readable
temperature/measurement labels and settings in the dark and light themes.
Packaged acceptance is recorded with the release.

## Native assets and protected copies

An inventory found 1,210 repository-owned native files and no existing FEM
definition requiring conversion. The current native helper regenerated and
reopened all six start templates; their bytes remained identical. The actual
New Document dialog creates a Part and Assembly with their initial editable
contexts and normal commands enabled. No factory migration or legacy adapter
was added.

The protected-project audit preserved and hash-verified 172 files, including
10 native documents. Two Drawings pass native rewrite/reopen on isolated copies.
Seven Parts and one Assembly report an unsupported old format in both the
previous installed CLI and the changed native implementation. Consequently their
modeling/reference operations cannot be claimed as verified in this cycle.
Every original retains its pre-test hash; no conversion or overwrite occurred.

A separate broad template-image GUI suite fails at “Embedded ordinary Sketch
image is missing from Tree” in both the preceding installed binary and this
candidate. The focused new-document/start-template gate passes. The broad failure
is retained as a pre-existing verification limit, not silently counted as passing.

## Boundaries of this evidence

These are finite deterministic equation and interaction checks, not certification
or a mesh-convergence study for arbitrary parts. The thermal scope is constant
isotropic steady-state conduction, surface power and uniform ambient convection.
There is no thermal expansion, transient heating or remote radiation solver.
The existing mesher's restriction on multiple overlapping projected partitions
of one target face remains; combine power on an identical region or use distinct
Body-face/planar regions. Native Linux execution is not performed on this host.
Windows and Linux consume the same C++ sources and committed factory data.
