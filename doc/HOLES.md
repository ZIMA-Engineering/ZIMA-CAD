# Holes: drilled channels from a Sketch

Part -> **Holes** creates cylindrical channels in the input body. Each
non-construction segment of its owned Sketch defines one cylinder's axis,
start, and end. The shared **Hole diameter** parameter sets every cylinder's
diameter. Ends are flat; the feature adds no drill tip, thread, or extension.

## Interaction

- A selected standalone Sketch becomes Holes at the same history position.
  Its container, placement, and geometric identities remain unchanged.
- With no Sketch selected, the tool opens a new definition. **SKETCH** opens
  the working Sketch; **Finish Sketch** returns to the same dialog.
- Properties reuse the shared Sketch dialog with one added diameter field,
  below the work-plane offset and above **SKETCH**.
  Placement, references, and Origin interaction follow the same rules.
- **OK** validates, calculates the subtraction, and commits one transaction.
  **Cancel** also discards the pending Sketch. A short MMB click does not
  confirm; an MMB double-click confirms even over View.
- Editing shows the stored input before Holes. Opening Properties or entering
  Sketcher does not calculate a body. Closing restores the full history.
- Sketcher retires the property preview and offset handle while editing. Its
  axes use the normal brown plane color and its idle origin point is black;
  ordinary hover/confirmation colors remain available. Segments can attach to
  this origin through the common picker.
- **External reference** and **Reference -> outline** accept earlier original
  geometry in both new and existing Holes. A new draft uses its owning Body's
  insertion cursor and coordinate frame before it has a persistent history
  entry. Returning to Properties retains these pending edits; Cancel discards
  them with the rest of the draft.

Construction segments are not drilled. Non-construction circles, arcs,
ellipses, splines, and text are unsupported input in this first version.
Diameter ranges from 0.001 to 1,000,000 mm; each drilling segment must be at
least 0.001 mm long. Cylinders are united before subtraction, so intersecting
channels do not count removed material twice. This is a Part-only feature,
including a Part activated inside an Assembly; it does not create an Assembly-owned operation.

## CLI

```text
holes.create <sketch-id> <diameter-mm>
holes.get <container-id>
holes.set <container-id> <diameter-mm>
```

JSON uses `sketch` or `container`, `diameter_mm`, and optional `name` and
`document`. Existing `sketch.*` commands edit the owned Sketch; explicit
`regenerate` recalculates the channels after such changes. Shared commands
still expose Sketch properties and placement. `holes.create` and `holes.set`
call the same transaction as the GUI.

## Data and verification

`FeatureKind::Holes` stores the owned Sketch ID and diameter in the native document.
Circular profiles derive from stable segment IDs. Edge, face, and vertex keys
refer to the source segment/point and distinguish individual axes at shared
endpoints. Neither segment order nor OCCT traversal defines identity.
Calculation uses existing circular extrusions, their group, and subtraction.
The shared placement contract is unchanged by the Holes feature itself.

With the user's approval on 2026-09-15, native versions are Part **22**
(internal **46**) and Assembly **21** (internal **30**). Both start templates
are updated; older versions are not migrated. See [Work planes](WORK_PLANES.md)
for automatic/manual base-plane selection.

`zima_cpp_holes_command_tests` covers cylindrical volume, intersecting channels,
construction geometry, a rotated Sketch, transactions, save/load, and regeneration.
`zima_cpp_holes_ui_contract` checks the actual tool, dialog, Sketcher transition,
mouse attachment to the origin, both external-reference tools in new/edit
sessions, Cancel, OK, MMB behavior, Undo, and saved geometry.
`zima_cpp_sketch_reference_command_tests` also checks a draft's insertion
boundary and projection in a translated Body coordinate frame.

Windows Release verification (2026-09-15): all tests passed after correcting
truncated console `help` output (full run 162/163, followed by 2/2 console and
expanded GUI tests). Creating Holes from scratch also checks removed volume
and removal of the new history item through Undo.

The 2026-09-15 Sketcher follow-up passed the expanded Holes GUI contract, native
Holes and Sketch-reference command tests, and the work-plane GUI contract. The
mouse test waits for camera alignment and selects edges with a nonzero planar
projection; an edge perpendicular to the Sketch plane cannot become a line.
New/edit screenshots were inspected for the normal brown axes and black origin.
