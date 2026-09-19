# Sketcher offset

## Agreed scope (2026-09-19)

Native Sketch curve offsets are implemented. The user confirmed that offsets
from ZIMA geometry are the required scope; STEP-derived offsets are not required
and must not be scheduled as remaining work. Existing projection support and
historical STEP verification below do not create a development requirement.

## Interaction

**Offset** is in the right-hand menu beside Trim and Mirror. It opens **Offset
Properties** with an owned Sketch curve, a positive distance, and **Flip**.
Use preselection or activate the field and select a curve in the View. A purple
preview and an arrow from the curve start indicate the offset side. The arrow
has a minimum screen size so it remains visible for small offsets.

Double-clicking the resulting curve or choosing Properties in the View or tree
opens the same dialog. OK creates/edits one revision; Cancel discards the preview.
The shared PropertiesSubWindow also supports confirmation by MMB double-click
over the View. A short MMB click ends reference input and temporary inspection.
**Free** removes the dependency on OK while retaining current native geometry.

## Ownership and trimming

An offset references only an owned curve in the same Sketch. First project an
external edge into an owned curve. That curve may have an external dependency;
the offset creates no separate external reference.

The supporting curve and retained interval are separate. Trimming the source
preserves the entire support and its identity, so an existing offset retains its
shape. A new offset from an already trimmed curve adopts its selected interval.
Trimming an offset changes the retained interval, not its supporting shape.
After splitting, each visible remnant has its own stable ID. Remnants of one
offset share the operation identity; distance and Flip are edited together.

Intersection endpoints retain the intersecting curve identity and parameters of
the same branch. Small changes are tracked by local numerical solving. Losing an
intersection or moving outside the tracked branch retains the last shape and
marks the dependency invalid. Invalid curves are red and rejected by profile calculation.

Dependent results have no freely movable control polygon. The solver protects
their points, and internal control points are not offered in the View. Freeing
retains points, knots, weights, and the current curve identity. Dependent offsets
are reparameterized when a trimmed source is freed, preventing them from being
trimmed again. If they also use a hidden portion outside the freed interval, free
them first; the command rejects that loss of supporting geometry.

## Geometry

Inputs include segments, circles, arcs, ellipses, elliptical arcs, and B-splines,
including rational STEP projections, periodic splines, and interpolation splines.
Calculation runs in the local Sketch plane without OCCT. OCCT is used only for
explicit body modeling and independent geometric tests.

Supports use rational B-splines with preserved knots and weights. Trimming uses
exact knot insertion and splitting. Segments and circular arcs have exact offsets;
other regular curves use adaptive cubic segments against the mathematical offset,
with a persisted tolerance of 0.00001 mm. This tolerance is independent of import
display tessellation. Calculation rejects undefined tangents, local reversal at
cusps, and approximations that fail the tolerance check.

The initial command operates on one curve. It does not automatically join chains,
create corner connectors, or choose loop branches; draw connectors manually.
Large intersection changes may require repairing the trim.

## Persistence

Sketch 33 stores `curve_supports`, `curve_trims`, and `offsets`, including intervals,
intersection dependencies, and the last calculated native geometry. Everything
stays inside `.prtz`, `.asmz`, and `.drwz`, with no required companion files.
Versions at this stage: Part 19 / JSON 43, Assembly 16 / JSON 25, Drawing 15 /
JSON 7. Templates in config and test documents are updated together. Normal
loading does not convert old formats.

## Verification

Numerical tests cover exact rational-spline trimming, both offset sides, periodic
curves, source changes, support preservation after trimming, intersection endpoints,
loss of the intersecting curve, point protection, freeing, and save/load. The kernel
test checks dependencies after trimming and changing a STEP projection, and the
exact volume of an extruded annulus from a circle and its offset. Exact closed
splines have a closure property independent of periodic parameterization. Circles
remain analytic during profile construction; checks do not mistake 0.001/0.0001 mm
gaps for polygon contact. Contours are classified by nesting, so an offset can
serve as a hole or an outer contour.

This test exposed inaccurate OCCT volume integration for a rational extruded
surface. Volume calculation therefore uses adaptive Gauss–Kronrod integration
with knot-span awareness for B-spline/Bezier and extrusion/revolution surfaces.
This changes no geometry.

An audit of the first 100 spline edges in `63113_0H030_mg___773WF0593_01.stp`
tried XY/XZ/YZ and offsets of ±0.1 mm. Of 600 cases, 587 were accepted. At 1025
parameter positions per accepted result, the largest deviation from independent
OCCT position and tangent data was 0.00000247619 mm. Rejections: 9 cusps, 2 tangent
singularities, and 2 tolerance failures. This is a sampled check, not a formal proof
for every curve.

The final Windows Release build on 2026-09-11 passed all 48 CTest tests (418.32 s).
The main-window integration test checked owned-curve selection, Flip, preview,
MMB double-click confirmation, reopening by double-click, distance editing, Cancel,
and save/load. The dialog and purple arrow were also visually checked in a
screenshot of the actual View.
