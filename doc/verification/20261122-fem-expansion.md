# Thermal expansion verification — 2026-11-22

This record covers the approved optional native `thermal.expansion` setting,
one-way thermoelastic solve and measurement-point temperature/displacement.
See the [workflow and equations](../FEM_THERMAL_EXPANSION.md). It extends the
[thermal and measurement checks](20261122-fem-thermal.md), using the same shared
C++ implementation, Qt 6.11.0 and OCCT 8.0.1. Native Linux execution remains
unverified on this Windows host.

## Equations and regression

The native thermal target passes 108 expansion cases spanning T4/T10, both
windings, affine/curved T10 geometry, two scales and a translated origin,
heating/cooling/zero temperature change, and free, axially restrained and fully
restrained expansion. Independent displacement and stress formulas verify each
case. The free cases use minimal rigid-motion restraints. Tests also check
reaction balance and exclusion of an authored mechanical force.

A linear temperature field on T10 reproduces its compatible quadratic
displacement and the interpolated measurement point. An independent cubic
virtual-work integral detects insufficient thermal-load quadrature. Material
unit checks cover Celsius, Kelvin and Fahrenheit. Invalid material values,
wrong-sized/nonfinite/sub-absolute-zero temperature fields and missing mechanical
restraints are rejected. Enabled and disabled native settings round-trip.

Final native thermal, pressure, condition and operation targets passed in
0.097, 2.986, 1.640 and 0.106 seconds respectively. The existing condition target
also covers its 96 owned-Sketch dimension/edit/drag variants. The saved mechanical
regression mesh has 33,920 nodes and 16,709 elements: all displacements, reactions
and stresses remain exactly equal to the preceding implementation. The two
comparison solves completed in 169.846 seconds; this is equivalence evidence,
not a performance improvement claim.

## GUI and localization

The complete FEM GUI contract passed in 65.536 seconds. Each of Body-face,
planar-Sketch and projected-Sketch heating exercises enabling/disabling expansion,
Cancel, unchanged OK, Undo/Redo, mechanical restraints, paired measurement values,
all three color quantities, deformation animation and native save/reopen with
independently repeated thermal/mechanical solves. Doubling the expansion
coefficient doubles displacement without changing temperature; Undo restores the
original solution. Invalid material reports an error. A mesh-size change clears
the solution and the point works after remeshing.

Existing reference placement, whole-Origin entry, reference loss/repair,
measurement-only cached edits, pressure calculation and progress/worker cleanup
checks pass in the same suite. The shared container-placement contract is
unchanged. Five-language catalog validation passes (10.955 seconds). GUI checks
exercise Czech, English, German, French and Russian in both themes. Captured
Czech dark and German light settings are readable; the displacement screenshot
shows temperature and all displacement components together. Visual amplification
does not change those measurements.

## Native assets and protected copies

The inventory covers 1,210 repository-owned native assets, with zero existing
FEM definitions affected by the added optional setting. The changed native helper
regenerated and reopened all six start templates; their bytes remain identical.
The focused GUI New Part/Assembly template gate passes (4.002 seconds), including
the first active editable context and normal commands.

The existing verified backup was rechecked against all 172 protected files.
Two Drawing copies pass native rewrite/reopen. Seven Part copies and one Assembly
copy report an unsupported old format in both the published 2026112203 CLI and
the changed implementation. Their modeling/reference behavior therefore remains
unverified; no legacy adapter or conversion was introduced. All original hashes
remain unchanged. No private project name or path is included in this record.

The previously recorded broad template-image GUI failure is outside this change;
the focused new-document gate passes. These finite checks do not establish
arbitrary-model mesh convergence. Constant isotropic steady-state small-strain
thermoelasticity excludes contact, plasticity, transient heating and remote
radiation. Windows package acceptance is recorded in the
[release note](../releases/2026112204.md).
