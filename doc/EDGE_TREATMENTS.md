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

## Extension points

The initial Chamfer is intentionally symmetric. Two-distance and
distance-plus-angle definitions can be added as modes inside the same
Chamfer properties window. They must reuse the existing stable references and
must not introduce another creation/editing dialog pair.
