# Sketcher commands

The GUI console and standalone `zima-cad-cli` share Sketch geometry transactions
in `workspace/sketch_operations`. GUI drawing, editing and embedded profile
drafts use the same operation. A new Part Sketch owns a Sketch container in
the active body; a standalone Assembly Sketch also owns a placement container
stored directly in `.asmz`. GUI and CLI share native container insertion.

`sketch.delete` deletes a standalone Sketch and its container. A profile-owned
Sketch cannot be extracted from its feature; delete the entire owner. The
operation shares GUI commit and one Undo/Redo step. Properties and references:
[SKETCH_PROPERTIES_COMMANDS.md](SKETCH_PROPERTIES_COMMANDS.md).

## Coordinates, targets and results

Geometry commands require `sketch`, the stable Sketch ID. For mutations,
optional `document` must match the active document. Queries may address another
open document without activating it. Points are JSON `[x, y]` arrays in Sketch
local coordinates, always **millimeters**, regardless of display units.
`snap_mm` is a positive point-merging tolerance, default `0.000001`.
Displayed segment order never defines identity.

Stored circular/elliptic curve angles use radians; angular dimensions and text's
explicit `angle_degrees` use degrees. `sketch.get` reports these conventions
separately. A circle remains a circle and a B-spline remains an exact native
spline; display geometry is not the command's geometry source.

Mutations return `document`, `sketch`, `revision`, `changed` and, where applicable,
`point` or `geometry` with created IDs. Rectangle/polygon creation returns a
list of new curves. Invalid inputs are rejected before publication; identical
values and zero translations create no extra history entry. `undo`/`redo`
use document history.

## Available operations

| Command | Arguments beyond target Sketch/document |
| --- | --- |
| `sketch.list` | `offset=0`, `limit=100` (maximum 1000); metadata list |
| `sketch.get` | Metadata and entity counts |
| `sketch.entities` | `offset=0`, `limit=500` (maximum 5000); IDs and kinds |
| `sketch.entity.get` | `entity`; one stored point, curve, text, reference, constraint or dimension |
| `sketch.create` | `name`, `plane=XY` (`XY`, `XZ`, `YZ`); no input `sketch` |
| `sketch.point.create` | `position`, optional `construction=false`, `snap_mm` |
| `sketch.point.move` | `point`, `position`; respects constraints and fixed points |
| `sketch.point.fixed` | `point`, boolean `fixed` |
| `sketch.point.delete` | `point`; normal dependent-geometry deletion rules |
| `sketch.segment.create` | `first`, `second`, optional `construction`, `snap_mm` |
| `sketch.segment.centerline` | `segment`, boolean `centerline` |
| `sketch.circle.create` | `center`, `radius_mm`, optional `construction`, `snap_mm` |
| `sketch.arc.create` | `center`, `start`, `end`, optional `clockwise`, `construction`, `snap_mm` |
| `sketch.ellipse.create` | `center`, `major`, `minor` (semiaxis endpoints), optional `construction`, `snap_mm` |
| `sketch.elliptical_arc.create` | Additionally `start`, `end`, optional `reversed` |
| `sketch.bspline.create` | `points`, `degree=3`, `closed=false`, `interpolating=false`, `construction=false`, `snap_mm` |
| `sketch.rectangle.create` | `first`, `second` (opposite corners), `snap_mm`; preserves native rectangle constraints |
| `sketch.polygon.create` | `center`, `rim`, `sides` (3–1024), `snap_mm` |
| `sketch.geometry.construction` | `geometry`, boolean `construction` |
| `sketch.geometry.delete` | `geometry`; normal Sketcher rules |
| `sketch.translate` | `delta`, ID arrays `points` and/or `geometry`; shared translation solver |

JSON objects are convenient for array arguments. After obtaining an actual ID
from `sketch.create`:

```json
{"command":"sketch.bspline.create","arguments":{"sketch":"ID_FROM_RESULT","points":[[0,0],[3,8],[7,-3],[10,0]],"degree":3}}
```

