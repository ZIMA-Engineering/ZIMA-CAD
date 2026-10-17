# Sketch origin and drawing contact verification

Windows native development build, OCCT 8.0.0, 2026-10-17.

## Changed contracts

Command input prioritizes the persisted Sketch origin over native points,
intersections, midpoints and axes. Hitting both axis corridors offers that real
origin, including their corner overlap. Ordinary native-point dragging retains
its normal selection contract. Polygon creation now consumes its center and rim
snaps, as the other curve tools already do.
An actual origin PointReference now exposes the ordinary selectable C marker;
numerical coordinates at zero alone do not create a marker or a constraint.

When a circle-center or ellipse defining-point contact fails to converge, an
analytic ellipse-axis seed or the existing constrained point manipulation path
supplies an initial guess. The final common solver still
validates the new relation with the original dimension definitions. Redundant
or impossible relations remain rejected.

Drawing dimensions accept an analytically edge-on conic as a straight support
without changing its source identity or native curve. Arbitrary curve/line point
entry is removed. Real endpoints, straight-support midpoints, centers,
intersections and tangencies retain their respective meanings. Radial arrow
placement remains an annotation operation. Closed-circle seams are not endpoints.

Convex, plane-trimmed cone fragments expose exact visible generators using their
persisted analytic support and Boolean ancestry. Uncertified trim boundaries are
not approximated as exact geometry. The actual bolt head has two such contours.

Sketch construction strokes and their existing toolbar icon use #4DD811.
Construction strokes are painted after ordinary Sketch wires, above solid
geometry; cyan selection/hover and later point/annotation overlays are preserved.
Other uses of the existing orange construction color are unchanged.

## Verification matrix

| Area | Exercised variants and evidence |
| --- | --- |
| Native picking | XY/XZ/YZ; four corners of overlapping axis corridors; one-axis-only hits; different Sketch owners; origin versus native points/intersections/midpoints; existing viewer contract suite |
| Origin creation GUI | Point, segment both ends, construction line, polyline, rectangle both corners, slot both centers, 4/6/8-gon center/rim, circle center/rim, arc center/start/end, ellipse center/major/minor, elliptical arc five roles, both spline kinds first/interior/last points; three planes; persisted two-coordinate origin reference, equations, three attempted anchor drags, Undo/Redo, save/reopen |
| Bolt relief native | Six source files; axis Y and origin contacts; radius/diameter; driving/reference; locked/unlocked; three successive value edits, independent radius/center equations, read-only external sources, rejected anchor drags, actual free axial movement after removing C, Undo/Redo; 16 cases per file |
| Drawing native | Straight projected circle and bevel, both angular selection orders, angle sectors, repeated placements, serialization, linear and parallel references; tilted ellipse rejection; auto/explicit tangents on both sides; closed seam/arbitrary-point rejection; real arc endpoints and radial arrow placement |
| Actual bolt Drawing GUI | Two radius contours, four chamfer spans and four chamfer/axis angles; exact offered references, creation Undo/Redo, repeated radial annotation drags, independent equations, save/reopen |
| Construction presentation | Coincident ordinary and construction strokes; exact green framebuffer pixels; normal/active and checked/unchecked icon pixels in origin GUI matrix |
| Factory FORM | Existing native geometry tests for DimpledHole, BridgeLance, CornerGusset90 and VentilationWindow, including insertion geometry |
| Localization | No new user-facing strings; removed arbitrary-point choice; five-language catalog/coverage contract passes |

Native viewer, Sketcher, drawing measurement and drawing command suites pass.
The actual bolt Drawing GUI matrix passes. The original bolt relief GUI fixture
passes all six cases: X/Y/origin in both selection orders, redundant rejection,
three anchored drags, C Undo/Redo and native save/reopen after owner confirmation.
Embedded Sketches remain drafts until the owning feature's OK; GUI checks inspect
their real viewer markers/points during editing and native equations after commit.
Origin creation runs cover cases 0–101 in segments; the slightly rotated ellipse
minor-point case originally failed at case 90, was repaired and passed on rerun.
An independent native test preserves that exact numerical input as a regression.
The origin C label is also selected with the mouse and deleted in XY/XZ/YZ.
Deleting it restores both point freedoms; Undo/Redo restores/removes the same
relation, followed by anchored drags and save/reopen.

