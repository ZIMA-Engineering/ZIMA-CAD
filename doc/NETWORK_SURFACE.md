# Boundary Surface

Implemented and verified on Windows, 2026-09-27. Portable release acceptance is
recorded in [the release record](releases/2026092702.md).

## Scope and inputs

The agreed first command creates one general surface from exactly four boundary
chains in perimeter order. It does not create sheet material, unfold a surface,
or extend an extrusion up to the surface. Those are separate future operations.

Each boundary references either one supported curve in a Sketch, one complete
open Sketch chain, or one complete open 3D Curve. A Sketch chain must be connected
and unbranched. Individual lines, arcs, elliptical arcs and B-splines use the
existing exact Sketch geometry adapter. Sources must precede the surface in the
same editable Body. References retain source identities, not viewport polylines.

## Calculation and output

The installed OCCT 8.0.0 provides `BRepOffsetAPI_MakeFilling`. Explicit OK or
Regenerate builds a C0-constrained filling from the authored edges. Chain
orientation is resolved automatically without changing the source geometry.
Endpoints must close the perimeter within document linear tolerance. The
calculation checks boundary intersections, OCCT shape validity, fitting error
and nonzero area. An invalid definition reports an error and is not committed.

The result is a surface, with zero material volume, and retains semantic face,
boundary-edge and corner identities. Boundaries determine the generated interior;
they do not uniquely prescribe every possible surface spanning the same contour.
No user-editable interior grid or tangency controls are included in this version.
Highly twisted or otherwise incompatible boundaries may be rejected.

## Interaction and persistence

The Modeling command is **Boundary Surface** (localized Czech label:
**Hraniční plocha**). Its icon is a yellow curved quadrilateral with four boundary
edges and an interior grid. One internal properties dialog serves creation and
editing. Four numbered reference rows use shared input, clear and independent
inspection controls. Clearing a reference retains its structural row. A short
middle click ends reference entry and inspection. OK calculates and commits;
Cancel restores the previous model. Editing follows the common rollback rule.

The geometry follows its source curves. This command does not modify or extend
the protected common container-placement solver. The native Part stores all four
source references; no required sidecar or geometry cache is introduced. Source
geometry participates in the calculation fingerprint. History dependencies
prevent moving the surface before its inputs.

## Verification

Nine focused contracts passed: boundary geometry/document/workspace, reference
dialog, full GUI creation/editing, translations, native documents, history
commands, existing surface profiles, existing surface GUI, and five-language
application lifecycle. Coverage includes planar and warped patches, exact arcs
and rational B-splines, 3D Curve chains, reversed directions, gap/crossing
rejection, canonical source identities, source-change invalidation, native
save/reopen, unchanged OK, Undo/Redo, rollback and Cancel. GUI checks create new
documents from regenerated factory templates and verify active editing context.

The combined Windows translation test uses an 8 MiB stack for its large fixture
frame; this changes the test executable only. These checks do not guarantee that
all arbitrary four-sided contours produce an acceptable surface. The previously
recorded Sweep 2D/Linear Pattern layout failures remain outside this change.

## Deferred extensions

Interior curve networks, movable interior control grids, tangency constraints,
trimmed patches with holes, multiple patches, sheet creation and unfolding, and
extrusion up to the surface remain outside this first command.