B-splines allow 2–4096 input points and degree 1–25; the native Sketcher validates
the combination. `sketch.entity.get` returns read-only stored fields and accepts
no unvalidated patch. Responses above 64 KiB fail with `result_too_large`;
lists are paginated. List queries borrow Sketches instead of accumulating
geometry copies. Embedded profiles are read from persisted ZIMA definitions,
without OCCT.

## Calculation and editing

Curve changes update dependencies and validate the Sketch but **do not calculate
the body**. Responses report `body_calculated=false`. The last calculated body
remains until explicit `regenerate`, as during mouse drawing. Creating a Sketch
container uses normal Properties confirmation/calculation. The protected shared
placement solver is unchanged.

A saved embedded Sweep/Loft, Helical, Hole or Thread profile may be edited by its
exact Sketch ID; its container still determines ownership. Section Sketches are
readable, but mutations require the Section's own transaction; ordinary commands
reject them as `unsupported_sketch`. Drawing template Sketches are not controlled
by these commands. Mutations in inactive/derived bodies are rejected.

Pending GUI edits, including active Sketcher, remain protected against console
mutations. Queries are allowed. Mouse drawing invokes the shared transaction
directly rather than overwriting unfinished drafts with commands.

## Verification

Model tests cover exact curves, coordinates, moving one circle center, fixed
points, invalid input, history, no-op, original body cache, active body ownership,
embedded profiles and native Part/Assembly persistence. A real CLI process
creates, saves, reopens and extends a B-spline. GUI tests create Sketches through
both dialog and console; existing drawing, handle and offset tests exercise
the shared active-Sketch mutation.

The complete Windows Release suite passed **63/63** (388.03 s), including model,
GUI, process, reference and spline regressions: `build/sketch-full-tests.log`.
The console screenshot was visually checked. Catalog at this stage: **72 commands**.

## Offsets, trimming and additional curves

The second stage adds commands over the same Sketcher methods used by drawing
tools and Offset Properties. All change only the Sketch, share an Undo/Redo
transaction and accept `sketch` and optional `document` as above.

| Command | Arguments and result |
| --- | --- |
| `sketch.offset.create` | `source`, positive `distance_mm`, `flipped=false`; returns curve and shared operation IDs |
| `sketch.offset.get` | `geometry`; source, distance, direction, tolerance, visible interval, intersection anchors, `broken` |
| `sketch.offset.set` | `geometry`; optional `source`, `distance_mm`, `flipped`; edits the entire operation including trimmed pieces |
| `sketch.offset.free` | `geometry`; detaches offset, retaining current curve and ID |
| `sketch.curve.get` | `geometry`, `limit=4096` (1–100000); complete support spline and visible interval |
| `sketch.curve.retain` | `geometry`, `intervals` as `[start,end]` arrays; retains intervals and original support |
| `sketch.trim.pieces` | Optional `geometry`, `include_axes=true`, `offset=0`, `limit=500` (1–5000); current pieces between intersections |
| `sketch.trim` | `pieces`, `include_axes=true`, `snap_mm=0.0000001`; removes exact current pieces |
| `sketch.mirror` | `entities` (ID array), `axis`, `snap_mm`; returns new point/curve IDs |
| `sketch.oriented_rectangle.create` | `first`, `guide`, `axis`, `snap_mm`; rectangle oriented by symmetry axis |
| `sketch.tangent_arc.create` | `start_point`, `end`, `tangent`, optional `reverse`, `construction`, `snap_mm` |
| `sketch.common_tangent.create` | `first`, `second` (curves), `first_hint`, `second_hint` (points selecting the tangent branch) |
| `sketch.corner_fillet.create` | `first`, `second` (segments), `radius_mm`, `snap_mm`; native nondestructive corner fillet |

An offset derives from **our native Sketch curve**. An external reference cannot
masquerade as a native source. Trimming the source retains its complete support
geometry and does not shorten an existing offset. An offset created after
source trimming inherits the current interval. Pieces of a trimmed offset
share `operation`; distance/direction/source edits update all pieces. Cycles
are rejected. Freeing preserves geometry and dependent intervals under shared
Sketcher rules.

