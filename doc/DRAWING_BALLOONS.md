# Drawing balloons

The Drawing command **Balloons** (Czech **Pozice**) labels the first level of
the sheet's bill of materials. A subassembly receives one item number; its
internal Parts are not separate BOM positions. Repeated occurrences of the
same source share their item number and quantity. Family variants remain
distinct source documents.

## Interaction

The command is on the right Drawing toolbar after Text. One internal property
window serves creation and later editing. Select its View reference and click
the drawing view. Reference fields use the shared green input outline and
independent azure inspection eye.

- **Show all** reveals existing balloons and adds one for each first-level BOM
  item with an available projected edge. It preserves existing placements and
  avoids placing a new circle over another balloon in the same view.
- **Erase all** hides balloons in the selected view. It retains their IDs,
  references and placements; Show all can reveal them again.
- **Add balloon** accepts an original edge of a Part or subassembly, followed
  by a click to place the circle. A nested Part edge identifies its owning
  first-level subassembly. More than one manual balloon may label the same row.
- Click an existing balloon to select it. Its purple center grip moves the
  circle; its purple leader-end grip changes the attachment on original
  geometry in that view. Dropping the endpoint on empty space preserves the
  old reference. Escape cancels a pending drag.
- Right-click a confirmed balloon for **Balloon properties**, **Hide** or
  **Delete**. Properties can replace its attachment and edit circle diameter,
  text height and paper position. Double-click opens the same window.

The circle, leader and number are white in the View. The attachment is a yellow
dot. Text height defaults to **5 mm** on paper and circle diameter to **16 mm**.
Long item numbers expand the rendered circle to fit. Purple grips are editing
overlays and never appear in output. PDF, DXF and JPEG share the sheet renderer;
paper output uses black ink on white.

Changes inside Properties are a single transient transaction: **OK** commits
and closes; **Cancel** discards all pending changes. A middle-button double-click
over the View invokes OK. A short middle click ends reference entry and
inspection without saving. Dragging outside Properties commits one Undo step.

## Identity and regeneration

A balloon stores its view ID and an original edge reference (owner, semantic
key and exact occurrence path), plus normalized curve position. No OCCT
enumeration index, live topology traversal or body calculation is used for
picking, display or editing. The sheet stores its BOM source document ID and
each grouped row's immediate occurrence paths. Unrelated views cannot reuse
the same row merely because their local occurrence IDs happen to match.

Regenerate rebuilds the sheet BOM and projected geometry, then updates balloon
numbers and attachments. A missing row or curve retains the last anchor and
number and displays red for repair. View deletion removes its balloons and
those of deleted projected descendants. Moving a view moves its balloons;
their circle size, text height and position offsets are in paper millimetres.

All data is stored in the native `.drwz`: Drawing INI **17**, payload **9**.
Part/Assembly formats and start templates are unchanged by this feature.

## Console

Seven commands use the same operations and history as the GUI:

- `drawing.balloon.list`, `drawing.balloon.get`
- `drawing.balloon.create`, `drawing.balloon.set`, `drawing.balloon.delete`
- `drawing.balloon.show_all`, `drawing.balloon.erase_all`

Creation takes `view`, `reference` (`owner`, `key`, `instance_path`) and
`position` (`[x,y]`, view-relative paper mm, right/up). Optional fields are
`parameter` (0–1 along the persisted curve), `diameter`, `text_height` and
`visible`. Set and Delete identify the existing `balloon`. Bulk commands take
`view`. Read-only queries work entirely from stored data.

## Verification

`zima_cpp_drawing_balloon_tests` exercises grouped occurrences, subassembly
ownership, source separation, numbering, broken references, native round trips,
Undo and view deletion. `zima_cpp_drawing_balloon_ui_contract` exercises actual
View picking, both grips, internal dialogs, transient edits, middle-button
confirmation, context properties, visibility and PDF/DXF/JPEG output.

Acceptance for development build **2026091511**: all 15 selected contracts pass
in `build/balloons-acceptance-tests.log`. This includes the full CLI process
contract, native documents, Drawing operations/rendering, Body properties,
measurement UI and translations. The UI regression verifies RMB cycling during
an endpoint drag and real DXF TEXT entities with a 5 mm height. GUI/CLI build
log: `build/balloons-acceptance-build.log`. The sheet and dialog captures in
`build/balloons-ui.png` and `build/balloons-ui.png.dialog.png` were inspected.
No portable release was published for this change.
