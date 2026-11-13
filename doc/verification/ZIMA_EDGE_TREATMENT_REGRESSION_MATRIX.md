# ZIMA Fillet and Chamfer regression matrix

## Purpose

This is the mandatory acceptance matrix for changes to the ZIMA Fillet and
Chamfer builders, their analytical previews, reference publication and failed
feature editing. A change must not be committed, published or included in a
Windows or Linux release while any previously accepted case regresses.

Passing `BRepCheck_Analyzer`, returning no calculation error or producing a
nonempty solid is necessary but not sufficient. A geometrically wrong result
can still be topologically valid. Each accepted case must therefore verify its
expected boundary, terminal trimming and absence of auxiliary treatment
geometry.

The user models are read-only acceptance inputs under
`C:/Users/vladi/Prace/ZIMA-Engineering/03-CAD/01-ACTIVE_PROJECTS/ZE-CAD`.
Verification must use disposable copies and must never overwrite the originals.

## Release gate

Every affected change must pass all applicable checks below before it can move
forward:

1. The feature calculates to a valid nonempty result when the requested
   geometry is feasible.
2. The result follows the selected route and is trimmed by the exact relevant
   terminal faces or intersections. It has no teeth, tongues, caps, chamfer-like
   remnants or treatment surface protruding beyond its intended terminal.
3. A route that reaches a preceding Fillet or Chamfer is resolved from actual
   intersections and material side, not only by comparing radii or surface
   types.
4. Increasing or decreasing a radius remains continuous until a real geometric
   limit is reached. Crossing the radius of an adjacent treatment is not by
   itself a failure condition.
5. The analytical azure preview represents the same route, radius and terminal
   solution as the calculated result. It must not switch to an unrelated
   simplified path when the radius changes.
6. Selection order and route orientation do not change an otherwise equivalent
   result.
7. Generated ZIMA faces, boundary edges and endpoints remain selectable and
   publish stable owned references for dependent operations.
8. Save/reopen, regeneration and Undo/Redo retain geometry, route membership,
   parameters, calculation method and reference identity.
9. An invalid/red feature remains editable. Its stored references can be
   removed or replaced and its parameters can be changed. Editing a dependent
   invalid feature must not crash; Cancel restores the previous state and OK
   retries calculation from the correct rollback boundary.
10. The corresponding Chamfer construction is exercised wherever the terminal
    and node principle is shared with Fillet.
11. When requested by the user, the candidate build is opened in ZIMA-CAD with
    disposable copies of the listed user models. The cases are presented one
    by one at their recorded feature and parameter values for manual visual
    acceptance. No Fillet/Chamfer change may be committed, pushed or released
    until the user accepts that visual run. A rejected case returns the change
    to development even when every automated check passes.

## User-model cases

The stable manual run order and acceptance identifiers are maintained in
[`ZIMA_EDGE_TREATMENT_VISUAL_CHECKLIST.md`](ZIMA_EDGE_TREATMENT_VISUAL_CHECKLIST.md).

Status values are:

- **accepted**: the expected visible result has been confirmed and is a
  must-preserve baseline;
- **defect**: the current saved result or calculation is known to be wrong;
- **capture required**: the case is known, but its exact accepted shape or
  parameter interval still needs to be recorded before automation can use it as
  a numerical oracle.

### `RADIUS.prtz`

| Case | Persisted definition | Status | Required result |
| --- | --- | --- | --- |
| RADIUS-01 / Fillet 03 | ZIMA, constant R10, one two-edge route | capture required | The sweep reaches the true terminal intersection and is completely trimmed by the terminating faces. It must not cross the red terminal face and must not leave the previously observed chamfer-like end remnant. |
| RADIUS-02 / Fillet 05 | ZIMA, constant R6, two one-edge routes | defect/capture required | Both route ends must be solved and clipped as one valid corner treatment. No saw teeth, doubled caps or protruding radius fragments may remain. |
| RADIUS-03 / Fillet 04 | currently persisted as OCCT, constant R5 | capture required | This saved feature is not evidence for the ZIMA builder. A separate ZIMA fixture must capture the demonstrated radius sweep and its correct preview before it becomes an automated ZIMA gate. |
| RADIUS-04 / Fillet 09 | ZIMA, constant R4, one four-edge route using source, Fillet 08 and Fillet 01 owners | accepted baseline, currently regressed | Preserve the previously working compact treatment. The route must not grow into the large tent/ear shape or expose untrimmed auxiliary geometry. |
| RADIUS-05 / Fillet 10 | ZIMA, constant R4, one seven-edge mixed-owner route | capture required | Preserve exact route membership and solve every terminal without teeth or auxiliary caps. It must remain valid when neighboring accepted cases are corrected. |
| RADIUS-06 / Fillet 11 | demonstrated during the working session but not present in the current saved file | capture required | Recreate and save the selected route plus a radius sweep. The result must change continuously over the feasible interval and must not fail merely because its radius approaches or crosses an adjacent Fillet radius. |
| RADIUS-07 / Chamfer 01 | ZIMA, equal-distance A=2, one five-edge route using source, Fillet 09 and Fillet 10 owners | defect | The analogous shared terminal solution must calculate. Its route, preview and trimming must match the Fillet terminal principles and must not inherit an unrelated azure path. |
| RADIUS-08 / invalid dependent Fillet repair | The first of the last dependent Fillets is made invalid before editing the next one. | defect | Editing the downstream failed feature must not crash. Its references and radius remain editable so the history can be repaired or the feature removed. |