## Factory library audit and repairs

`tools/audit-bolt-sketches.ps1 -Directory config/lib` audits all ten native Part
assets currently present, including the two bolt reference copies. This is a
native solve audit of every persisted Sketch, not merely cached geometry.

All six bolt files had one free translation in the shaft and relief circles,
and three free motions in the hexagon Sketch. Repairs add origin references to
the three circle centers and an X-axis incidence to the positive-X hex vertex.
All four Sketches in each repaired bolt solve with zero remaining freedom after
native regeneration, save and reopen. Dimension values and reference identities
are retained. Previously displaced relief centers are intentionally corrected;
this is not a claim of geometrically unchanged faulty models.

DimpledHole had two modeling circles centered numerically at zero but constrained
to only one axis. Both now have a persisted origin reference, zero remaining
freedom, and native regenerated/reopened geometry. Its original active role Body
is restored. The manufacturing symbol's shared circle center is also anchored;
its two radii remain editable. All four factory FORM geometry suites pass with
the modeling repair; DimpledHole also passes after the symbol-center repair.

BridgeLance's modeling origin already has both axis incidences; offset arc
centers are intentional. CornerGusset90's modeling profile is fully constrained.
VentilationWindow's modeling profiles are located by symmetry/axis relations;
the rounded profile's remaining radius parameters do not imply a free center.
Manufacturing-symbol Sketches retain their authored editable shape freedom;
their free parameters are not silently fixed during a modeling-origin repair.
No user project document is rewritten.

## Limits

Linux compilation and GUI execution are not verified on this Windows host.
There is no document-format or shared container-placement change.
The creation GUI matrix does not establish every dimension-kind combination for
every curve tool. Native general Sketcher regressions and the focused bolt
dimension matrix supply additional coverage; external origin placement, trimming
of every newly anchored curve type, both themes, and solid-occluded construction
stroke screenshots are not separately exercised by the new focused matrix.
No release package is produced by this work.

## Drawing viewport and line-priority follow-up

The interactive depth renderer previously rasterized the complete view extent,
including geometry outside the canvas. At high zoom its image-size cap could
downsample visible strokes, displace their raster representation relative to
exact picking geometry and clamp different stroke widths to the same GL pixel.
Sheet rendering now intersects raster bounds with the visible painter viewport,
including a stroke-width margin. It retains the same model geometry, depth test,
selection references and separate vector-output preparation.

Thin strokes precede thick visible strokes in the depth renderer and both software
stroke paths. A thin crossing can no longer overwrite the strong radius contour
merely because its input edge occurred later. Hidden strokes remain behind visible
strokes; annotation selection overlays retain their established interaction.

Verification passes: the full drawing GUI suite; a radius crossed by a thin edge
in both input orders with identical rendered pixels; distant off-screen geometry
without degradation of visible radius ink; the actual bolt's four chamfer picks
at view scale 800 with visible ink at the exact picking positions; normal-scale
two-radius/four-span/four-angle GUI creation, radial drags, Undo/Redo and reopen;
native drawing command/measurement and five-language localization contracts.
The original cached Part and an independently regenerated copy both expose the
same two selectable analytic head contours. User source documents are unchanged.

The Shaft Thread dialog no longer constructs an unused Origin button. Its four
face-reference inputs and their inspection controls are unchanged; no shared
container-placement code or localized text is changed.
Its command consumes the existing origin-binding guard so generic registration
does not recreate that button. The focused native GUI check passes: button
absence, four reference rows, pending preview, OK/Cancel, Undo/Redo and preserved
cylinder identity (`ZIMA_VERIFY_CONSOLE_ONLY=1` and
`ZIMA_VERIFY_SHAFT_THREAD_DIALOG_ONLY=1`, `--verify-startup`).

