# Fillet and Chamfer node verification — 2026-11-07

## Scope

This change simplifies the visible Fillet/Chamfer selection contract and adds
exact ZIMA completion for the requested two- and three-branch node cases. It
does not change native document schemas, persisted reference identity, factory
assets, container placement, geometry-side interpretation or OCCT/Qt versions.

The visible calculation choices are ZIMA and OCCT. ZIMA is the existing durable
automatic route: it uses the exact custom builder inside its supported boundary
and falls back to OCCT outside it. The former strict experimental choice remains
loadable only as legacy persisted state and is no longer exposed by Properties
or the command API.

## Geometry

The solver keeps segment tools and node connectors separate:

1. build one exact signed band for every selected segment;
2. classify shared endpoints from exact topology and material occupancy;
3. apply consecutive same-sense bands in batches;
4. apply the required node connector as a signed CUT or FUSE;
5. unify only redundant coplanar/coaxial seams while retaining topology history.

For an orthogonal three-branch Chamfer, the missing connector is the tetrahedron
between the pairwise `A` offsets and the intersection of the three Chamfer
planes; its volume is `A^3/12`. A staggered two-branch shoulder uses the oblique
square pyramid of volume `A^3/3`. The Fillet equivalents use a radius cube minus
the trihedral sphere or, at the staggered shoulder, minus both perpendicular
rolling cylinders. The sphere is attached to all three source-edge ancestry
records.

Tangent two-segment continuation remains one line. Non-collinear selected nodes
accept only same-sense inner or outer branches; mixed or ambiguous configurations
are rejected by the exact builder and therefore fall back to OCCT in user-facing
ZIMA mode. `A x B` and `A + angle` Chamfer require exactly one connected open
route. Rejection occurs before mutation.

## Interaction and persistence

- Each authored segment is one top-level Properties item with its own remove
  control. No route branch, manual end-face column or route-level control is
  displayed.
- Clicking an already selected current segment or descendant removes its stable
  authored logical item.
- The preview publishes the analytical edge treatment wire and unique segment
  endpoints as azure markers without calculating a result Body.
- Split descendants are shown as inspection data for the authored item and do
  not replace its persisted identity.
- Source resizing recalculates the result while retaining the exact treatment
  definition. Native save/reopen, cold evaluation and Undo/Redo preserve it.

## Verification results

The Windows Release build completed for the changed application and test
targets. Direct exact-builder suites passed:

- `zima_cpp_occt_planar_edge_fillet_tests`: 1.27 s;
- `zima_cpp_occt_planar_edge_chamfer_tests`: 0.84 s;
- `zima_cpp_direct_batch_edge_treatment_tests`: passed;
- `zima_cpp_occt_edge_treatment_support_tests`: passed;
- `zima_cpp_solid_state_fillet_document_tests`: passed.

The direct suites compare valid shape, volume, Fillet area and result ancestry
against OCCT in both edge orders. They cover outer and inner two-/three-branch
nodes, mixed independent CUT/FUSE bands, the staggered shoulder, blind pocket,
through notch, side-reaching pocket, circular paths and existing angled or
tangent terminal cases. Each suite includes many complete operations; individual
representative operations remain below the requested one-second interaction
target.

Command and UI checks passed:

- `zima_cpp_edge_treatment_command_tests`, including native save/reopen, cold
  calculation, upstream resize and Undo/Redo;
- `zima_cpp_edge_treatment_query_tests`;
- `zima_cpp_edge_treatment_method_ui_contract`;
- `zima_cpp_edge_treatment_fragments_ui_contract`;
- `zima_cpp_edge_treatment_ui_contract`;
- `zima_cpp_symmetric_edge_treatment_ui_contract`;
- `zima_cpp_treatment_selection_ui_contract`;
- the complete shared `zima_cpp_ui_contract_tests` executable;
- `zima_cpp_translations_contract` for Czech, English, German, French and
  Russian.

The broad `zima_cpp_console_ui_contract` still stops before its edge-treatment
section at the unrelated existing Sketch assertion
`Console did not persist the linked native segment and its endpoint references`.
It reproduced twice. No production Sketch code was changed in this scope; the
focused treatment GUI, command and persistence contracts above reached and
passed the changed paths. A repository-wide green result is not claimed.

## Remaining limits

- The exact custom node connectors currently require orthogonal branches.
  Other valid geometry uses the OCCT fallback in ZIMA mode.
- Selecting a mixed inner/outer node as one node is intentionally unsupported;
  independent inner and outer routes remain supported in authored order.
- Variable-radius Fillet remains on its established OCCT path.
- Native Linux compilation and execution are not established by this Windows
  verification.