`sketch.curve.retain` accepts 1–1024 nonoverlapping intervals. Parameters 0–1
refer to the currently visible curve. Input order is retained, allowing pieces
across a closed seam, e.g. `[[0.75,1],[0,0.25]]`. The first retained piece keeps
the original ID; others receive new IDs. `[0,1]` is a no-op. `sketch.curve.get`
returns full support; its `start/end` delimit the visible range in that support.

For curve queries, `limit` applies to the combined count of poles, weights and
knots. Oversized support is never truncated into an invalid spline: the result
reports `geometry_omitted_by_limit=true`, degree and counts but omits `support`.
Only Sketch curve mathematics runs, never OCCT body calculation.

For trim, first query `sketch.trim.pieces`, then pass only `geometry`, `start`
and `end` from each selected row:

```json
{"command":"sketch.trim","arguments":{"sketch":"SKETCH_ID","include_axes":false,"pieces":[{"geometry":"CURVE_ID","start":0,"end":0.5}]}}
```

Before deletion, pieces are revalidated against current intersections. A missing
requested interval returns `stale_geometry` without changing the document.
Sequential piece indices and arbitrary sampled points are not accepted. Maximum
2048 pieces per request; duplicate pieces are rejected. The query also returns
endpoints for orientation. `include_axes` must match the intended subdivision.

Trimmed-curve endpoint anchors follow small intersection changes. If an
intersection disappears or switches branch, native repair state (`broken`) is
retained instead of inventing another reference. `offset.get` exposes this state.

Mirror also accepts stable base axes `sketch_axis:x` and `sketch_axis:y`.
Corner Fillet returns its stored record identity, not transient evaluated
tangency-point IDs. Original segments remain. Repeating the same segment pair
also edits the existing radius.

Integration passed **9/9** (27.80 s), `build/sketch-curve-integration-tests.log`;
matching GUI/CLI build: `build/sketch-curve-integration-build.log`. Model tests
measure spline-offset error at 1025 points (below 0.00001 mm), trim accuracy,
intersection tracking, stale rejection, tangent branches, symmetry and native
save. Previous full stage: **63/63**. Catalog at this stage: **85 commands**.

## Constraints and Sketch solver

`sketch.constraint.create` accepts `kind` and explicitly ordered ID arrays
`points` and `geometry`. Omit unused arrays or pass empty arrays. Ordinary
constraints return `constraint`; point coincidence returns surviving `point`
because it merges topology rather than adding an equation.

| `kind` | Ordered `points` | Ordered `geometry` |
| --- | --- | --- |
| `horizontal`, `vertical` | Two points | Empty; alternatively no points and one segment |
| `coincident` | Surviving, consumed point | Empty |
| `point_reference` | Native point, reference point | Empty |
| `parallel`, `perpendicular`, `equal_length` | Empty | Reference, driven segment |
| `equal_radius`, `concentric` | Empty | Reference, driven circular geometry |
| `point_on_circle` | Point | Curve supported by the native constraint |
| `point_on_line` | Point | Segment or axis |
| `midpoint` | Point | Segment |
| `midpoint_on_line` | Empty | Segment, line reference for its midpoint |
| `symmetric` | Source, mirrored point | Axis |
| `tangent` | Optional contact point | Two curves |

For example, anchor an existing native point to the Sketch origin:

```json
{"command":"sketch.constraint.create","arguments":{"sketch":"SKETCH_ID","kind":"point_reference","points":["POINT_ID","sketch_origin"]}}
```

`sketch.get` exposes stable base origin/axes: `sketch_origin`, `sketch_axis:x`,
`sketch_axis:y`. Other references use actual stored IDs. Input order is never
inferred from position. For tangency, the native solver validates contact and
curve domains. A segment endpoint may first receive `point_on_circle`, then
tangency at the same point. Numerical coincidence alone creates no relationship.

`coincident` rewires dependencies to the surviving point and removes the other.
It cannot produce invalid/collapsed dependent geometry. Undo restores original
points/relationships. Conflicting/invalid constraint solutions are not
published; native redundancy returns `redundant_constraint`.

`sketch.constraint.delete` takes `constraint`, removes it and verifies remaining
equations. Geometry may stay in place while degrees of freedom change.

