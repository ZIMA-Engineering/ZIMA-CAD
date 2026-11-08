# Curved Fillet and Chamfer termination verification — 2026-11-08

## Scope

This record covers the shared ZIMA Fillet/Chamfer route-end resolver, exact
curved Body termination, preservation of explicit selected route membership,
rolling-support azure preview geometry and safe deletion of selected history
treatments. It does not change native persistence, container placement,
geometry-side identity, dependency versions or user-visible text.

The implementation remains shared C++ for Windows and Linux. Windows was built
and tested with the existing Qt 6.11.0 and OCCT 8.0.1 pins. Native Linux
compilation and execution were not available on this Windows host.

## Geometry contracts

- Automatic route ends locate terminal faces incident to the two treatment
  supports at the exact endpoint. A face incident to both supports is preferred;
  split descendants may contribute one local face per support.
- A surface-differential test distinguishes a curved Body envelope crossed by
  the route from an earlier treatment tangent to it. The test is independent of
  cylinder, cone, sphere, torus, ellipse or spline naming.
- A subtractive automatic terminal receives bounded tool overlap and relies on
  the final Body CUT for its exact envelope. It is not pre-intersected with the
  same material, which previously created coincident faces and could discard the
  terminal sliver. Explicit Up To and additive terminals retain exact half-space
  clipping.
- Chamfer uses the same terminal contract as Fillet. Its custom builder consumes
  only the persisted selected route and cannot silently expand one edge to an
  OCCT tangent contour.
- Accepted Fillet and Chamfer shapes share face/edge/vertex ancestry completion
  and exact-result validation. No second same-domain rebuild is applied to an
  already accepted treatment shape.
- The lightweight preview uses calculated support-face sections before the
  tangent-line approximation for rolling supports. It retains two rails and
  both terminal section curves.
- Before a history deletion transaction, View candidates, authored wires,
  dimensions and transient topology handles are retired. Undo and subsequent
  selection use newly published topology.

## Focused Windows verification

The Release configuration built the application and the affected native/GUI
targets successfully. The following CTest matrix passed; two document tests
were repeated with `TEMP` and `TMP` inside the writable workspace after their
first run was blocked only by the execution sandbox's system-Temp policy.

- `zima_cpp_occt_planar_edge_fillet_tests`
- `zima_cpp_occt_planar_edge_chamfer_tests`
- `zima_cpp_occt_edge_treatment_support_tests`
- `zima_cpp_direct_batch_edge_treatment_tests`
- `zima_cpp_solid_state_fillet_document_tests`
- `zima_cpp_edge_treatment_command_tests`
- `zima_cpp_edge_treatment_query_tests`
- `zima_cpp_thin_feature_wire_tests`
- `zima_cpp_sketch_profile_wire_tests`
- `zima_cpp_history_deletion_tests`
- `zima_cpp_edge_treatment_ui_contract`
- `zima_cpp_symmetric_edge_treatment_ui_contract`
- `zima_cpp_treatment_selection_ui_contract`
- `zima_cpp_edge_treatment_fragments_ui_contract`
- `zima_cpp_edge_treatment_method_ui_contract`
- `zima_cpp_history_deletion_ui_contract`
- `zima_cpp_translations_contract`

The exact low-level matrix includes convex and concave CUT/FUSE cases, two- and
three-branch nodes, staggered shoulders, through notches, side pockets, circular
routes, rolling plane/circle, circle/circle, ellipse/circle and spline/circle
supports, explicit angled termination and automatic additive/subtractive
cylindrical Body termination. Both new curved-terminal fixtures validate the
resulting B-Rep and compare its volume with the intentionally perpendicular
baseline.

Read-only recalculation diagnostics also covered the reported current models:
`11.prtz` Chamfer 02 remained a one-edge ZIMA route without OCCT contour
fallback, and `12.prtz` Fillet 03 extended its R10 treatment to the cylindrical
Body envelope instead of retaining the perpendicular route-end plane. The user
models were not modified by this verification.

## Broader-suite boundary

An exploratory run of the complete 545-test Windows catalog was stopped after
396 tests when a long unrelated legacy GUI scenario ceased producing progress.
All affected Fillet, Chamfer, route, preview, document and history tests reached
before that point passed again. The exploratory run also reproduced unrelated
pre-existing failures in Sketcher interaction, browser/GPU startup, legacy
command/catalog expectations, temporary hard-link permissions and parallel GUI
processes. Those results are not used as acceptance evidence and this record
does not claim a completely green repository-wide suite.

## Localization and compatibility review

No user-visible string or translation key changed. The complete `cs`, `en`,
`de`, `fr` and `ru` catalog contract passed. Documentation added by this change
is English only. Native document schemas, start templates and factory assets are
unchanged, so no format conversion or factory regeneration is required.
