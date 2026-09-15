# Application tools and document tabs

## Application selection

The heading above the right-hand Part tools is a dropdown with **Modeling** and
**Sheet Metal**. It uses the same actions as the **Applications** menu. Changing
either control immediately updates the other control and the displayed commands.

The choice is retained separately for each editable document during the current
session, including across tab switches and regeneration. A newly opened Part
starts in Modeling. In an Assembly, the active source document determines the
available tools; the displayed top-level Assembly remains unchanged.

Application selection is disabled during feature editing and Sketcher sessions.
Sketcher supplies its own tools. Drawing and Assembly retain their existing tool
headings. Sheet Metal currently displays a disabled placeholder: sheet-metal
modeling commands have not been implemented.

## Insert menu

**Insert** follows **Edit** in the main menu and exposes the commands available in
the current right-hand tools. This includes Part feature commands, Assembly
component insertion, Sketcher geometry and constraints, and Drawing commands.
Selection, Sketcher camera controls and command completion are omitted.

Menu entries reuse the actual toolbar actions, including their enabled state,
icons and submenus. There is no separate dispatch or command implementation.
Changing the application or entering/leaving Sketcher rebuilds the menu from the
same context as the toolbar. With no document open, Insert is disabled.

## Profile status

Extrusion and Revolution Properties show **Empty**, **Open**, **Closed** or
**Invalid**, based on their native Sketch data. Construction curves do not count
as profile geometry. A connected open chain reports Open; closed profile regions
report Closed. Disconnected/unsupported profiles and broken derived geometry
report Invalid. Status refreshes when returning from Sketcher and is independent
of the selected Solid, Thin or Surface result type.

Inspection uses the existing native profile builders without invoking OCCT or
changing the Sketch. It describes profile connectivity; final modeling validation
still runs when the operation is calculated.

## Stable tab width

The unsaved-change asterisk occupies a fixed-width slot beside the close button
in Part, Assembly and Drawing tabs. Saving clears the marker without changing the
tab width. The Window menu also retains the unsaved-change indication.
Saving under a different filename may naturally change the tab width.

## Verification

`zima_cpp_application_tools_ui_contract` exercises both application selectors,
per-document choices, regeneration, Part/Assembly/Drawing/Sketcher Insert menus,
open-profile status through Sketcher, and exact tab widths before editing, after
editing and after Save. The surface profile geometry and GUI contracts verify
native open/closed/invalid/empty classification and closed Extrusion/Revolution
profile status.

Windows Release validation on 2026-09-16 passed the application tools GUI,
surface profile geometry/GUI and Sketch Offset GUI contracts. The rebuilt
CLI process suite also passed (44.45 s). The application toolbar screenshot
was inspected for menu order, dropdown placement and tab spacing.
The full workspace startup contract passed in 122.95 s.
