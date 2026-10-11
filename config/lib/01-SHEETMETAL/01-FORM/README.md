# Sheet forming definitions

Select a native `.prtz` definition with Sheet Metal > Form. The inserted feature
owns an independent copy. Properties > Replace definition changes that copy
while retaining the feature and its placement references.

| Definition | Placement | Geometry |
| --- | --- | --- |
| `VentilationWindow.prtz` | One flat sheet face, two positioning references, angle | Outer surface with an explicit opening/cutting Sketch |
| `CornerGusset90.prtz` | Two outer flat faces adjoining one 90-degree Bend, one longitudinal positioning reference | Closed native solid; cut and Shell are derived automatically |
| `CornerGusset90-EXPERIMENTAL-unrounded.prtz` | The same two outer faces and station | FORM_CUT solid followed by a linked FORM surface, outward stock thickness; sharp transitions for manual Fillet |
| `VentilationWindow-EXPERIMENTAL-outward.prtz` | One outer sheet face and two positioning references | FORM_CUT Sketch followed by the outward surface variant |

Each definition uses the four Body names `FORM_CUT`, `FORM`, `FORM_FLAT` and
`FORM_SYMBOL`. Their names identify the roles independently of Body-list order.
Keep ordinary Part ownership, history and dependency rules when authoring them.
All required data belongs in the Part file. A Drawing is not an insertion input.

The experimental files are the user's trial candidates, not a replacement for
the retained rounded definitions. In the new corner, edit the solid in
`FORM_CUT`; `FORM` contains **Surfaces from Body**, with that preceding Body as
its explicit source and the two closing planes removed. The source solid stays
available. Do not move `FORM` before its dependency or introduce forward links.
For a planar window the cutting role remains a Sketch. Both candidate files are
selected directly through the ordinary Sheet Metal > Form command. Start manual
corner rounding at the actual FORM–Bend junction. Automatic candidate rounding
and the final production style still require the user's decision.
See [the current investigation](../../../../doc/performance/20261011-corner-outward-surface-experiment.md).

The retained corner source has closing planes on XY and XZ, a sloped-wall
profile with a 16 mm width, `16/3` mm cap radius, `20/3` mm centre offset and
12 mm depth, and a separate exact-outline symbol. Its CUT and FLAT Bodies are
intentionally empty. Changing the source profile
does not automatically edit the independent manufacturing symbol Sketch.

Retained corner insertion uses a symbolic `0.5t` wall, cavity transition radius
`t` and opposite transition radius `0.25t`, where `t` is destination thickness.
Its compact source passed the nine documented combinations, including `Ri = t`.
The experimental linked surface uses full stock thickness and has no automatic
transition Fillets. Sharp projecting junctions remain for practical review.
Both variants must fit the available sheet and Bend geometry. The spatial
form disappears in Unbend; the symbol remains and the empty FLAT role performs
no precut. See [design and verification](../../../../doc/SHEET_FORM_DESIGN.md)
for the measured cases, limits and platform status.
