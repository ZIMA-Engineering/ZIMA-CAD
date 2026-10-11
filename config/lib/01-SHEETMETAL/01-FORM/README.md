# Sheet forming definitions

Select a native `.prtz` definition with Sheet Metal > Form. This folder offers
only the two completed, current definitions selected by the user on 2026-10-11.

| Definition | Placement | Geometry |
| --- | --- | --- |
| `CornerGusset90.prtz` | Two outer flat faces adjoining one 90-degree Bend, one longitudinal positioning reference | Solid Cut, linked outward surface with full stock thickness, exact outer Bend-radius trim and both transition Fillets |
| `VentilationWindow.prtz` | One outer sheet face, two positioning references and angle | Cutting Sketch followed by an outward surface with full stock thickness |

Both definitions keep `FORM_CUT` before `FORM`, followed by `FORM_FLAT` and
`FORM_SYMBOL`. Body names identify the roles, independently of list order.
Keep ordinary Part ownership, history and one-way dependencies.

In the corner, edit the closed solid in `FORM_CUT`. `FORM` uses **Surfaces from
Body** with that preceding Body as its source and the two closing planes removed;
the source solid remains available. The inserted feature trims the outer Bend
domain and applies ordinary native Fillets at the actual FORM–Bend junction,
with radii `t` then `0.25t`, where `t` is destination thickness. Native
`form_outward` and `form_corner_finish` retain this explicit intent.

In the Window, `FORM_CUT` is the cutting Sketch. Select the outer sheet face
first: the spatial Form grows outside it. Keep the authored attachment frame
and surface orientation. Both definitions must fit the available stock and
Bend geometry; the Window's existing 4 mm thickening limitation remains.

Insertion stores an independent complete definition in the destination Part.
Updating this factory folder does not alter already inserted features or custom
libraries. Properties > Replace definition explicitly replaces a stored copy
while retaining feature identity and placement references.

In Unbend the spatial Form disappears and the manufacturing symbol remains;
the empty `FORM_FLAT` role performs no precut. Historical inward definitions
are regression inputs under `cpp/tests/fixtures/form`, outside the library
picker. Local backups and the sharp manual trial were preserved outside this
folder under `build/form-diagnostic/library-before-cleanup-1106`.

See [authoring](../../../../doc/FORM_AUTHORING_GUIDE.md),
[design](../../../../doc/SHEET_FORM_DESIGN.md) and
[measured verification](../../../../doc/performance/20261011-corner-outward-surface-experiment.md).
