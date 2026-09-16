# ZIMA-CAD Sketcher

This document defines binding interaction, snapping, constraint and degree-of-
freedom behavior. `SKETCH_MODEL.md` describes data/equations,
`SKETCHER-TERMINOLOGY.md` terminology and `UZIVATELSKY_MANUAL.md` ordinary usage.

## Tool confirmation

Geometry entry uses one interaction contract. LMB confirms all definition
points, including the point completing an ordinary object. Short MMB never
creates/confirms geometry; it is reserved for navigation. LMB places a text
anchor in the View. Quick MMB double-click finishes the active tool and returns
to **Select**. A multipoint B-spline saves only already LMB-confirmed points;
the double-click cursor position is never added as another point.

`Enter` inside a numeric/text editor confirms only its value. It must not also
submit geometry, close Properties or invoke **OK**.

After completing geometry, a constraint, dimension or other repeatable operation,
the selected tool remains active for another input. It does not automatically
switch to **Select**. Finish with quick MMB double-click, explicit **Select** or
canceling the associated dialog.

### Dimension values and arithmetic

Ordinary point-to-point distances, including horizontal/vertical projections,
display a positive magnitude regardless of pick order. A positive edit keeps
the current direction. A negative edit reverses that direction and displays
the positive magnitude again; entering another negative value reverses it
again. Aligned segment lengths follow the same rule. Existing constraints must
permit the reversal; a rejected edit leaves the Sketch unchanged.

Coordinate dimensions measured from the Sketch origin or its X/Y axes are
the exception: their value remains signed and directly specifies the desired
coordinate. For example, changing an origin coordinate from -20 to -30 keeps
it on the negative side.

The View's double-click value editor and Dimension Properties accept numeric
expressions with `+`, `-`, `*`, `/` and parentheses. Multiplication/division take
precedence, decimal points and decimal commas are accepted, and a negative
result follows the same direction rule as a literal negative input. For example,
`20+20-40*2/2` evaluates to zero, while `-(10+5)*2` evaluates to -30. The result
still must satisfy the dimension's geometry and limits. Incomplete expressions,
division by zero and non-finite results cannot commit or silently restore an
old value. Only the numeric result is stored, not a persistent relation.

Placing a dimension keeps Dimension active for another one. Quick MMB
double-click returns to Select; double-clicking an existing dimension edits its
value. Reopening the Dimension command is not needed to finish it.
Selecting an existing dimension while Dimension is waiting for its next first
reference restores the ordinary Sketch picker and keeps that exact dimension
confirmed. Command cleanup must not clear this newer selection asynchronously.

Regression coverage: `zima_cpp_sketch_dimension_entry_ui_contract` exercises
both point-pick orders, immediate editing and MMB completion, repeated edits,
expression errors on Enter/focus-out, Properties, and native save/reload.
`zima_cpp_sketch_dimension_command_tests` additionally checks signed origin
coordinates, aligned and projected direction reversal, magnitude limits,
fixed-point rejection, Undo/Redo and arithmetic validation.

## Shared cancellation and Escape

One central stateful **Cancel** action handles `Esc` and a future toolbar button.
Tools must not implement incompatible Escape variants. It proceeds from the
smallest pending state to the whole tool:

1. First `Esc` cancels the pending point, transient geometry or active candidate,
   retaining a repeatable tool.
2. With nothing pending, next `Esc` ends the tool and returns to **Select**.
3. In **Select**, `Esc` clears hover and confirmed selection.
4. `Esc` neither closes Properties nor discards its changes; use the dialog's
   explicit **Cancel**.

A Cancel button must call the same state action. Mouse/keyboard/button handling
must share command logic. The earlier design also proposed one central Confirm
route for short MMB and Enter; the current Tool confirmation contract above
restricts them to navigation and editor-value confirmation respectively.

## Initial scale and text

An ordinary model Sketch initially zooms using the current monitor's logical
DPI, aiming for roughly 1 model mm per physical screen mm. Zoom remains
user-controlled and is not calibrated measurement. `.frmz` frames and `.tblz`
title blocks instead open fitted to the whole sheet.