`sketch.solve` explicitly solves and commits any Sketch geometry change without
body calculation. `sketch.solve_status` evaluates a temporary copy, preserving
document, revision and cache. Both accept `iterations=100` (1–10000) and return
`status`, `remaining_degrees_of_freedom`, `maximum_residual`.
`under_constrained` is valid; mutating `solve` rejects `conflicting`/`invalid`
as `constraint_conflict` without commit. Queries may report those states.
The general protection against overwriting pending GUI editing also applies.

Integration passed **7/7** (11.69 s), `build/sketch-relation-integration-tests.log`;
GUI/CLI built from the same source. Coverage includes all fifteen relationships,
both H/V forms, independent geometry equations, freedoms, topology merging,
invalid input, Undo and native save. Catalog at this stage: **89 commands**.

## Dimensions and their properties

`sketch.dimension.create/get/set/delete` take `sketch` and optional `document`.
Create takes `kind` and ordered `points`/`geometry`; get/set/delete use returned
stable `dimension`. They share native Sketcher factories/solver, numeric
validation with Dimension Properties and the document transaction. Container
placement rules remain unchanged.

| `kind` | Ordered references |
| --- | --- |
| `distance`, `distance_x`, `distance_y` | One segment in `geometry`, or two `points` |
| `distance_x`, `distance_y` to an axis | One point and opposite base axis: X to `sketch_axis:y`, Y to `sketch_axis:x` |
| `point_line` | One point and one line/axis |
| `symmetric` | One or two points and an axis |
| `line_distance` | Reference and driven parallel lines |
| `radius`, `diameter` | One circle, arc or corner-fillet record |
| `angle` | One segment |
| `three_point_angle` | First point, vertex, second point |
| `angle_between` | Two lines; four points defining two lines; or two points and a reference line |
| `symmetric_angle`, `symmetric_line_distance` | Axis and one or two lines |
| `ellipse_major`, `ellipse_minor`, `ellipse_rotation` | One ellipse |

Lengths use **mm**, angles **degrees**; results report `unit`. References do not
require mouse selection. The native solver checks suitability, solvability and
duplicate driving dimensions.

Coordinates from the built-in Sketch origin or X/Y axes display and accept
signed absolute values, including `point_line` against a built-in axis. Repeating
`value=-30` keeps that coordinate at -30. Other distances display magnitudes;
negative input reverses their current direction. Limits use the displayed value.

### Coupled coordinate solving (2026-09-16)

The solver solves supported rectilinear dimension equations simultaneously before
its common constraint verification. H/V and coincident point groups, signed
origin/axis coordinates, axis point/line distances, aligned lengths and equal
lengths participate in the same system. This prevents a coordinate edit from
pulling one corner across its opposite corner and reversing a dimensioned
rectangle. Fixed points and drag anchors remain equations in that system.
Unsupported nonlinear/reference graphs retain the existing solver path.
Dimension signs, input conversion, labels and native serialization are unchanged.

Verification includes 112 coordinate edits across both axes, four reference
forms and reversed equation order: negative values, repeated negative input,
zero, crossing the origin and a 1000 mm translation. Each result checks all
point coordinates, dimensions, residuals and native Sketch round trips; eight
fixed-corner conflicts must reject atomically. Actual Windows GUI tests edit
both coordinates through double-click/Enter and check the saved rectangle.
Eleven related CTest contracts passed, including Sketcher, dimension commands,
Family Table, profiles, Flat, Bend and drawing-template commands.

An isolated copy of the user's `part.prtz` supplied a second rectangle fixture.
Its saved state predates the screenshot's coordinate locators, so the test adds
those locators to the copy and checks seven native command edits and saved
results. The original document hash remains unchanged. Evidence:
`build/signed-rectangle-tests.log`, `build/signed-rectangle-neighbors-tests.log`,
`build/signed-rectangle-native.log`, and `Projects/test/signed-rectangle.png`.

Create/set accept optional properties:

