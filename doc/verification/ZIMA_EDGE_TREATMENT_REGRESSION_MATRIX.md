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
| RADIUS-08 / invalid dependent Fillet repair | The first of two dependent ZIMA Fillets is made invalid before editing the next one. | automated contract passes | The last valid Body remains visible. The downstream failed feature opens without another blocking prompt; its radius and references remain editable; Cancel is non-mutating; deletion and source repair survive Undo/Redo; and the repaired chain saves and reloads calculated. Covered by `zima_cpp_invalid_dependent_fillet_ui_contract`. |

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

The 2026-11-14 candidate adds a stacked cylindrical Body fixture which
reproduces the `READIUS2.prtz` Fillet 03 geometry class. It creates two
independent side Fillets, then connected five-edge routes after an R10
treatment. The final route is swept through R4, R8, R9, R10, R11, R12, R14 and
R18. It produces exact empty intersections for two optional terminal
extensions; those tails are discarded while the main cutter remains
mandatory. Every sweep value requires valid topology, one final cutter,
non-empty face lineage for all five selected edges and no result volume outside
the original cylindrical envelope. This permanently covers values below, equal
to and above the adjacent Fillet radius.

The same geometry class now also covers the current `RADIUS.prtz` Fillet 15
failure. Its persisted source edge resolves to a non-straight five-member U
route after preceding treatments. For additive treatment, the base sweep is
clipped to each local curved Body terminal but no remote terminal tail is
created. The R4-R18 sweep additionally asserts that changed material does not
pass the real route endpoints in either terminal direction. Straight split
routes keep the established single-edge-equivalent continuation behavior.

An additional actual-model gate covers Fillet 12 in the later saved
`RADIUS.prtz` copy. The selected straight route spans Z40 to Z48 and ends
tangentially on an earlier cylindrical Fillet surface without a remote planar
cap. The added material must remain within that source-edge interval; the test
projects every vertex of the exact added B-Rep onto the source direction and
rejects the former extension to Z52. Existing tangent routes with a genuine
remote terminal cap retain their continuation and remain covered by the
synthetic planar Fillet suite.

The Fillet/Chamfer GUI contracts also reject an overextended analytical
preview independently of the preview generator. Final treatment contacts are
checked against both their longitudinal rail and terminal section. These
verification contacts are separate from the view labels: labels identify every
distinct source node of each connected selected path (`1.1`, `1.2`, `1.3`,
and so on; the next disconnected path starts at `2.1`). A closed path labels
its shared start/closing node once as `1.1`. Display samples and generated
treatment contacts are not labelled. The ordinary and symmetric GUI variants
pass.

On Windows, clean regeneration also passed for the disposable copies of
`RADIUS.prtz`, `READIUS2.prtz` and `RADIUS-03.prtz`. The Properties rollback
preview precheck covered RADIUS Fillets 04, 05, 09, 11, 12, 13 and 14, RADIUS
Chamfer 01, READIUS2 Fillet 03 and RADIUS-03 Fillet 01. Every stored route
resolved, every published transient stroke exactly matched the analytical
preview, Cancel preserved the document revision and developer inspection of
the ten screenshots found no disconnected or remote preview span. These
results do not replace the user-directed manual acceptance rows or the missing
exact saved-case gates.

A later precheck used the then-current `RADIUS.prtz` definition, which differs
from the historical rows above: Chamfer 01 is equal-distance A5 with two
independent one-edge source routes, and Fillet 15 is constant R4 with one
persisted source edge resolving to the five-member U route. A full disposable
copy regenerated and previewed Fillet 15; a second copy with only downstream
Fillet 15 removed did the same for Chamfer 01. Published preview geometry and
Cancel passed. Its then-current endpoint-only labels are superseded by the
all-source-node contract above. The source file was not changed.

A read-only follow-up sweep (a fresh in-memory open for each value and no save
to the project files) passed R2-R15 for the retained ZIMA Fillet 04
reproduction, R2-R12 for current RADIUS Fillet 05, R1-R10 for current RADIUS
Fillet 11, R1-R8 for the retained four-edge RADIUS Fillet 09 definition,
R4-R18 for current READIUS2 Fillets 01 and 03, and A1-A10 for current RADIUS
Chamfer 01. The current project files have evolved from several definitions in
the manual catalog, so these ranges are candidate evidence rather than a
replacement for the exact saved-case and user-acceptance gates.

The 2026-11-15 follow-up repaired four additional cases without modifying the
user project files. A constant ZIMA Fillet whose connected route changes
support angle now uses the existing rolling-support blend path instead of the
planar profile sweep; the actual RADIUS Fillet 13 completed a changed-value
calculation, Undo/Redo and native save/reopen in a disposable copy. Fillet 12
continues to use the planar route and its established bounded tangent-terminal
extension. The READIUS2 Fillet 03 preview now clips both rails at both ends when
a Boolean split exposes several coincident cylindrical boundary fragments; a
synthetic regression independently rejects an intersection outside the route's
terminal interval. The cyan route selected for READIUS2 Fillet 06 resolves to
one closed ten-member path: node 1.9 continues through the closing arc to the
single 1.1 start/closing label, so no extra repeated label or route member was
added.

The same follow-up added ZIMA A x B Chamfer support for one complete circular
edge. Direct kernel tests cover both adjacent-face choices and verify the
removed volume independently. The public command regression covers creation,
FLIP, changed-value editing, Undo/Redo, native save/reopen and a cold
recalculation. The focused Fillet, Chamfer, preview and command suites all
passed on Windows, as did the five-language UI translation contract. Linux was
not run on this Windows host; the implementation is shared C++ and still
requires the corresponding native Linux verification before a cross-platform
completion claim.

The 2026-11-16 candidate makes the open-route fallback explicit. Both physical
end vertices and their independent `Cut` / `No cut` modes are persisted in Part
format 53. A new or changed endpoint starts as `No cut`, so the generated tool
cannot pass through the Body in search of a later face. `Cut` uses the complete
local boundary and selects the retained material side independently at each
end. The regression rejects the previously observed opposite-side trim, remote
material tunnel and asymmetric-pocket continuation. Equal-distance, A x B and
A + angle Chamfers run the same endpoint matrix as Fillet, including a complete
circular A x B edge. Closed routes remain without endpoint choices.

The GUI matrix double-clicks the purple open-end label, exercises both localized
menu choices, verifies the analytical preview after each change, and checks
Cancel, OK, Undo/Redo and native save/reopen. Shaft Thread inspection now
requires both diameter and thread-length dimensions after double-click. Opening
A x B Chamfer Properties must retain the shaded display mode. Sketcher coverage
also includes native/external circle incidence, repeated angular/radial drags,
dimension edits, impossible/redundant rejection, Trim, Undo/Redo, save/reopen
and the enabled Image command. These focused Windows checks do not replace the
manual catalog or native Linux execution.

Deleting an earlier Fillet or Chamfer is also an automated dependency gate. A
later treatment with mixed surviving and missing routes is retained, its
missing route and aligned metadata are pruned, and its surviving route is
recalculated. Undo/Redo and native save/reopen must preserve the repaired
definition. A later treatment is marked unavailable only if no usable route
remains. This passes in `zima_cpp_history_deletion_tests` and
`zima_cpp_history_deletion_ui_contract`.

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