Confirming a text anchor opens shared internal **Sketch Properties**. Until
**OK**/**Cancel**, the View cannot accept another text point. The multiline
editor shows about five lines and preserves line breaks. Creation/editing share
the same dialog and interaction. Default rotation is `0°`, with readable
left-to-right outlines and no implicit mirroring. Only explicit **Flip
horizontally** mirrors the text.

**Text mode** offers:

- **Ordinary text**: filled characters, retained as editable text; Protrusion,
  Revolve and Sweeps ignore it when constructing profiles.
- **Modeling geometry**: character outlines become profiles; closed outlines
  form solids while inner letter contours remain holes.

Switching mode does not explode text into lines or lose content, position or
alignment. Both modes share creation/editing, OK/Cancel and MMB double-click
confirmation. New title-block/frame text defaults to ordinary mode; model
Sketch text defaults to geometry. Existing title-block IDs such as `field1:text`
remain selectable/editable like new text.

## Core principle

Geometry entry progressively removes possibilities (degrees of freedom). Each
confirmed input constrains the next without discarding earlier confirmed
conditions unless an explicit conflict is reported.

For multipoint geometry:

1. The first confirmed point anchors the initial conditions.
2. The current point follows the cursor subject to confirmed points, inference
   and snapping.
3. Confirmation saves the selected inference as a real constraint.
4. Without inference, the point is stored free.

A Sketch position has one internal point. Confirming `C` at an existing point
reuses its stable ID, rather than creating a duplicate point and constraint.
Connected segments share one actual endpoint. `C` at this stage is preview
information; merging native points leaves no independent `C` constraint/marker.
Persistent `C` means a point lies on other geometry.

The explicit **Coincident (C)** command accepts a segment endpoint and either
Sketch X/Y axis in both orders: point then axis, or axis then point. A point
selected before starting C can also be constrained to an axis. The second pick
uses the same offered candidates for hover and click, and creates a persisted
point-on-line relation.

A reversible shared-corner radius is created by selecting two connected
segments and dragging their common point. This exact gesture takes precedence
over ordinary point dragging. The initial implementation moved only the directly
grabbed point within solver limits; later group translation is defined below.
The radius dimension drives the Sketch and is editable by double-click. Like
other Sketch dimensions it is hidden in ordinary result View, while the arc
remains part of the profile.

For drawn segments/construction lines, automatic direction inference is stored
as point-level `H/V` on the second point, not another whole-line constraint.
If both ends are already coincident with the origin or constrained to the same
axis, `C C` determines direction and redundant `H/V` is omitted. The active
second input point is always orange.

Rectangles use the same representation: edges have no geometry-level `H/V`.
Point constraints between consecutive corners store direction. First corner is
the anchor, subsequent corners are driven; the last closes against both previous
and first corners. During entry, orange circles show the emerging horizontal/
vertical arm ends from the first corner. Confirm size with LMB under the current
Tool confirmation contract; earlier descriptions used MMB.

After the first corner, RMB in empty space enters axis selection. RMB over
geometry retains normal candidate cycling. A construction line hovers orange;
LMB selects it as a blue axis. Cursor movement determines rectangle length along
that axis; LMB confirms under the shared contract. First corner remains the
reference; its counterpart and both far corners are driven. Persist two symmetry
constraints and one necessary longitudinal parallel constraint. The second
parallel and transverse perpendicular conditions follow from symmetry and are
not duplicated.

Likewise, omit `H/V` when both new segment/construction-line points have `C` on
the same horizontal/vertical segment: their support already fixes direction.

For constraints created from existing entities:

1. First selected entity is the reference.
2. Second is driven and adapts on creation.
3. Any exception must be specified by the tool, never arise accidentally from
   solver behavior.

After creation a constraint is a mathematical relationship. If a later dimension/
constraint moves the reference, its driven entity follows. Underconstrained
Sketches use the solution nearest the last valid state.

## Interactive input priorities

Evaluate candidates only inside screen tolerance. Higher priority wins; cursor
distance resolves candidates of equal priority.

1. **Coincidence**: existing point or local Sketch origin.
2. **Point on geometry**: axis, segment, arc, circle or external reference.
3. **Point alignment**: same Y (`H`) or X (`V`) as an existing point.
4. **Direction from previous point**: horizontal/vertical new geometry.
5. **Tool geometry inference**: tangent, perpendicular, parallel, intersection,
   midpoint or curve characteristic point.
6. **Free placement**.

A tool's mandatory continuity may locally change priority. For example, tangent
continuation from a circle/arc anchors the confirmed contact and constrains the
second point before ordinary H/V inference. The active candidate must be visible
in preview; hidden overrides of intent are forbidden.

RMB cycles multiple valid objects/snaps under the cursor. Tool-specific RMB
behavior runs only when there is no next candidate to select.

The same cycle is intended to include valid inference/constraint alternatives:
free point, coincidence, point-on-geometry, H/V, tangent, perpendicular or
parallel. Only mathematically valid, nonconflicting alternatives participate.
RMB changes the orange preview; LMB confirms exactly it and persists its explicit
constraint. Automation may offer/order candidates but cannot create a different
hidden constraint after confirmation. Short MMB follows the navigation contract.

## Point degrees of freedom

A free point has two movement freedoms: X and Y.

- Horizontal point alignment sets driven Y from reference Y, removing one freedom.
- Vertical alignment sets driven X from reference X, removing one freedom.
- Coincidence fixes both X/Y, removing both.
- Point-on-line/curve usually leaves one motion parameter.
- A coordinate dimension fixes its corresponding coordinate.
- Another independent condition can fix the last freedom.

Do not add redundant constraints twice. Reject conflicting new constraints with
a clear message. Never silently remove an older constraint to resolve a new conflict.

## Horizontal and vertical constraints

One command supports geometry and points.

### Segment or construction line

Select one entity. Its first defining point stays fixed during creation while
the second aligns: `H` gives equal Y; `V` equal X. The marker is at geometry midpoint.

### Two points

Select reference point, then driven point. `H` copies reference Y, `V` copies X.
The marker appears at the second/driven point. Selecting it highlights both
points; deleting the constraint deletes neither point.

Selecting a geometry-level `H/V` marker highlights its defining endpoints, not
the whole segment. Persistent `C` highlights point and support geometry; merged
native points have no remaining `C`. `T` highlights both tangent geometries.
Fixed points use `F`; `K` is reserved for an exact generated curve characteristic
point. A point-level `H/V` marker highlights only its reference/driven points.
During drawing, `C` with point `H/V` takes precedence over automatic perpendicular/
parallel inference. Preview `H/V` appears at the current second point. Tangent
segments meeting circles/arcs/ellipses/elliptic arcs show `C + T` at ordinary
contacts; only an exact confirmed quadrant contact uses `K + T`.

Selecting point-on-axis `C` highlights point and axis in normal cyan. Other
reference supports use the same rule. When drawing perpendicular from existing
geometry, preview `⊥` belongs to the first confirmed contact. Snapping the second
point must not move that marker to the new segment's end.

## Other constraints

- **Coincident**: merges two native points into one stable topology point without
  stored `C`. After a point, choosing an axis/segment/curve creates persistent
  point-on-geometry `C`. Main X/Y axes may also be selected first as reference,
  then the driven point.
- **Parallel**: first line is reference; second rotates parallel.
- **Perpendicular**: first line is reference; second rotates perpendicular.
- **Equal**: first length/radius is reference; second adopts it. `=` appears only
  at the driven child; reference highlighting appears when selecting the relation.
- **Midpoint**: driven point first, reference segment second. This explicit
  exception lets the second entity's type determine the operation.
- **Symmetric**: two points, then symmetry axis; the points form one driven pair.
- **Tangent**: ordering/fixed contact depend on the supported curve pair and are
  shown with `T` in preview.
- **Concentric**: first circle/arc is reference; second adopts its center.

## Common tangent segment

**Common Tangent** creates geometry, not merely an additional constraint. It
accepts circles, circular arcs, ellipses, elliptic arcs and B-splines. Segments,
points, axes, external references and other Sketches' curves are not offered.

1. Click the first curve near the desired contact.
2. Click the second curve near its desired contact.
3. Both click locations select the initial solution branch (upper/lower,
   left/right, external/internal tangent).
4. The solver creates a normal profile segment with both endpoints on their
   curves and tangency at both ends.

The segment, two contact points and four relationships remain in the persisted
ZIMA Sketch and solve again after source changes. It is not an unassociated
calculated line. Calculation uses only analytic/persisted Sketch geometry;
hover, selection and creation call no OCCT.

Arcs/open B-splines restrict contact to their parameter domains. A missing,
degenerate or conflicting local tangent rejects the entire operation without
extra points, segments or constraints. First click is transient; Escape cancels
it. The second valid click saves everything in one reversible revision.

## Selection, dragging and transient display

Rectangle selection stores one set of points, lines, curves and text. Tree only
mirrors that set and must not shrink it while selecting individual rows.
`Delete` removes the full selection in one revision, safely removing orphan
points and related constraints.

Point/dimension dragging operates on a transient document copy. Preview shows
only the active Sketch and the same passive model context as ordinary Sketcher;
other Part Sketches must not temporarily appear while the button is held.

Point dragging respects geometric meaning. Circle/arc centers translate the
curve without changing radius. An arc endpoint is a radial handle: distance
changes radius, direction changes arc range. A locked radius permits only
angular endpoint motion; an unlocked driving radius adopts the dragged value.

The same rigid center translation applies during constraint creation/topology
merging, not just dragging. Moving a center to an axis/segment end also moves
arc endpoints and dependent control/contact points. A fixed/externally anchored
dependency rejects the operation transactionally.

With multiple selected points/geometries, starting a drag on a selected point
translates the entire selection from its original state by one `ΔX, ΔY`.
Internal lengths, angles, radii and relationships remain. The grabbed point is
only a handle. Fixed/externally driven points and constraints into unselected
anchored geometry restrict movement; they must never be silently detached.

## Multistep curves

Arc, ellipse, elliptic arc and both spline tools keep all previously confirmed
input points visible through subsequent steps. Ordinary geometry snapping offers
`C`; exact quadrant points of circles/arcs/ellipses offer `K`. Confirmed offers
persist as real constraints; preview cannot show a relationship that disappears
on completion.

Two tools share the same stable point model. **B-spline – control points** uses
confirmed points as control vertices; **Interpolating spline** passes through
all confirmed points. First confirmation shows a point, second a polyline
preview, third onward the selected spline's actual preview. Both require at
least three points; quick MMB double-click finishes at the last confirmed point.
Short MMB remains navigation. A three-point spline remains a spline and does
not offer a shared circular radius.

Adding endpoint `T` to an open B-spline preserves the contact point and attached
segment while adjusting the neighboring spline control point. Tangency uses
the exact endpoint derivative, not a sampled display polyline. `C` and `T`
remain separately visible and removable.

A tangent arc in **Polyline** displays its derived center and shared start.
Bringing its center near main X/Y offers `M`; confirmation constrains the center
to that axis. The endpoint also uses ordinary `K` and point `H/V` alignment.
Exact `K` takes precedence over derived center-to-axis snapping; confirmed
`H/V` persists as a real point constraint.

Switching Polyline from arc back to segment retains the shared arc endpoint.
With automatic tangency active, continuation offers `C T`, highlights the
supporting arc and saves tangency on confirmation. Preview/confirmation share
inference. The arc center is separate; no duplicate point is created at the
segment/arc junction.

Text placement is a drawing tool, so rectangle selection must not consume empty
View clicks. LMB sets the anchor and immediately shows a transient outline from
internal Properties. OK saves; Cancel leaves the Sketch unchanged.

## Automatic dimension

One **Automatic Dimension** command uses the first two confirmed points as a
length reference; a segment/axis supplies its two defining points. Cursor
position offers aligned, horizontal or vertical variants. Clicking empty View
confirms placement; selecting another offered segment, axis or points continues
the same command toward an angular dimension.

Vertically aligned points offer vertical distance and horizontally aligned
points offer horizontal distance, even beyond segment endpoints. Never choose
a zero perpendicular projection accidentally. Redundant/conflicting dimension
rejection displays a message and retains pending input; solver exceptions must
not terminate the application.

Owned Sweep Sketches use the same workflow. The 2D Sweep GUI regression draws
segment–arc–segment from the path origin, switches directly to dimensioning the
first segment and confirms in empty space. Reentry also verifies dimensioning
a segment and its point pair, with shared endpoints and no extra points.

Angular dimensions require two complete directions (four points). Two segments,
two axes or their combination are shortcuts for those same points. The second
reference stays orange until confirmation. Once both directions exist, arc and
text follow the cursor; the final click sets sector, sign, radius and actual
stored placement. Dragging a saved dimension point must not switch sector.

Value edits are transactional. The solver retains the angular branch and uses
remaining freedoms in connected geometry. Example: two segments, first length
from a fixed origin, an angle between them, and the second's far endpoint on an
axis. Changing the first length moves the common point and solves the second
segment's new axis intersection without changing angle. Duplicate/already-driven
dimensions are rejected without changing the Sketch.

## Solver regression scenarios

These form an ongoing verification matrix. Basic variants are covered and must
remain covered as the solver expands:

1. **Angular dimensions**: extend driving, locked/reference, negative-value and
   deletion cases in more complex constrained chains.
2. **Connected curves**: cover segment–arc, arc–segment, elliptic arc–segment and
   segment–B-spline mobility with separate combinations of `C`, `T`, `H/V`, fixed
   point and driving dimension. Each drag needs an `A -> B -> A` test. Forward
   dragging a free segment endpoint tangent to a circular-arc endpoint is
   verified, but return along the same branch may still resist and remains an
   open issue. Direct arc-endpoint dragging works and is not that issue.
3. **Centers on axes**: circle/arc center on main axis, both axis/point selection
   orders, rigid arc endpoint transport and center merge with segment endpoint.
   Propagate into the free branch without pulling the active point away.
4. **Curve parameters**: simultaneous circular/elliptic radius and rotation,
   polygon size/rotation and B-spline end-arm edits without losing contact.
5. Exercise each scenario in C++ GUI and turn click order, displayed inference
   and persisted constraints into a regression.

During second-point entry for segments/construction lines, existing segment
lengths are offered within screen tolerance. A candidate snaps to equal length,
shows `=`, highlights the reference segment orange and saves a real equal-length
constraint. It does not override `C`, point `H/V` or mandatory tangent continuity.

Universal Dimension accepts symmetric length via **point–axis–point** or
**axis–point–point**. After point+axis, an editable perpendicular-distance preview
appears. Selecting the same point third creates a diametral symmetric dimension
across the axis; another point drives both on opposite sides by half the total
value. The axis may be a base Sketch axis or construction line. Symmetric point,
line and angular dimensions are driving solver equations, participating in
freedom, redundancy, numeric edits and geometry dragging, not just annotations.

If the dimensioned point is also an intersection on a supporting segment, the
solver first propagates the change into that segment, then solves other
constraints. If symmetric length drives a slanted wall endpoint and symmetric
angle drives the wall, the endpoint remains anchored while the free wall end
rotates. Both dimensions therefore coexist in a Revolve profile.

Equal length is also offered during H/V entry; preview shows `=` and `H/V` and
confirmation saves both independent conditions.

A new segment's second point may snap as the mirror of its first across a
construction line. Preview highlights the axis and shows `S` with `⊥`, or
`S + H/V` when their connector is horizontal/vertical. Confirmation saves the
actual symmetric point-pair constraint. Separate perpendicular/H/V constraints
are omitted as consequences of symmetry.

## Markers and selection

A constraint marker belongs to its driven entity. Hover highlights orange;
selection uses the normal cyan/blue selection state. Marker selection exposes
all participants. Deleting a marker removes only its constraint. Basic markers:
`H`, `V`, `C`, `K`, `M`, `T`, `F`, `=`, `S`, `∥`, `⊥`. `C` denotes arbitrary
point-on-geometry position; `K` only an exact generated characteristic point.
Curve type alone never turns `C` into `K`.

There is no fixed cap on simultaneous preview markers. Point/relation markers
at one point share ordering, horizontal slots and a vertical baseline, avoiding
overlap for `C + H`, `C + T`, `= + H` or `S + V`. Equal-length `=` belongs to
geometry and appears at new-segment midpoint; concurrent `H/V` stays at the
second point.

## Conflicts and exceptions

Reference–driven ordering exceptions require a tool gesture or an already fully
constrained participant. The tool must announce or clearly preview them; silent
reversal is forbidden. The long-term aim is explicit persisted reference/driven
roles for solving and dependency presentation.

## Spline closure, participants and axis sliding (2026-09-06)

During spline entry the final point can snap to the first, sharing one endpoint
identity. **Tangent** supports two splines sharing an endpoint created through
C, or selecting one spline twice for tangency at its shared start/end. This is
Sketcher-only, not periodic 3D spline functionality. Tangent self-closure needs
enough control points for independent end arms. Preview does not add another
coincident point when the cursor remains on the just-confirmed point (e.g. an axis).

Selecting a dimension/constraint marker in View or Tree highlights participants
from stored Sketch references. A length dimension created from an ordinary
segment uses endpoints A/B. A visibly fillet-shortened segment also retains its
segment reference to measure actual tangent ends.

A locked connector length between a segment midpoint and a point on an axis does
not fully fix the axis point. Dragging the first segment's free end recomputes
the midpoint; the connector endpoint can slide along the axis. The length solver
intersects the supporting line with the circle defined by the length and chooses
the nearer solution. Unreachable positions are rejected atomically. Locked
lengths stay fixed; unlocked dimensions follow achieved geometry.

`zima_cpp_sketcher_contract_tests` constructs this mechanism and loads the reported
`02.prtz` Sketch fixture `cpp/tests/fixtures/midpoint_axis_locked_rod.json`.
It checks several reachable drags, retained constraints/locked length and rejection
of unreachable placement.

### Verification status

After this fix, Sketcher, general document and 3D Curve/Sweep tests plus
`--verify-startup` passed. The last standalone window test failed at deferred
thread-catalog opening (`Deferred thread catalog did not open after the pointer
gesture`); this fix does not resolve it. A previously reported temporary dimension
selection blockage, cleared by starting Dimension again, still has no confirmed
cause and must not be reported fixed.

### Circular external references

Equal Radius and Concentric accept external circles/arcs. Sources may be edges
or a single circular outline/section of an external face. External geometry stays
read-only; the native Sketch circle/arc changes radius or center respectively.

Center/radius recognition checks all persisted reference points without OCCT,
independent of uniform sampling. Ellipses, invalid references and multiple face
contours are not treated as one circle. Coplanar faces provide finite outlines;
noncoplanar faces retain existing intersection/section behavior. Save/reopen
preserves constraints/source identity; reference changes update the relation.

2D Sweep/Helix Sketches can open from the tree with unresolved preceding geometry.
If their frame cannot currently be derived, the editor uses the saved plane and
reports missing dependencies in the status bar. Opening calls no OCCT and does
not commit invalid solids; Cancel retains history. Regression checks all two/
three Sketches even without a calculated preceding body.

### Automatic tangency and common circle tangent (2026-09-07)

A common tangent segment between circles stores C + T at both ends. Significant
points select branches; an extra K does not lock contact to a quadrant. Preview
shows C + T at both contacts. If another automatic tangent relation is already
implied, redundancy does not cancel segment creation. Conflicting/invalid
constraints are not ignored. Manual constraints still report redundancy.

Two-circle common tangents use exact geometry candidates selected by both click
positions, including equal circles with horizontally aligned centers and circles
with fixed centers/radii.

### Text anchor

Title-block text highlights its anchor orange on hover and cyan on confirmation.
The anchor remains a separate Sketch point; highlighting does not add another
selected object. Bottom/Middle/Top alignment uses actual glyph bounds including
diacritics and multiple lines. Template opening derives outlines from stored
values, anchors and alignment settings.

### Dragging with locked dimensions

The cursor projects onto permitted motion when locked H/V distance fixes position
relative to origin, fixed reference or axis. Locked rectangle width permits
height changes and vice versa. The same applies to X/Y dimensions, negative
coordinates and propagation through H/V/coincidence. Unlocked driving dimensions
follow the achieved shape; locked values remain fixed. Fully locked points do
not move.

## Construction geometry

Segments, circles, circular/elliptic arcs, ellipses and B-splines can switch via
View/Tree context actions **Convert to construction geometry** and **Convert to
profile outline**. All construction curves use chain lines, including hover/
confirmed selection.

The role changes while identity, control points, dimensions, ranges, constraints
and dimensions remain. Construction curves are excluded from solid profiles.
Segments remain finite and arcs retain ends. Infinite axes are separate;
chain-line styling does not turn a segment into an axis.

### Template text and dimensions in Properties (2026-09-09)

**Flip horizontally** describes visible mirroring in current Sketch coordinates.
Normal title-block/frame text has it off, as ordinary Sketch text does. Existing
template orientation/contours remain; differing template axis direction is
converted only while reading/confirming the dialog.

Double-click dimension edits affect the current working Sketch, including 2D
Sweep profile/path, Helical, Sweep/Loft and Section Sketches. Locks still protect
dragging, while intentional numeric edits remain possible. Solid calculation
waits for whole-container confirmation.

Container Properties also shows owned-Sketch dimensions. Double-click edits them
with Sketch, Extrusion, Revolution, 2D Sweep, Helical and Sweep/Loft Properties
open. Preview uses the pending Sketch, including repeated edits and return via
Sketch. OK commits/calculates the whole container; Cancel discards pending edits.
Saved profile/path dimensions also appear when inspecting container parameters
in the View.

Arc-radius changes respect tangent segments whose other endpoint belongs to
another arc. Contact may slide on its circle; when fixed, solve the tangent's
intersection with the opposite circle. Endpoints remain on their arcs, constraints
persist and unsolvable edits reject without changing the input Sketch. Locked
dimensions still allow deliberate numeric edits.

### First plane and working profile (2026-09-10)

The first plane placement reference initially determined Extrusion/Revolution's
Sketch plane; other references supplement position/orientation. The later
selectable work-plane contract retains this automatic default and permits an
explicit override; see [WORK_PLANES.md](WORK_PLANES.md). Front/Back, rotation and
profile offset must immediately agree in preview, dimensions, Sketch editor
and calculated body.

A new feature also enters the active body's temporary preview history so its
owned Sketch uses normal reference solving. The working Sketch inherits that
preview's solved plane/origin/axes; local 2D geometry is not rewritten on
orientation changes.

SKETCH entry/return retains the entire pending feature, including references,
orientation, offset and length/angle, also for existing calculated features.
OK commits the draft and calculates the body; Cancel preserves original Sketch/
parameters when editing.

`zima_cpp_profile_frame_ui_contract` covers first planes XY/XZ/YZ, eight side/
rotation combinations, offsets, Sketcher return, cancellation and agreement of
saved Sketch/calculated-body bounds after OK. The audit also checks first planes
for standalone Sketch, Helical base circle and 2D Sweep path. For 2D Sweep the
first reference prefills the independently editable path plane. Sweep/Sweep-Loft
sections remain derived from the path tangent at the chosen point. The fix uses
the existing container-placement solver without changing its rules.

### Immediate trim preview and constraints (2026-09-09)

Every Trim click or completed drag updates the View immediately in standalone
Sketches, profile drafts, templates and nested Parts. Rendering/selection use
the same pending geometry. Escape restores pre-command state; completing the
Sketch includes the current trim.

Trim transfers tangency to the curve piece containing the original contact,
even when both owners split in one stroke. Retained endpoints/centers keep IDs
and point constraints. Removed contact removes its tangency. A junction now
represented by one shared endpoint remains topology without duplicate incidence.

Trim also handles segments attached through circle/ellipse quadrant **K**.
On retained arcs this becomes **C** or one shared endpoint, never a reference
to a deleted circle. Moving an arc endpoint does not independently overwrite
another arc's point connected through a segment; constraints solve its motion.
With equal radii, the grabbed arc drives changes regardless of original equal
selection order. Tangency can be added at existing C/K junctions after segment
direction changes; the stored junction determines contact, not a tangent point
for the old segment direction.

For equal arcs connected by common tangents, contact dragging respects the
constrained center connector. With fixed center, radial drag changes radius;
with a center sliding along the connector, longitudinal drag also moves it.
Small cursor error into a forbidden direction must not block permitted motion.
A driving radius/diameter dimension may control either side after equality.
When adding equality, the first selected curve still supplies initial radius;
a conflicting second-curve dimension is rejected transactionally.

### Directional dimensions for points on axes (2026-09-09)

A point constrained to a line remains movable along it. Editing X/Y does not
treat point-on-line as fixing both coordinates. This permits changing center
spacing in a profile with arcs, tangent arms and concentric holes, one center
at origin and another on X. Radii, equality and tangency remain; tests cover
original spacing 19.448732 mm and repeated changes to 12, 20 and 35 mm. Tangent
contacts retain their own anchoring rules during segment-length edits.