- `value`, `driving`, `locked`: locks protect geometry from dragging. Intentional
  numeric Properties changes remain possible while locked, as in GUI. Reference
  dimensions (`driving=false`) retain measured value, ignore `value` edits and
  cannot remain locked.
- `position=[x,y]`: original Sketch label position; `solution_side` is -1/1,
  `angle_sector` is -1/0/1 according to the native solution.
- `limits={lower,upper}`: numeric limits; `null` removes an individual limit.
- `text`: strings `prefix`, `suffix`, `text_override`, `tolerance_mode`,
  `symmetric_tolerance`, `single_tolerance`, `upper_tolerance`, `lower_tolerance`.
  Each field is limited to 2048 bytes. Modes: empty string, `symmetric`,
  `single_deviation`, `deviations`.
- `layout`: `plane_quarter_turns` 0–3, nonnegative `envelope_offset` or null,
  `text_along`, `text_outward`, `line_offset`, `radius_rotation_degrees`, boolean
  `arrows_reversed`, `radius_center_line_hidden`. Angular planes are determined
  by measured arms and forbid nonzero `plane_quarter_turns`.

Unknown fields are rejected. Value, text and layout commit atomically as one
Undo; a rejected value cannot save the valid part of a label. Label-only
changes do not affect geometry. Empty/identical edits create no revision.
`get` returns `document_layout` and `sketch_layout` separately because these are
two existing persistent layers. Command Properties saves the document layer,
as GUI Dimension Properties does outside active Sketcher.

```json
{"command":"sketch.dimension.create","arguments":{"sketch":"SKETCH_ID","kind":"radius","geometry":["CIRCLE_ID"],"value":12,"locked":true,"layout":{"text_along":3}}}
```

The body retains its last calculated state until explicit `regenerate` processes
the edited profile. Regression: a 10 × 5 mm profile extruded 2 mm retains its
100 mm³ volume until regeneration; with length changed to 20 mm it then becomes
200 mm³. Native files/templates require no format change. Catalog: **93 commands**.

All 16 dimension kinds use native factories/solver and GUI numeric validation.
The complete Windows Release suite passed **66/66** (395.03 s),
`build/sketch-dimension-full-tests.log`. After adding angular-plane rejection
and native Assembly dimension persistence, the final suite passed **6/6**
(15.83 s), `build/sketch-dimension-final-tests.log`; matching GUI/CLI build:
`build/sketch-dimension-final-build.log`. Tests also cover combined value/label
Undo, numeric locks, reference measurements, invalid input, retained body and
explicit regeneration. External references and STEP curve projection were next.

## External references and projected profiles

Four commands consume original reference data persisted during body calculation.
Projection is shared with GUI external-reference selection and calls no OCCT.
Sketch, reference and optional profile curve change in one transaction.
`sketch` and optional `document` have their usual meaning.

| Command | Arguments and result |
| --- | --- |
| `sketch.reference.create` | `kind` (`edge`, `point`, `axis`, `face`), `owner`, `key`, optional `instance_path`, `profile=false`; returns `reference`, `source_document` and profile `geometry` where applicable |
| `sketch.reference.project` | `reference`; adds our profile curve and returns `geometry` |
| `sketch.reference.delete` | `reference`; removes external linkage, preserving our profile curve and its ID |
| `sketch.reference.refresh` | Explicitly refreshes this Sketch's references; returns `broken_references` and count |

Get source identities from `reference.list/get`; find stored Sketch references
with `sketch.entities` and inspect them with `sketch.entity.get`. `owner`, `key`
and `instance_path` identify the exact original source. A displayed result-body
edge or sequential edge index cannot substitute for that identity.

```json
{"command":"sketch.reference.create","arguments":{"sketch":"SKETCH_ID","kind":"edge","owner":"SOURCE_OWNER_ID","key":"PERSISTED_EDGE_KEY","profile":true}}
```

In a Part, sources must precede the target Sketch in existing body/container
order; forward dependencies are rejected. Ownership validation uses the shared
embedded-profile list, including Helical/Sweep3D/Hole/Thread. Existing transforms
convert references into owning-body/Sketch coordinates. An Assembly root Sketch
requires the exact source occurrence path. Never infer it from a Part name or
join it with slashes; pass the reference query's path unchanged. Different
occurrences of the same Part can be referenced separately.

