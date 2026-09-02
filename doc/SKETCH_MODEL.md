# Point-based sketch model

The canonical sketch schema is version 2 and is stored in the `sketch_data`
parameter of document format 8. A sketch is a graph. Points are its only
positional state. Geometry, constraints and dimensions refer to stable point
IDs.

## Invariants

- A point owns its local `x` and `y` solver state.
- Geometry never duplicates point coordinates.
- Connected geometry shares a point ID.
- Point-to-point C is a topology edit: all dependent references are rewired to
  one surviving point ID and the absorbed point is removed. It is not stored
  as a solver constraint and has no C marker.
- If that edit merges both endpoints of one Segment, the zero-length Segment
  and its owned relations disappear and the single merged point remains.
- A visible persisted `C` constraint means one native point lies on another
  geometry, straight or curved, at an arbitrary position. The support geometry
  does not acquire a second point at that position.
- Anchoring a native point to Sketch Origin, an external point or a generated
  curve keypoint is a point-reference equation. Origin and external-point
  references are hidden. A generated native curve keypoint is displayed as
  `K`, because it is the exact persisted form of point-on-curve placement.
  None of these references transfer point ownership across the boundary.
- `C`, `K` and `T` describe different facts and may coexist. A generic tangent
  contact on a curve is `C + T`; an exact tangent contact at a generated
  quarter keypoint is `K + T`. `K` must never be inferred merely because the
  support is curved.
- Constraints and dimensions are independent entities.
- Point-pair constraints contain defining point IDs directly. Relations whose
  meaning requires a support geometry additionally retain that geometry's
  stable ID; the support remains separate from the point node.
- Every ID is unique within the complete sketch.
- References must resolve. A referenced point cannot be removed directly.
- The solver may update points; rendered curves are derived from them.
- A curve centre is a positional root for its complete dependent point
  closure. A constraint, point reference or point merge that relocates the
  centre translates that closure rigidly by the same delta. Circular radius,
  arc domain and ellipse axes must not change as a side effect of centre
  placement.
- Creating or editing a driving dimension is transactional. The candidate
  state is solved and rank-checked before it replaces the committed sketch;
  conflict or redundancy leaves points, constraints and dimensions unchanged.
- Angular rank equations use a locally signed `atan2(cross, dot)` residual.
  Unlike `acos(dot)`, it retains a usable derivative at 0 degrees, so a 0°
  angle is correctly recognized as redundant with an existing Horizontal
  relation (and equivalently at the vertical-axis cases).
- A persisted angular sector is presentation state, not by itself the solver's
  directed-line branch. For a supplementary display the solver keeps whichever
  of `alpha` and `180-alpha` matches the current persisted directions. This
  prevents editing an unrelated length from flipping the angular equation.
- Point-to-line and line-to-line distances persist a non-negative magnitude
  and a separate normal-side branch. Entering a negative value flips the
  branch and stores the absolute magnitude. Genuine coordinate dimensions
  against Sketch axes remain signed.

## Geometry definitions

| Geometry | Defining points |
| --- | --- |
| Segment | start, end |
| Construction line | start, end |
| Circle | centre point + scalar radius |
| Circular arc | centre, start, end + synchronized scalar radius/domain |
| Spline | ordered interpolation points |

## Point constraints

| Constraint | Points | Equation |
| --- | --- | --- |
| Horizontal | `A, B` | `Ay = By` |
| Vertical | `A, B` | `Ax = Bx` |
| Point on geometry (C) | `A, geometry` | `A lies on geometry` |
| Exact curve keypoint (K) | `A, curve keypoint` | `A = generated keypoint` |
| External point reference | `A, reference` | `A = reference` (hidden) |
| Perpendicular | `A, B, C` | `(A-B) · (C-B) = 0` |
| Parallel | `A, B, C, D` | `(B-A) × (D-C) = 0` |

For perpendicularity, `B` is the shared vertex. Segments `AB` and `BC` may
make the relation selectable and visible, but they are not operands of the
constraint.

The interaction convention is reference first, driven element second. For
two-point horizontal and vertical constraints the stored owner is the driven
point and `point_id` identifies the reference point. Relational constraints
may retain `relation_role: driven` as editor metadata. The complete selection,
snapping-priority and exception rules are documented in [SKETCHER.md](SKETCHER.md).

Coincident/Shodnost has two data-model outcomes. Two native points are merged
into one topology node and no constraint record remains. A native point placed
on an axis, Segment or curve keeps an explicit point-on-geometry relation. The
base X/Y axis may be selected before the point or after it; both gestures
produce the same point-on-axis equation.

External projected geometry is a reference, not copied authoritative
geometry. Cached coordinates may be retained for display and recovery, but
must never become the source of truth while the reference is valid.

## Common tangent construction

The Common Tangent command consumes two persisted Sketch curve IDs and one
local-space click hint for each curve. Supported inputs are circles, circular
arcs, ellipses, elliptical arcs and B-splines. The hints select the local
solution branch; they are command input and are not persisted as topology or
as a replacement for the resulting constraints.

