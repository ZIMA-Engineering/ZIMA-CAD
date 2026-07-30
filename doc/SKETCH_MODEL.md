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

External projected geometry is a reference, not copied authoritative
geometry. Cached coordinates may be retained for display and recovery, but
must never become the source of truth while the reference is valid.

## Editor boundary

The current editor temporarily uses a flat mutable projection while a command
is in progress. `SketchModel.to_editor_data` and
`SketchModel.from_editor_data` isolate that representation from persistence.
Project files store only the canonical version 2 object. Old sketch payloads
are intentionally not supported.