An Assembly root Sketch has no activated-Part path of its own. Shared validation
distinguishes this from a Part edited in Assembly context, which still requires
both paths and the owning Assembly ID. The correction also removes the same
erroneous GUI rejection. Native fields/extensions are unchanged.

An active Part in an Assembly also supports direct `sketch.reference.create`
for all four source kinds. `instance_path` is the full path from the displayed
top Assembly. The same occurrence uses the local earlier-source rule; another
occurrence of the same source Part cannot create self-dependency. The common
Assembly dependency summary commits with the Part, including Part Undo/Redo.
`sketch.reference.delete` preserves the native curve and removes the summary
dependency only after checking other Parts in the branch. GUI owned-profile
drafts follow the same rule, committing only on feature OK. Details:
[CONTEXT_REFERENCE_TRANSACTIONS.md](CONTEXT_REFERENCE_TRANSACTIONS.md).

Command projection copies only the selected edge/point/axis or selected face's
triangles from borrowed reference data, not an entire Assembly for one edge.
Exact spline knots, weights and poles project directly; coarse display points
never replace mathematical support. A planar face can supply an intersection
line; other intersections use the existing projection of stored face data.
Ambiguous sources are rejected.

`profile=true` applies only to an edge. First projection creates our curve linked
to the reference; repeating the same source projection is rejected without
change. The curve supports existing trim/offset commands. Detaching preserves
last geometry and curve identity. Native Sketcher deletion handles constraints
directly dependent on the removed reference.

`refresh` reads the document's last calculated state and does not calculate
bodies. Explicit `regenerate` refreshes the dependency chain. Missing original
sources become `broken` while retaining the last valid profile curve. Small
exact-spline movement updates support while retaining trimmed intervals and
dependent offsets. Solver conflict rejects the complete transaction. The
original stage did not pull newer open Part data into its parent Assembly;
current source-sharing rules are in
[ASSEMBLY_GEOMETRY_SHARING.md](ASSEMBLY_GEOMETRY_SHARING.md).

The initial external-reference stage left general command editing of activated
Parts for the later active-occurrence contract; the context support above was
added subsequently. Pending GUI editing/active-occurrence guards remain.
Special `reference.refresh/delete` also protect stored Part context dependencies
outside their owning Assembly. Catalog at the initial stage: **97 commands**.

The four-reference-command stage passed the complete Windows Release suite
**67/67** (389.69 s), `build/sketch-reference-full-tests.log`. After limiting
history traversal and adding a Helical profile, final coverage passed **6/6**
(18.92 s), `build/sketch-reference-final-tests.log`; matching GUI/CLI build:
`build/sketch-reference-final-build.log`. Coverage: original edges/points/axes/
faces, invalid/duplicate sources, forward dependencies, rational spline with
only two display points (circle residual below 1e-12 mm²), trimmed support and
offset after 0.01 mm movement (error below 1e-8 mm), geometry retained after
source loss/detachment, native files, two nested occurrences without loading
source files and real CLI/GUI paths. Text and remaining Sketcher edits came
next, then modeling domains from the coverage table.

## Sketch text without GUI dependencies

`sketch.text.create` requires `value` and `position=[x,y]`; `sketch.text.set`
requires stable `text`. Both take `sketch`, optional `document`, `height_mm`
(default 10), `angle_degrees` (0), `flipped` (false), `modeling_geometry` (true),
`horizontal` (`left/center/right`), `vertical` (`bottom/middle/top`) and `color`
(`green/white/yellow/red`). Omitted set fields retain their values.
`sketch.text.get` returns properties, contour/point counts and actual `bounds_mm`
without reshaping characters. Delete with `sketch.geometry.delete` and the text ID.

```json
{"command":"sketch.text.create","arguments":{"sketch":"SKETCH_ID","value":"Řez Ø10","position":[20,30],"height_mm":3,"modeling_geometry":false}}
```

