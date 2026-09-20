# Modeling and Sheet Metal visual conventions

The toolbar icons are native SVG assets under `resources/icons`, using a
24 × 24 view box. Keep silhouettes simple enough to read at the application's
16 px toolbar size. The revised line icons use 1.75-unit strokes with rounded
caps and joins.

- Neutral geometry uses `currentColor`, resolved from the application's
  `WindowText` palette role for light and dark themes.
- Modeling operations use green `#80AA1A`.
- Reference geometry (Point, Axis, Plane, Sketch and 3D Curve) uses brown
  `#AD6E2E`, matching the View reference colour. Mirror uses the same brown axis.
- Sheet Metal geometry uses azure `#39C5E8`. Sheet Cut shares Sheet Blank's
  square outline and adds an internal green diagonal. Thread remains green.

Toolbar separators use native separator actions and the shared
`cpp/app/toolbar_style.hpp` stylesheet. The line is green, with a fixed
1 logical-pixel thickness and 4 logical-pixel margins; Qt handles orientation
and display scaling. Modeling has no separator between the application selector
and Selection, or immediately after Selection. Operation groups remain separated,
including the group beginning with Mirror. Do not introduce custom separator
widgets with independent colours or dimensions.

The common document Tree retains its native hierarchy guides and expand/collapse
indicators in Part, Assembly and Drawing. Feature names do not receive generated
`+` or `−` prefixes. Removing those prefixes does not change a feature's stored
add/subtract operation or its calculated geometry.

Save As offers native document formats; output formats belong under Export.
Separate application instances are opened through the operating system rather
than a duplicate New Window menu action. See [Document copy](DOCUMENT_COPY.md),
[Drawings](DRAWINGS.md) and [Multiple instances](MULTIPLE_INSTANCES.md).

For the reference-table lifetime contract during profile handle dragging, see
[C++ performance measurements](CXX_PERFORMANCE.md#profile-handle-updates-2026-09-20).
