# Exact projection of spline edges into a Sketch

## Data flow

Inputs are a source spline edge and a Sketch plane. The output is an owned
rational B-spline preserving the orthogonal projection shape. During explicit
body calculation, OCCT restricts the source spline to the actual edge range.
ZIMA stores degree, control points, knots with multiplicities, and weights.
The original-reference viewer packet contains these data and the stable edge
owner. OCCT enumeration position never becomes identity.

Reference → Outline projects control points into the Sketch plane while preserving
knots and weights. Sketcher evaluates rational de Boor interpolation from ZIMA data.
Picking, projection, opening Properties, and restoring saved files do not call
OCCT. Projected-spline accuracy does not depend on display mesh density. Data is
transformed with Part geometry, Assembly occurrences, mirrors, and patterns.
Subsequent body calculation uses the same knots and weights, included in the
cache input fingerprint.

## Ownership and updates

An owned curve has an independent ID and control points. An external reference is
a separate dependency. Deleting the external reference removes that dependency
and its grouping; the owned curve, points, and identities remain. Existing
dimensions and constraints on owned geometry survive.

Explicit source refresh updates control points, knots, and weights. Point identities
remain when the control-point count is unchanged. If the source disappears, becomes
ambiguous, or changes its control-point count, the last valid shape remains and
the reference becomes broken. No similar edge is substituted automatically.

Exact-spline Properties preserve degree and nonperiodic parameterization. Control
points are read-only while externally linked. Simply pressing OK does not round
original coordinates to the displayed decimal precision. After detachment, control
point positions can be edited.

## Scope of this change

Exact conversion covers B-spline edges, including rational and trimmed sources.
Recognition of other edge types is unchanged. At this stage, exact splines do not
use the old trim operation that reconstructs shapes from samples. Associative
trimming is the next separate step, followed by offsets of owned curves.
Connectors will be manual.

Persistence remains inside `.prtz`, `.asmz`, and `.drwz`, with no required companion
file. Internal versions at this stage are Part 19 / JSON 43, Assembly 16 / JSON 25,
Drawing 15 / JSON 7, and Sketch 33. Start templates and test documents are updated
together. Normal loading does not convert old formats.

## Verification

`zima_cpp_exact_spline_contract_tests` covers an analytic rational quarter-circle,
nonuniform knots, direction reversal, mirroring, persistence, source updates,
broken dependencies, detachment, and extrusion volume. It also includes a full
periodic edge, restricted parameter range, reversed orientation, and spatial
translation. A dialog test verifies coordinate precision after simply pressing OK.

The `63113_0H030_mg___773WF0593_01.stp` audit checked 2,077 spline edges in XY,
XZ, and YZ. Of 6,231 combinations, 30 projections degenerated into a point and
6,201 curves passed Sketch creation and save/load. Comparing 1,025 parameter
positions per curve against the original OCCT edge measured a maximum of
9.87818e-10 mm. This is a sampled measurement, not a formal continuous error bound.
The audit itself took 3.40 s; this is not the duration of a full Part import.

The previous conversion used up to 16 display-polyline points as new spline control
points. On the same file, it previously measured deviations up to 0.281186 mm with
0.1 mm display deviation and 0.0650588 mm with 0.01 mm display deviation.

The final Windows Release build passed all 46 CTest tests (385.29 s).

This foundation is extended by [Offset and support preservation during trimming](SKETCH_OFFSET.md).

## External references in a pending profile edit (2026-09-11)

When editing an Extrusion-owned profile, changes belong to the parent dialog's
working Sketch. Adding an external edge and Reference → Outline must use the
same active Sketch and mutation path as ordinary Sketcher tools. Previously,
writing directly to the document left the View showing an unchanged working copy,
and returning from the profile could overwrite the new reference.

The fix retains geometry in the working copy until the parent's OK. Cancel preserves
the original Sketch. `zima_cpp_owned_profile_reference_ui_contract` clicks an edge
of the first body in the actual View while editing an Extrusion of the second body,
then verifies the reference, projection, Cancel, and OK persistence. Picking and
projection invoke no OCCT and do not change shared container placement.

Repair verification: Windows Release, the new integration test passed in 8.08 s.
The same reference, outline, Cancel, and OK checks also passed on working copies of
`Projects/part.prtz` with import q63113-0H030-A. That large-model test ran separately
because it exceeded the usual 120 s limit. Subsequent Sketch handle and offset
Properties checks also passed.
