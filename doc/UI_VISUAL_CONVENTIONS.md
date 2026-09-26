# Modeling and Sheet Metal visual conventions

The toolbar icons are native SVG assets under `resources/icons`, using a
24 × 24 view box. Keep silhouettes simple enough to read at the application's
16 px toolbar size. The revised line icons use 1.75-unit strokes with rounded
caps and joins.

The workspace applies the saved application font before constructing its actions
and widgets, both on cold startup and when recreating the window after a language
change. Language changes preserve the selected ISO/system font and its size.

`Application/UseISOFont=false` is the default: dialogs, the Tree, menus and
buttons use Qt's platform GeneralFont, including its platform size. Global
Settings exposes **Use ISO font for the GUI** as a checkbox; enabling it uses
the bundled ISO font for those controls. There is no configurable font-family
picker and no additional bundled GUI font.

The one bundled `osifont-lgpl3fe.ttf` is also the GUI fallback. It is appended
to the system font's family preference list; Qt 6.8 and newer additionally
register it as the application fallback for Latin, Cyrillic and Greek glyphs.
Fallback addresses unavailable families or glyphs, not aesthetic defects in
a successfully loaded font. The checkbox provides an explicit user override.

View/Sketch annotations explicitly use the technical font, independently of
the application font. Drawing rendering uses the same shared font registration
in `cpp/common/technical_font.hpp`. Registration prefers the embedded resource;
standalone tools can load the same existing font file. Native Sketch text
outlines keep their existing ISO geometry/font contract. Changing the GUI font
does not change document text geometry or Drawing font choices.

The font GUI contract checks switching in both directions, missing-family
fallback, independent technical fonts and the translated checkbox/tooltip in
all five languages. Dialog layout and numeric-field checks cover the system
font default.

- Neutral geometry uses `currentColor`, resolved from the application's
  `WindowText` palette role for light and dark themes.
- Modeling operations use green `#80AA1A`.
- Reference geometry (Point, Axis, Plane and 3D Curve) uses brown
  `#AD6E2E`, matching the View reference colour. Mirror uses the same brown axis.
  The displayed document's main Origin point stays black. Construction Point
  markers and all feature, Body and occurrence Origin points use the same brown
  as planes. Marker sizes and hover/confirmation colours are unchanged.
  Sweep path endpoint markers also use brown in their ordinary state, with
  green hover and azure confirmation/reference colours.
- The Sketch icon, ordinary Sketch curves and Sketch point markers are white.
  Sketch construction lines and centerlines remain brown; Feature points keep
  their brown colour so they are distinct from Sketch points.
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

Command hover recovery uses one application event filter tracking the last
hovered toolbar button. If a toolbar refresh or Properties window interrupts
Leave/release delivery, pointer movement elsewhere clears stale hover/pressed
state and schedules repaint. No polling timer or per-button application event
filter is used. Mouse activation, keyboard activation and open popup menus keep
their normal button semantics. The Cylinder Axis GUI regression exercises a
real toolbar click followed by movement into View.

**View > Colors and appearance** opens the existing appearance editor directly;
there is no intermediate one-item Colors submenu. The action retains its
existing availability rules and toolbar entry.

Near-vertical outside dimension labels reserve clearance for their complete
font-dependent background mask, including tolerance text, beyond the endpoint
arrow. The leader stays parallel to the measured line and its elbow/support
is extended only as needed. The same presentation drives painting, picking and
grips; measured values and stored annotation placement are not rewritten.
Regression checks cover both label sides and leader directions at three font
sizes, and compare arrow pixels with/without text in the actual Sketch View.

Ordinary View Sketch strokes are white and 3D Curve strokes are brown (`#AD6E2E`),
one logical pixel wide. Properties and Sketcher use white strokes at the existing editing
width; construction geometry and external references retain their own styles.
Hover uses interaction green and confirmation uses azure, including Sketch points when the
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
Single Tree selection confirms visible geometry in azure. An ordinary double-click
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


## Interaction colour contract (2026-09-24)

- Hover offers use one green, `#4DD811`, in View, Drawing, the Tree, tabs,
  command buttons and reference controls. Hover never recolours confirmed
  geometry. It does temporarily replace an active GUI background and leaving
  restores that background.
- Active tabs, commands and Tree rows use an azure background (`#00D1FF`).
  Active Tree state belongs to the row background, not green feature text.
  The insertion cursor retains green text. Reference input retains its green
  outline; reference inspection remains azure and independent of input ownership.
- Confirmed feature wire, reference inspection and pending preview wire use
  azure (`#00D1FF`) for every operation, including subtraction and surfaces.
- Sketcher construction curves and axes are orange (`#FF8C00`). This is an
  intentional distinction from brown (`#AD6E2E`) model/Drawing axes.
- Tree Origin icons distinguish document (white `#FFFFFF`), Body (red
  `#FF0000`) and feature/container (green `#4DD811`); hover and selection affect their row
  background, not the icon's identity colour.
- A through-all preview has a dashed symbolic terminal outline. Longitudinal
  connectors remain continuous; both terminals are dashed for two-sided
  through-all. This changes transient presentation only, never calculated
  geometry, end conditions, topology identity, or print/export line semantics.

Hover,
selection and colour changes must not invoke OCCT or change the common picking
contract, document data, regeneration, or Undo/Redo.
Verification: the Windows application, CLI and Drawing harness were rebuilt.
GUI interaction, Sketch stroke styles, five-language localization, selection
filters, cylinder-axis command lifecycle, owned profile references, surface
conversion, Drawing UI, viewer picking and sheet-cut preview contracts passed.
Pixel checks cover green hover, azure confirmation, preview dashes,
active-command hover/restore and Origin icon colour in Normal/Active/Selected
states. No user-visible strings were introduced.

