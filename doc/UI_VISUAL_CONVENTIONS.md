# Modeling and Sheet Metal visual conventions

The toolbar icons are native SVG assets under `resources/icons`, using a
24 × 24 view box. Keep silhouettes simple enough to read at the application's
16 px toolbar size. The revised line icons use 1.75-unit strokes with rounded
caps and joins.

The workspace applies the saved application font before constructing its actions
and widgets, both on cold startup and when recreating the window after a language
change. Language changes preserve the selected ISO/system font and its size.

- Neutral geometry uses `currentColor`, resolved from the application's
  `WindowText` palette role for light and dark themes.
- Modeling operations use green `#80AA1A`.
- Reference geometry (Point, Axis, Plane, Sketch and 3D Curve) uses brown
  `#AD6E2E`, matching the View reference colour. Mirror uses the same brown axis.
  The displayed document's main Origin point stays black. Construction Point
  markers and all feature, Body and occurrence Origin points use the same brown
  as planes. Marker sizes and hover/confirmation colours are unchanged.
- Sheet Metal geometry uses azure `#39C5E8`. Sheet Cut shares Sheet Blank's
  square outline and adds an internal green diagonal. Thread remains green.
- Unbend reuses the azure Sheet Profile silhouette with a green horizontal
  arrow pointing at its vertical wall. Bend Back uses an azure horizontal
  segment and a green curved arrow pointing upward and left.

Toolbar separators use native separator actions and the shared
`cpp/app/toolbar_style.hpp` stylesheet. The line is green, with a fixed
1 logical-pixel thickness and 4 logical-pixel margins; Qt handles orientation
and display scaling. Modeling has no separator between the application selector
and Selection, or immediately after Selection. Operation groups remain separated,
including the group beginning with Mirror. Do not introduce custom separator
widgets with independent colours or dimensions.

Ordinary View Sketch and 3D Curve strokes are brown (`#AD6E2E`), one logical
pixel wide. Properties and Sketcher use white strokes at the existing editing
width; construction geometry and external references retain their own styles.
Hover remains orange and confirmation cyan, including Sketch points when the
owning history container is selected. Finish Sketch uses the green check icon.

Property windows without an explicit initial size open at their layout's safe
minimum width, retaining the natural height and existing control sizes. They
are compacted after queued numeric-field sizing has settled, so their initial
width matches the actual mouse-resize minimum. They remain resizable. Section
Properties starts at 660 logical pixels wide.

The Tree header contains File Settings, Material, Parameters, Family Table and
Relations, in that order. Availability follows the existing document actions.
Parameters and Family Table are no longer duplicated in the View toolbar. The
document-type heading is omitted; source/Drawing navigation retains its icon
and destination label.
Single Tree selection confirms visible geometry in cyan. An ordinary double-click
also exposes the object's existing dimensions where supported, without opening
Properties or the source document. Source documents open through context menus.

A perpendicular external axis is stored as an `axis_point` reference and shown
as a point cross. Nonperpendicular or missing source axes hide that cross and
invalidate solving without deleting its stable identity or dependent constraints.
Reference refresh restores the same point when perpendicularity returns. Camera
orientation never changes this classification; only the source and Sketch frames do.

The common document Tree retains its native hierarchy guides and expand/collapse
indicators in Part, Assembly and Drawing. Feature names do not receive generated
`+` or `−` prefixes. Removing those prefixes does not change a feature's stored
add/subtract operation or its calculated geometry.

Context menus that offer **Active** place it first. The active Body, component or
Section uses the shared green `active-check` icon; inactive items have no check.
Activating an already active Section remains a no-op.
Body menus omit Return to Part and Insert before/after. Tree dragging controls
order and the existing Tree cursor controls insertion.

Tree objects with stored editable names expose a separate **Rename…** action:
Part features, Bodies, body operations, Sketches, construction objects and curve
points, saved measurements, Sections, Assembly cuts and groups, and Drawing
sheets and views. The shared internal name dialog offers OK and Cancel. Renaming
preserves calculated bodies, placements, references and projected drawing geometry;
it creates one undoable metadata transaction. Derived subcomponent labels and fixed
group headings have no independent name. Real Part and subassembly occurrences
invoke the shared source-document rename, updating the native path, document name,
open tabs and every matching occurrence name, including nested snapshots and
Undo/Redo states. This does not activate the source or change the displayed Assembly.
Pattern/Mirror groups retain their independent feature names. Saved document roots
reuse the same file rename action.

The shared Container Placement numeric panel has three aligned columns: position
X/Y/Z, absolute rotation RX/RY/RZ, and rotation correction RX/RY/RZ. Units remain
visible in the fields. Reference entry and orientation controls sit above this
grid. The layout is shared by Body, feature, Sketch, construction, Mirror, Pattern
and Section properties. It preserves numeric locks, constrained-axis states,
reference resolution and persisted placement values. This common-panel layout
change was explicitly approved on 2026-09-21.

Save As offers native document formats; output formats belong under Export.
Separate application instances are opened through the operating system rather
than a duplicate New Window menu action. See [Document copy](DOCUMENT_COPY.md),
[Drawings](DRAWINGS.md) and [Multiple instances](MULTIPLE_INSTANCES.md).

For the reference-table lifetime contract during profile handle dragging, see
[C++ performance measurements](CXX_PERFORMANCE.md#profile-handle-updates-2026-09-20).

Numeric dimension editing in Part, Assembly and Drawing uses the shared
`cpp/app/inline_dimension_edit.hpp` field. Keep its size, colours and Escape
handling identical; do not introduce a Drawing-only OK button. Drawing value
ownership and Tree groups are described in [Show/Erase](DRAWING_SHOW_ERASE.md).
