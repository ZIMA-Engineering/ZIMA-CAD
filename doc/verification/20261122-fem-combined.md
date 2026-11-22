# Combined FEM and distance measurement verification — 2026-11-22

This record covers the approved `thermal-mechanical-static` analysis kind and
persisted `measurement-distance` object. See the
[workflow and numerical treatment](../FEM_COMBINED_AND_DISTANCE.md). The shared
C++ implementation retains Qt 6.11.0 and OCCT 8.0.1. The protected shared
container-placement solver is unchanged. Linux execution remains unverified.

## Native equations and document transactions

The thermal target passes 432 combined-load configurations: T4, straight T10 and
curved T10; both coordinate windings; heating, cooling and zero heat power;
positive, negative and zero force; both pressure directions and zero pressure;
and free expansion with rigid-motion restraints or fully restrained expansion.
Displacements, reactions and all six stress tensor components independently
satisfy thermal/force/pressure superposition. Von Mises scalars are not added.
Curved-element stress comparisons use the same output quadrature for each field.
Mechanical loads leave the independently repeated temperature field unchanged.
Combined analysis without a thermal field is rejected.

Combining two projected patches exposed a mesher defect: applying the first
partition changed the second condition's single referenced face into multiple
targets, invalidating its retained-region seed. Each patch is now resolved on
the unchanged source and all regions pass together through the established
fragment/mapping path. The independent groups keep their exact areas and moments.
The generated-mesh matrix covers 24 combinations of coincident, overlapping and
disjoint heat/pressure patches, both orderings, both face sides and T4/T10.
Existing curved-cylinder projected pressure, radial moment and planar hole
checks also pass. CAD source geometry and persisted face/side identities do not
change; disposable mesh tessellations may change.

The existing thermal expansion matrix, heat balance, material units and thermal
quadrature checks also pass. The complete pressure, condition and operation
targets pass, including the existing 96 owned-Sketch edit/drag combinations.

Distance tests cover reversed point order, zero-length spans, large translated
coordinates, long spans with small extension/contraction, rigid translation,
finite rigid rotation and T4/T10 interpolation. The rationalized length
difference retains changes that would be lost by subtraction at large scale.

Document tests cover point edits, hidden source points, duplicate/self/missing
references, wrong-kind references, atomic rejection, unchanged OK, Undo/Redo,
native save/reopen and reference replacement. Losing either reference freezes
both last valid endpoints, including when the other point subsequently moves.
Point placement reference loss is also covered. Measurement edits create no CAD
calculation and preserve the source geometry.

## Native assets and protected copies

The inventory covers 1,210 repository-owned native assets; none contains a FEM
definition affected by these additions. The changed native helper regenerated
and reopened all six start templates, whose bytes remain identical.
The focused GUI New Part/Assembly gate passes, including its first active
editable context and ordinary command availability.

Verified backup checks cover all 172 protected files. Two Drawing copies pass
native rewrite/reopen. Seven Part copies and one Assembly copy report unsupported
old formats in both the published 2026112204 implementation and the candidate.
Their modeling/reference behavior remains unverified. No migration adapter was
introduced and original hashes remain unchanged. Private names and paths are
excluded from this record.

## GUI interaction, caching and localization

The full Windows FEM GUI suite passes for Body-face, planar-Sketch and
projected-Sketch loads. Each combined calculation is independently repeated on
the same generated mesh and reproduces the GUI vertices exactly. Separate
thermal and mechanical solutions satisfy displacement superposition within
ten nanometres on the 120 mm fixture, including transverse bending. The finite
PCG stopping criterion permits
slightly different final iterations for independent right-hand sides; solver
precision was not reduced.
The final full GUI run passed in 75.546 seconds; the New Document template gate
passed in 3.635 seconds. Maximum separate-solve differences for the three patch
modes were `4.51804e-10`, `5.51313e-10` and `3.41886e-6` mm. The complete native
pressure and thermal targets passed in 2.816 and 0.265 seconds respectively.

Actual Tree and View mouse events enter the two distance references, with
automatic advancement. Tests cover the independent azure inspection eye,
reference replacement, fixed table rows during resizing, short middle-click
ending entry, middle-button double-click confirmation, Cancel, unchanged OK,
Undo/Redo, point deletion/restoration, remeshing and native save/reopen.
Distance creation/edits reuse the mesh and solution. Animation and magnification
leave the numerical label unchanged; missing references do not show stale values.

All five translation catalogs pass validation. Combined analysis and distance
controls are checked after switching Czech, English, German, French and Russian.
The surrounding FEM suite retains its two-theme checks. Captured Czech and
French distance dialogs are readable with both reference rows and bottom actions
visible. Result captures show temperature/displacement at points and the original,
deformed and changed distance together.

## Acceptance boundary

Packaged runtime and publication results are recorded separately when completed.
Native equation checks do not establish arbitrary-model mesh
convergence. This analysis uses constant isotropic properties and one-way,
steady-state, small-strain thermoelasticity. Contact, plasticity, transient heat
transfer, remote radiation and deformation feedback are outside its scope.
