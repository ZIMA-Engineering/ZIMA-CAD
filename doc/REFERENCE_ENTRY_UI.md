# Modeling reference-entry presentation

The 3D Curve editor is the presentation reference for modeling property dialogs.
This convention does not change feature geometry, reference ownership, the shared
container placement contract, or the meaning of OK and Cancel.

## Shared controls

Use `cpp/app/reference_table_style.hpp` for command-owned reference tables and
`zima/ui/reference_cell.hpp` for reference fields, row actions and inspection.
Numbering belongs to the vertical header. The first data column contains the
entry arrow or remove button; it must not replace row numbers. Existing logical
column indices may remain stable while the header changes their visual order.

Green outlines identify active input. Green text identifies an offered new entry.
Hover uses the existing shared reference-cell presentation. Inspection remains
independent and cyan. Do not add an inspection action to a command unless its
controller supports independent inspection of that exact reference.

Row mutation remains command-owned:

| Editor | Meaning of removing a row's reference |
| --- | --- |
| Container placement, Thread, Mirror/Pattern references | Clear the value; retain the fixed slot |
| Curve points | Delete the point, with existing route validation |
| 2D/3D Sweep profile station | Remove its explicit profile; retain the station and profile inheritance |
| Shell and Drill Point faces | Remove the selected face entry; keep an offered input row |
| Fillet/Chamfer route | Remove the route and its members |
| Fillet/Chamfer member | Remove only that member; retain the route's existing restore behavior |
| Unbend/Rebend individual elements | Retain the existing owner-list and all/individual selection behavior |

The Fillet/Chamfer editor retains its expandable route hierarchy. Its separate
number and action columns apply the same presentation without flattening routes.
The offered input row is UI-only and never becomes an empty persisted route.

## Resizing

Forms retain natural spacing and stay at the top. Reference collections at the
bottom consume additional height through `expandBottomTable` and a positive
layout stretch. The 2D Sweep profile table, 3D Sweep profile table, Fillet/Chamfer
routes, Shell faces, Drill Point faces and Unbend/Rebend elements follow this
rule. In 3D Sweep, the point table keeps its existing minimum height so increasing
the window does not move all later parameter rows; the final profile table grows.

## Review scope

The modeling-dialog review covered ordinary placed primitives, Point/Axis/Plane,
Curve and Sweep, shaft Thread, Fillet/Chamfer, Shell, Drill Point, Mirror/Pattern,
Unbend/Rebend, Sheet from Body and Cylinder Axis. Existing common placement fields
are consumed unchanged. Numeric-only forms and derived station/status fields are
not interchangeable with editable reference lists. Assembly target checklists,
document parameter tables and Drawing annotation editors have separate ownership
and are not converted into modeling reference lists by this change.

No new localized UI labels are needed. Existing translations are reused in all
five languages. UI checks cover fixed-slot clearing, route/member removal,
profile-station preservation, pending edits, and dialog layout at both supported
test window sizes.

The dedicated 2D Sweep and edge-treatment GUI workflows verify model interaction.
The edge-treatment workflow covers all five Fillet/Chamfer modes, annotation
grips, direct dimension editing, OK/Cancel and Undo/Redo. Specialized GUI suites
are dispatched before entering the general console suite's large stack frame;
otherwise accumulated test-local document snapshots can exhaust the Windows
main-thread stack during a kernel calculation. This is test-runner isolation,
not a change to the production calculation or the executable's stack limit.