The broader console GUI suite did not complete: sandboxed file-copy hard-link
creation was denied; a retry with access progressed past that stage but failed
the earlier menu-import transaction/timing check before reaching Shaft Thread.
That unrelated import behavior was not changed in this task. The focused Shaft
Thread test and complete drawing GUI suite pass separately.

## Reopened Drawing contour hover

The earlier bolt checks explicitly projected the source before testing. They
therefore missed the reported open-file path: the saved Drawing contained four
analytic contours but lacked the two head generators in its stored projection
and measurement derivatives. Opening reused those derivatives unchanged.

Drawing loading now prepares ordinary interactive projections directly from the
embedded calculated mesh, using the same shared preparation for display and
picking. This does not read or regenerate the source Part, change topology,
create an undo transaction, or change the file format. Crops, breaks, authored
view properties and thread designation metadata remain separate from the derived
projection. Section projections retain their existing loading path.
Native measurement curves and points retain their persisted data; only
`drawing-contour:` curves are replaced with current camera-derived contours.
This also avoids exposing source Sketch curves present in the display packet's
original-reference ancestry but intentionally absent from measurement entry.

The actual bolt checks now open the saved Drawing without source reprojection.
Native hover probes cover 99 interior positions on each head generator for both
angular and automatic linear picking. The Windows GUI checks cover linear and
angular hover on the four head/end chamfer contours at scale 800, checking cyan
highlight pixels at visible probe locations, followed by reference acceptance.
A synthetic convex cone regression saves deliberately incomplete projection
derivatives with an embedded calculated source and an unavailable source Part;
reopening must restore the exact selectable generator.

Final Windows checks pass: five native/catalog contracts (Drawing dimensions,
measurement dimensions, view operations, Thread drawings and translations), four
GUI contracts (measurement dimensions, factory surface contours, breaks and
details), the actual saved bolt's 396 native hover probes and eight high-zoom
GUI linear/angular cases. The local product executable was rebuilt successfully;
`zima-cad.bat` retains its existing development target. The user's Drawing and
Part files were not modified.

Localization review: this correction introduces no user-visible strings. The
five-language catalog contract remains required. Linux runtime verification is
not available on this Windows host.

## Circular rim junctions and resized Drawing dimensions

The user's side-view screenshot exposed a sampled circular rim stopping short
of its exact extremum. The saved head rim has a radius near 2.75 mm but its
display samples reach only about 2.72995 mm. An adjacent analytic chamfer reaches
the true endpoint, leaving a visible gap at high zoom. View-local profile
preparation now inserts analytic extrema within each existing circular segment
when its plane is edge-on. This applies to complete circles and trimmed arcs;
trim endpoints, native references, exact splines and source meshes are retained.
Side-direction arrays are interpolated at inserted display samples to retain
their existing tangent classification.

Outside linear and chain dimensions now retain their signed paper clearance
from the calculated projection when a source change refreshes a Drawing view.
The shared view-edit and Regenerate paths compare the previous/current resolved
presentations and projected extents, and adjust existing layout offsets inside
the same Drawing transaction. Unchanged views do not move dimensions. Internal
dimensions retain their witness-relative placement; angular and radial layouts
retain their established attachment behavior. No native format, source geometry,
container placement, Sketcher solver or user-visible text changes are involved.

Verification passes on Windows: seven circular-rim fixtures (complete sampled
circles with different phases/counts and a trimmed arc); 24 independent linear/
chain clearance checks across horizontal/vertical directions, both outside
sides and three successive size changes; the actual bolt at scale 800 with
18 junction ink probes and the existing eight hover cases; five native/catalog
contracts and five Drawing GUI contracts including rendering, dimensions,
contours, breaks and details. The dedicated GUI clearance case uses real native
Part calculations and the Regenerate action, with three successive source
resizes, dimension dragging, unchanged Part revision during Drawing regeneration,
Drawing Undo/Redo and save/reopen. Its dimension lies across the model from its
witness points so witness-relative placement alone cannot pass the test.

The focused clearance matrix covers ordinary unbroken views; clearance-specific
variants for cropped/broken views and oblique parallel dimensions have not been
exercised. Their established general GUI contracts pass where listed above.
Linux runtime verification remains unavailable on this Windows host.