GUI and CLI use one model-level contour generator. Bundled OSIFONT is embedded
in the model library at build time; no system font or running Qt process is
required. FreeType reads vector outlines, HarfBuzz handles Unicode, kerning and
character composition. Native text can be modeling geometry or annotation.
Font name remains `osifont`, as in the existing dialog.

Height is nominal font cap height. Actual glyph ink may be slightly lower or
higher: OSIFONT cap height is 1515 units, H outline height 1510. Alignment uses
actual outline bounds; line spacing uses font metrics. Both Y directions and
template mirroring preserve the GUI contract. Tests compare dimensions against
the former Qt path.

Font curves become persisted polygon contours with deviation no greater than
the smaller of **0.01 mm and 0.1% of nominal height**. The check uses Bézier
control-point distance from the segment, avoiding hundreds of unnecessarily
short faces when extruding small text. Maximum 4096 Unicode code points;
unsupported characters, invalid UTF-8, empty outlines and computational-limit
failures reject the whole change. Tabs equal four spaces. Native files retain
outlines; opening does not recreate them.

Text-to-model-profile conversion normalizes input loop orientation; the kernel
then reverses a hole loop exactly once. This fixes invalid profiles for glyphs
with holes while retaining displayed outlines and their own orientation. A
numeric test compares an extruded digit's volume to its ink area and verifies
that the hole remains empty.

Commands change only Sketch/history; body calculation remains explicit. No-op
preserves revision/cache; Undo restores exact stored contours. Catalog: **100 commands**.

## Parametric B-spline properties

`sketch.bspline.get` takes `sketch`, `geometry`, optional `document` and `limit`
(1–4096, default 256). It returns degree, closure, interpolation, construction,
`exact`, `read_only`, point count, stable point IDs, mm coordinates, knots and
weights. Above the limit, geometry arrays are omitted and
`geometry_omitted_by_limit` is set. It calculates neither body nor curve.

`sketch.bspline.set` takes `sketch`, `geometry`, optional `degree`, `closed`,
`points`. `points` replaces all `[x,y]` values in existing order without changing
control-point count; maximum 4096. Integer degree is 1–25 and less than point
count. Editing preserves curve/point IDs, knots, weights and interpolation;
it neither recreates nor converts the curve. Exact splines allow free-pole
movement but retain degree/closure. External-reference, trimmed and offset
splines remain source-driven.

GUI Properties and commands share `Sketch::edit_bspline_properties`. Supplied
coordinates are fixed solver targets during calculation; persisted fixed-point
flags remain unchanged. Conflicting constraints, movement of fixed/source-driven
points and invalid geometry reject the whole change. Multiple point edits form
one transaction/Undo. Dependent offsets update; body calculation waits for
explicit Regenerate. Unchanged parameters create no revision.

```json
{"command":"sketch.bspline.set","arguments":{"sketch":"SKETCH_ID","geometry":"SPLINE_ID","points":[[0,0],[3,10],[7,2],[10,0]]}}
```

Catalog after this stage: **102 commands**. Remaining modeling domains are listed
in the coverage overview.

An owned Section Sketch is readable through these queries. Only
`section.sketch.edit` commits its mutations: the batch uses the same native
mutation commands without nested `sketch/document` arguments and validates the
whole section path at the end. Contract/examples:
[SECTION_COMMANDS.md](SECTION_COMMANDS.md).

## Part Sketch properties (2026-09-14)

`sketch.set` and `sketch.reference.set` share Sketch Properties commit. Generic
reference assignment/removal also works for standalone containers. Dialog OK
preserves FRONT/TOP rows. A new working frame is prepared before calculation,
including offset for an owned profile. Parameters:
[SKETCH_PROPERTIES_COMMANDS.md](SKETCH_PROPERTIES_COMMANDS.md).

Both applications and all tests built; related regressions passed **12/12 in
244.41 s**, catalog **291 commands**, CTest **158 tests**. At that stage, the
approved persistent Assembly Sketch container, conversion to cuts and shared
deletion remained in the [plan](SKETCH_PROPERTIES_CLI_PLAN.md); later completed
behavior is described at the start of this document. CLI completion is assessed
in the current coverage overview, not inferred from this historical count.
