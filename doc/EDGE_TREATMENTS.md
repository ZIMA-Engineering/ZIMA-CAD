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

Treatment dimensions use the persisted input-edge samples and adjacent-face
side directions at the feature's real history boundary. Properties and ordinary
double-click inspection use the same preview helper; neither needs an OCCT
calculation merely to display the dimensions.

- Fillet radius dimensions lie in the normal section of the route. Linear-radius
  Fillets expose their start and end radius separately.
- Chamfer distances retain the route endpoint and its corresponding tangent
  point as their measured witnesses. Each distance lies in its adjacent-face
  plane. Its outward direction follows the route tangent away from the selected
  start, so the shared dimension envelope offsets the dimension line beyond that
  end instead of across the solid in a common transverse section.
- Both distances in A × B mode follow that same envelope rule, including Flip.
  Distance-plus-angle retains its angular dimension in the normal section
  defined by the measured rays.

The standard envelope offset is 8 mm. The ordinary purple grips and dimension
layout controls can change it. This is a three-dimensional annotation rule;
rotating the View can still project an external dimension over the displayed
solid. The placement remains stable rather than jumping with camera rotation.
Stored layout offsets are interpreted in the corrected distance plane; existing
manually positioned Chamfer labels can therefore move when this version opens
or inspects them. Dimension identities, measured values, route side selection
and body geometry are unchanged.

## Validation of Chamfer distance planes (2026-10-01)

The Windows GUI regression covers vertical and horizontal edges and checks that
the outward direction follows the edge and that both dimension-line endpoints
clear every corner of the dimension envelope by the default 8 mm. The A × B
case also toggles Flip on and off. Existing tests exercise all five
Fillet/Chamfer modes, every applicable grip, Cancel/OK, Escape, direct value
editing, native save/reopen and Undo/Redo. The default horizontal-edge view is
captured for visual review. Log: `build/chamfer-envelope-ui.log`.

Localization review: no new or changed user-visible strings. The existing
five-language catalogs are reused.

The complete Windows GUI run passed in 60.39 seconds. The shared dimension-layout
and five-language translation contracts passed 2/2 in 11.71 seconds
(`build/chamfer-envelope-contracts.log`). No new geometry calculation or format
change was introduced. Linux validation remains deferred to the Linux host.
