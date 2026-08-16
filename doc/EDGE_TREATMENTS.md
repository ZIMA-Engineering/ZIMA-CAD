# Edge treatments: Fillet and Chamfer

Fillet and Chamfer are two modes of one edge-treatment workflow. They share
`EdgeTreatmentPropertiesDialog`, stable edge selection, history rollback,
preview semantics, view highlighting, and dimension editing.

## Separate user-facing properties

Fillet opens Fillet Properties with a common radius. Chamfer opens Chamfer
Properties with a common symmetric distance. Neither window contains an
operation selector and an existing feature cannot be converted to the other
type. Both operations accept one or more edges, selected with Ctrl+click or
removed from the list.

A visually continuous route may contain several OCCT edges after Boolean
splits. The properties tree stores both the route and its individual members.
Removing a child removes only that edge and preserves the remaining route;
removing the parent removes the complete route. Restore Route recomputes the
continuous route from its surviving seed, or from the first remaining member
when the old seed was removed.

Creation and editing of each operation use the same shared dialog
implementation and selection workflow, configured with a fixed operation type.

## Calculation contract

- Fillet uses `BRepFilletAPI_MakeFillet` and `radius`.
- Chamfer uses `BRepFilletAPI_MakeChamfer` in its one-distance symmetric mode
  and stores `distance`.
- Both store the full `edge_refs` list and mirror its first entry in legacy
  `edge_ref`.
- All selected edges are submitted in one OCCT builder operation.
- Apply is a transient preview during creation and updates the same feature
  during editing; OK is Apply plus commit/close.
- A failed build keeps the last valid body and leaves Properties open.

The general tree and view rollback rules are defined in
[`HISTORY_EDITING.md`](HISTORY_EDITING.md).

## View interaction

The generated treatment face is never filled as a selection substitute. Hover
and selection highlight only its boundary edges. Selection survives camera
rotation and clears through the common view-selection rules. A left double
click exposes the editable radius or distance dimension. Context-menu
Properties opens the shared Edge Properties window.

Hover, left-click and RMB cycling consume the same ordered viewer candidate
list. A treatment contributes only the persisted boundary edges of its own
generated face. Generated-edge ancestry inherited by later Boolean operations
must not make the rest of the result body selectable as that treatment.

## Inspection dimensions

Treatment dimensions use a deterministic normal section derived entirely from
persisted curve descriptors. The section is calculated once when inspection or
Properties is activated and remains fixed until that interaction ends; preview
regeneration must not move it to another tessellation point.

- A complete circle has no intrinsic start. Its stable parameter zero is the
  owning container's local X axis projected into the circle plane, falling back
  to local Y or Z when parallel to the circle axis.
- An arc, line or spline uses its first persisted parametric point and tangent.
- A Fillet radius is constructed between the two circular rims of the generated
  fillet face. The stored radius and rim chord determine the section-circle
  centre; the arrow touches the arc between the rims and points toward that
  centre.
- A Chamfer dimension is rotated by 90 degrees inside the same normal section.
  Its two witness/extension lines start at stable points on the two actual rim
  circles and meet one common dimension line. The text uses compact `5x45°`
  formatting with no spaces around `x`.

This inspection is UI geometry only and must not call OCCT. All analytic curve
data required by it is produced and persisted at the explicit body-calculation
boundary.

## Extension points

The initial Chamfer is intentionally symmetric. Two-distance and
distance-plus-angle definitions can be added as modes inside the same
Chamfer properties window. They must reuse the existing stable references and
must not introduce another creation/editing dialog pair.
