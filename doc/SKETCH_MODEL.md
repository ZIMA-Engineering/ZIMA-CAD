# Point-based sketch model

The canonical sketch schema is version 2 and is stored in the `sketch_data`
parameter of document format 8. A sketch is a graph. Points are its only
positional state. Geometry, constraints and dimensions refer to stable point
IDs.

## Invariants

- A point owns its local `x` and `y` solver state.
- Geometry never duplicates point coordinates.
- Connected geometry shares a point ID.
- Coincidence between independent points is a constraint and does not merge
  their identities.
- Constraints and dimensions are independent entities.
- Constraints never target a segment or another curve. They contain the
  defining point IDs needed by their equation.
- Every ID is unique within the complete sketch.
- References must resolve. A referenced point cannot be removed directly.
- The solver may update points; rendered curves are derived from them.

## Geometry definitions

| Geometry | Defining points |
| --- | --- |
| Segment | start, end |
| Construction line | start, end |
| Circle | centre, circumference point |
| Three-point arc | start, point on arc, end |
| Spline | ordered interpolation points |

## Point constraints

| Constraint | Points | Equation |
| --- | --- | --- |
| Horizontal | `A, B` | `Ay = By` |
| Vertical | `A, B` | `Ax = Bx` |
| Coincident | `A, B` | `A = B` |
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

External projected geometry is a reference, not copied authoritative
geometry. Cached coordinates may be retained for display and recovery, but
must never become the source of truth while the reference is valid.

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
- When more than one mathematical solution exists, solving should prefer the
  solution closest to the previous valid sketch state. This prevents geometry
  from unexpectedly flipping or jumping between solution branches.
- Topology-changing operations such as Trim must centrally map old entity and
  point IDs to their replacements. Dimensions and constraints must either be
  transferred deliberately or removed with an explicit, predictable rule.
- External references use stable semantic `FaceRef`, `EdgeRef` and `VertexRef`
  identities for supported Box/Wedge, Extrusion and Revolve results. Their
  persisted identity does not depend on the current numerical ordering of OCCT
  topology. The implemented additive/subtractive subset also propagates
  supported ancestry with explicit missing and ambiguous states; general
  Boolean coverage remains ongoing.
- Driving state, UI locking and reference/display-only state are separate
  properties. An unlocked user dimension is still driving and immutable from
  the solver's perspective.

## Basic sketcher completion boundary

Only a small set of major user-facing capabilities remains before the basic
sketcher feature set is considered closed:

- production-ready centre/start/end arcs;
- stable spline creation and control-point editing;

Point-pair symmetry about a construction line is implemented. Trim, including
intersection splitting and dependency remapping, is deliberately deferred and
is not part of the current basic-completion target.

After these are complete, priority shifts from adding tools to reliability:

- detect and explain conflicting or redundant constraints;
- visualize under-, fully- and over-constrained states and remaining degrees
  of freedom;
- preserve the current solution branch during edits;
- extend predictable external-reference recovery across the remaining general
  Boolean-result and future feature topology;
- build regression coverage for combinations of geometry, dimensions,
  constraints and references.
