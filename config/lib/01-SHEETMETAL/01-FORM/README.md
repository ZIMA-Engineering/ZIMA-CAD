# Sheet forming definitions

Select a native `.prtz` definition with Sheet Metal > Form. The inserted feature
owns an independent copy. Properties > Replace definition changes that copy
while retaining the feature and its placement references.

| Definition | Placement | Geometry |
| --- | --- | --- |
| `VentilationWindow.prtz` | One flat sheet face, two positioning references, angle | Outer surface with an explicit opening/cutting Sketch |
| `CornerGusset90.prtz` | Two outer flat faces adjoining one 90-degree Bend, one longitudinal positioning reference | Closed native solid; cut and Shell are derived automatically |

Each definition uses the four Body names `FORM_CUT`, `FORM`, `FORM_FLAT` and
`FORM_SYMBOL`. Their names identify the roles independently of Body-list order.
Keep ordinary Part ownership, history and dependency rules when authoring them.
All required data belongs in the Part file. A Drawing is not an insertion input.

The corner source has closing planes on XY and XZ, a sloped-wall profile with a
20 mm width, R8 cap and 10 mm centre offset, and a separate exact-outline symbol.
Its CUT and FLAT Bodies are intentionally empty. Changing the source profile
does not automatically edit the independent manufacturing symbol Sketch.

Corner insertion uses the destination thickness with inner transition radius
`t` and outer radius `2t`. It must fit the available sheet and Bend geometry.
The tested `Ri = t` combinations do not fit this fixed-radius construction and
report an error; the program does not substitute smaller radii. The spatial
form disappears in Unbend; the symbol remains and the empty FLAT role performs
no precut. See [design and verification](../../../../doc/SHEET_FORM_DESIGN.md)
for the measured cases, limits and platform status.