### `READIUS2.prtz`

| Case | Persisted definition | Status | Required result |
| --- | --- | --- | --- |
| RADIUS-09 / Fillet 01 | ZIMA, constant R14, two one-edge routes | defect (preview) | The calculated geometry is present, but the feature preview is not clipped at its terminal circles. Preserve the current two-branch result and generate the authored preview for both branches and their terminal sections. Both longitudinal rails must terminate exactly on the circular end arc; no endpoint may extend beyond it. The corner must not collapse into a tongue or self-overlapping patch. See [the earlier missing-preview state](evidence/20261113-radius-09-missing-preview.png) and [the current overextended-preview state](evidence/20261113-radius-09-overextended-preview.png). |
| RADIUS-10 / Fillet 02 | ZIMA, constant R11, two one-edge routes | capture required | Preserve both branches and their terminal trimming. Record the confirmed visible result as a numerical and visual oracle. |
| RADIUS-11 / Fillet 03 | ZIMA, constant R18, one one-edge route | capture required | The calculated result and analytical preview must use the same terminal path. The preview must not show the previously reported unrelated or simplified route. |
| RADIUS-12 / Fillet 04 | ZIMA, constant R4, one nine-edge route using Fillet 03, Fillet 02 and source owners | capture required | Preserve the valid multi-owner route. A radius sweep below, equal to and above the adjacent treatment radius must retain the correct analytical preview and calculate whenever geometry remains feasible. |
| RADIUS-13 / Fillet 05 | ZIMA, constant R4, one nine-edge route using Fillet 01, Fillet 02 and source owners | defect | The current file fails with `OCCT did not create a Fillet contour`. The selected route must be solved as the corresponding valid end/corner treatment; its preview and final result must use the same path. |
| RADIUS-14 / additional failing edge A | exact references still to be captured | capture required | Capture the persisted owner/key, radius, expected terminal solution and a screenshot or numerical oracle. The edge may not be omitted merely because the current command cannot calculate it. |
| RADIUS-15 / additional failing edge B | exact references still to be captured | capture required | Capture the persisted owner/key, radius, expected terminal solution and a screenshot or numerical oracle. The edge may not be omitted merely because the current command cannot calculate it. |

## Synthetic kernel coverage already present

`cpp/tests/occt_planar_edge_fillet_tests.cpp` and
`cpp/tests/occt_planar_edge_chamfer_tests.cpp` already exercise useful building
blocks, including:

- ordinary convex and concave edges;
- additive and subtractive treatments;
- smaller and larger profiles meeting a preceding radius;
- connected and split routes;
- single and split cylindrical terminals;
- two- and three-branch inner and outer nodes;
- non-right-angle wedge nodes;
- through-notch and side-pocket terminals;
- mixed CUT/FUSE cases;
- circular, spline and rolling-support routes;
- perpendicular, angled, tangent and returning terminals;
- route-order equivalence and analytical ancestry.

These tests remain required, but several currently prove only validity,
volume/area equivalence or face counts. The user-model defects show that those
assertions do not detect every wrong trim, tooth, tongue or overextended tool.
Each reproduced user case must therefore add a stronger geometry oracle, such
as exact boundary intersections, terminal sections, containment against the
input Body envelope, forbidden protrusion volume and stable generated
edge/vertex ownership.

## Work needed to complete the gate

1. Capture screenshots and exact feature definitions for every
   **capture required** row without modifying the original user files.
2. Reproduce each distinct geometry principle as a minimal synthetic kernel
   fixture. Do not copy user project data into the repository without explicit
   approval.
3. Add numerical assertions that reject the demonstrated wrong shapes, not just
   invalid topology.
4. Add the radius-sweep intervals for RADIUS Fillet 11 and READIUS2 Fillet 04.
5. Add GUI verification for preview/result agreement, selectable generated
   edges and endpoints, and invalid-feature repair.
6. Apply the same terminal fixtures to symmetric ZIMA Chamfer where applicable.
7. Run the complete matrix on Windows. Run the same shared C++ kernel matrix on
   Linux before claiming cross-platform completion.
8. Run the user-directed ZIMA-CAD visual acceptance session and record the
   accepted or rejected result for every presented case.

No user-visible text or native file format is changed by this verification
document.