The broader Section UI test reaches an existing unrelated failure:
`Drawing sheet/view has no Rename action`. The Drawing selection-menu branch is
unchanged from HEAD and does not provide the action expected by that test. This
colour revision does not alter menu functionality to address that separate issue.
The transient `preview_terminal_dashed` flag is not serialized and leaves all
preview semantic roles unchanged, including sheet-cut footprint inputs.
### Sketch interaction refinements (2026-09-24)

Sketch-entry buttons share a white background (`#FFFFFF`) and dark text (`#102027`).
Hover remains green and an
active/checked button uses azure. Primitive owned-Sketch entry consumes the
same helper as the other feature dialogs. Menus explicitly style their normal
background, separators, disabled labels and green offered row instead of mixing
native hover painting with a partially styled menu.

View-to-Tree Sketch selection is synchronized with Tree signals blocked, so
an intermediate empty Tree selection cannot erase the confirmed entity or
its application-side IDs. Native segment midpoint offers precede the segment
and sketch axes; external C/CC contacts retain priority. This ordering does not
change the common picker, hit tolerance or persisted constraints.

No visible text or document format changes are introduced by these refinements.

Component display snapshots now publish the same idle Sketch profile as the
Part view. Sketcher axes, midpoint handles, external-reference display aids and
constraint markers no longer leak into the passive Assembly view. Native curves
and ordinary points remain visible, while original-reference geometry is retained.
This is display-only filtering; no placement, constraint or solid calculation
is changed.

Verification includes actual Sketch clicks with a pixel check for azure selection,
midpoint offer and committed M, and the existing external C/CC rectangle cases.
A focused nested-Assembly return check (`ZIMA_VERIFY_SKETCH_RETURN_ONLY=1` with
`ZIMA_VERIFY_NESTED_BODY_ONLY=1`) checks unique curves and unchanged composed
Body/Part/Assembly coordinates after Properties OK and return. It also captures
the Sketch button and checks green menu highlighting. The full older
nested-Body scenario separately fails at its active/passive later-history assertion,
before the return operation; that broader failure is not hidden by the focused
return check and remains to be investigated.
Final Windows verification passed: UI contracts, five-language catalog validation,
workspace contracts, native midpoint/selection and external C/CC GUI cases, and
the focused nested return/menu scenario. The latter also checks that passive
component display contains no Sketcher axes or midpoint handles. The normal
`zima-cad.bat` launcher points to the rebuilt Windows executable.

### Confirmation checkmark contrast (2026-09-24)

The shared `active-check` icon retains its green normal state and provides a
black hover pixmap using the same SVG silhouette at every supported size.
This covers OK buttons, Finish Sketch and other consumers of the shared icon;
leaving the control restores its green icon. Sketch-entry buttons now use white
and dark text instead of the earlier orange treatment. No text, translations,
command behavior or stored data changes are involved.

### Uniform selection and preview (2026-09-24)

The user superseded operation-dependent wire colours: all confirmed selections,
reference inspections and pending feature previews use azure. Green remains the
hover colour. The viewer operation-colour map, feature classification helper,
public colour setters and per-refresh feature scan have been removed entirely.
Operation controls and model semantics are unchanged; explicit annotation colours
and the normal appearance of unselected geometry retain their existing meanings.
The checkmark hover handler explicitly selects the black pixmap on Enter and
restores green on Leave, since Qt button styles do not all use QIcon::Active.
Final verification passed for button Enter/Leave colour restoration, uniform azure selection/reference/preview rendering, through-all dashes, and all five localization catalogs. The Windows executable was rebuilt. Keyboard focus alone does not switch the confirmation checkmark to black.

### Menu action icons and Tree ordering (2026-09-24)

Tree context menus no longer expose Move Up/Move Down actions for Part features,
Sketches, construction objects or Assembly cuts. Manual Tree drag/drop ordering
and its validation remain unchanged. Only the redundant menu actions and their
menu dispatch branches were removed.

File Close, Rename, Delete and Working Directory actions have icons, including
the deletion submenus. Properties, Edit, Rename, Suppress/Restore and Hide/Show
reuse shared semantic SVG icons in context menus, including relevant Drawing
annotation menus. Existing action labels, translations, shortcuts, enabled state,
confirmation flows and model operations remain unchanged.

Localization review: labels were reused without adding or changing visible text;
all five catalogs pass validation. UI and selection-filter checks pass. The family
rename fixture now uses names already compatible with every naming configuration
(`FROM_TAB`, `FROM_TABLE`), so its synchronization assertions no longer conflict
with uppercase/space normalization. Production naming behavior is unchanged.
Select Parent uses a nested-box arrow, Ground/Release use grounded and lifted
component symbols, and Regenerate uses paired circular arrows. Modeling and Drawing
regeneration actions share the same icon. These additions change no action text
or behavior.
The Applications menu reuses the existing Part, Assembly, Bend, Surface, Sweep
and Drawing icons for its six mode actions. Piping remains hidden. Mode switching,
check states and all five localized labels are unchanged.

Parameters, Material, Family Table and the lower dimension catalog in Relations allocate
spare vertical space to their tables using the shared expanding-table presentation.
Initial and minimum window sizes are
unchanged; headings stay above the table and action buttons below it. This layout
change introduces no visible text or translation changes.
