# Sheet forming definitions

Select a native `.prtz` definition with Sheet Metal > Form. This folder offers
four completed native definitions.

| Definition | Placement | Geometry |
| --- | --- | --- |
| `CornerGusset90.prtz` | Two outer flat faces adjoining one 90-degree Bend, one longitudinal positioning reference | Solid Cut, linked outward surface with full stock thickness, exact outer Bend-radius trim and both transition Fillets |
| `VentilationWindow.prtz` | One outer sheet face, two positioning references and angle | Cutting Sketch followed by an outward surface with full stock thickness |
| `DimpledHole.prtz` | One outer sheet face | Formed hole with an enlarged opening, flat precut and two concentric construction circles as its manufacturing symbol |
| `BridgeLance.prtz` | One outer sheet face | Bridge lance with a rectangular replacement region, empty flat precut and a construction rectangle symbol |

All definitions keep `FORM_CUT` before `FORM`, followed by `FORM_FLAT` and
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
an empty `FORM_FLAT` role performs no precut. Dimpled Hole instead uses its authored Ø7.4 mm flat precut. Historical inward definitions
are regression inputs under `cpp/tests/fixtures/form`, outside the library
picker. Local backups and the sharp manual trial were preserved outside this
folder under `build/form-diagnostic/library-before-cleanup-1106`.

See [authoring](../../../../doc/FORM_AUTHORING_GUIDE.md),
[design](../../../../doc/SHEET_FORM_DESIGN.md) and
[measured verification](../../../../doc/performance/20261011-corner-outward-surface-experiment.md).

Bridge Lance uses a point from the preceding FORM_CUT Sketch as the symmetric
surface Extrusion limit. The point defines a plane perpendicular to extrusion;
the dependency follows the authored point identity rather than a copied length.