The command projects both hints to their curves, refines the two contact
locations until the connecting chord is tangent at both ends, and commits:

- one ordinary Segment with stable point and geometry IDs;
- one point-on-curve constraint for each endpoint;
- one Tangent constraint between the Segment and each source curve.

All records are created transactionally. An absent, out-of-domain,
degenerate, non-convergent or conflicting branch leaves the Sketch unchanged.
The implementation uses the persisted ZIMA curve model and never OCCT. This
also means later edits are solved from the four stored relations, not from a
cached line calculated at creation time.

Viewer hover, both confirmations and right-click candidate cycling consume the
shared ordered candidate list filtered to supported curves owned by the active
Sketch. Curves from sibling Sketches are not valid command inputs.

## Editor boundary

The current editor temporarily uses a flat mutable projection while a command
is in progress. `SketchModel.to_editor_data` and
`SketchModel.from_editor_data` isolate that representation from persistence.
Project files store only the canonical version 2 object. Old sketch payloads
are intentionally not supported.

## Agreed development direction

The current sketcher architecture remains the foundation; it is not scheduled
for replacement. Geometry, constraints, dimensions, solving and presentation
must remain separate concerns. Further work should consolidate this design
instead of adding isolated special-case corrections for individual tools.

The following principles guide that consolidation:

- Constraints should converge on one collection of independently identified
  records instead of being distributed through geometry entities.
- The solver should express constraints and driving dimensions as a unified
  system of equations and residuals, rather than successively modifying
  geometry with tool-specific rules.
- An explicit Tangent contact is the authoritative shared point. Radius edits
  move that point radially and realign the free tangent arm; length edits move
  the less-supported endpoint; dragging a tangent arm solves the nearest valid
  contact branch on the circular support. This includes a Segment whose contact
  is the shared start/end point of an Arc. The cursor-driven free Segment end
  stays authoritative while the contact slides within the persisted Arc
  domain. The same support priority is used by ordinary and X/Y-aligned length
  dimensions.
- Direct manipulation of a Circle `C+T` contact is a radial-handle contract.
  With an unlocked Radius/Diameter the Circle centre stays fixed, the contact
  follows the cursor, the radius changes and the attached tangent Segment
  preserves its previous length and tangent orientation. A locked radial
  dimension changes the gesture to an angular slide around the existing rim.
- A driving Angle on a Segment with one Circle `C+T` contact preserves the
  Circle centre, Circle radius and Segment length. It moves the contact to the
  corresponding tangent point and then lets the ordinary solver validate all
  remaining relations. A two-support common tangent is not reduced to this
  one-contact rule.
- Tangent dragging is not considered complete after a one-way move. Its
  regression contract is a round trip `A -> B -> A`: the solver must retain the
  same intended contact branch and restore the original point/curve state
  without drift or hysteresis. The current Segment-to-Arc endpoint path passes
  the forward move but can resist the return move; that reverse-continuity case
  remains open. Direct radial dragging of the Arc endpoint itself is verified
  separately and is not part of this defect.
- When more than one mathematical solution exists, solving should prefer the
  solution closest to the previous valid sketch state. This prevents geometry
  from unexpectedly flipping or jumping between solution branches.
- Topology-changing operations such as Trim must centrally map old entity and
  point IDs to their replacements. Dimensions and constraints must either be
  transferred deliberately or removed with an explicit, predictable rule.
- Trim preserves one native contact point. For example, trimming a tangent
  Segment at a Circle makes that point the Segment endpoint while its C
  relation to the Circle and the T relation remain. If the trimmed curve is
  reconstructed as an Arc ending at the same point, Segment and Arc share the
  point ID and the now-intrinsic C relation to that Arc is removed.
- External references use stable semantic `FaceRef`, `EdgeRef` and `VertexRef`
  identities for supported Box/Wedge, Extrusion and Revolve results. Their
  persisted identity does not depend on the current numerical ordering of OCCT
  topology. The implemented additive/subtractive subset also propagates
  supported ancestry with explicit missing and ambiguous states; general
  Boolean coverage remains ongoing.
- Driving state, UI locking and reference/display-only state are separate
  properties. Numeric solving enforces every driving dimension. Direct
  manipulation may deliberately update an unlocked driving value to the
  measurement reached by the drag; a locked value remains a hard equation.

## Current reliability boundary

Centre/start/end Arcs, both B-spline creation modes, point-pair symmetry and
Trim with dependency remapping are implemented on the canonical point model.
They remain subject to combined-interaction regression testing; they are no
longer architectural placeholders or deferred alternate representations.

Current priority is reliability rather than another parallel solver path:

- detect and explain conflicting or redundant constraints;
- visualize under-, fully- and over-constrained states and remaining degrees
  of freedom;
- preserve the current solution branch during edits;
- extend predictable external-reference recovery across the remaining general
  Boolean-result and future feature topology;
- build regression coverage for combinations of geometry, dimensions,
  constraints and references.
