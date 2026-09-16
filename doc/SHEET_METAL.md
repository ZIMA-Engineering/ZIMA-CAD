# Sheet Metal

## Implemented document defaults

Select **Sheet Metal** in the application dropdown above the right-hand Part
commands. The panel starts with **Selection**, followed by **Sheet Metal
Properties...**. That shortcut opens **File Settings > Sheet Metal**. Opening
File Settings from Tools accesses the same fields, dialog and transaction.

- **Default material thickness** is an always-editable numeric field without a
  checkbox. It starts at **1 mm** when the Part has no stored thickness. OK saves
  the displayed value; Cancel leaves the Part unchanged. It accepts finite positive
  numbers in millimeters, up to 1,000,000 mm. These units are explicit and independent
  of display units.
- **Default K factor** is the Part's material property `SHEETMETAL_K_FACTOR`.
  It accepts finite numbers from 0 to 1. When the material has no value, the
  initial default is 0.5; it is not a material-specific manufacturing calibration.

Defaults are stored in the native Part. Thickness uses the shared document
parameter `SHEETMETAL_THICKNESS`; K factor uses the existing material property,
with dimensionless unit `1`. These are existing native metadata stores; no file
format change, sidecar, or template conversion is needed. Loading another material
can change its K factor while leaving the document thickness intact.

OK commits both defaults together. Cancel discards pending edits. The shared
properties-window behavior includes confirmation by middle-button double-click
over the owning View. Undo/Redo and native save/reopen retain the settings.
Changing these defaults preserves the calculated model geometry. The settings
belong to the active Part source, including while editing it inside an Assembly.

Sheet Metal Properties is an editing shortcut, so it is absent from **Insert**.
Assembly file settings have no Sheet Metal page.

Closing File Settings restores the application dropdown immediately, after OK,
Cancel or middle-button confirmation. Switching back to Modeling does not require
regeneration or a tab switch. Application switching remains disabled while a
properties editor owns the interaction.

## Flat sheet (Tabule)

The **Tabule** command is available in the Part's Sheet Metal toolbar and Insert
menu, before Bend. It creates one additive history container with one owned
Sketch, using the existing container placement and work-plane selection. The
Sketch lies on the container plane; this feature has no separate profile-plane
offset. Container XYZ and reference offsets retain their usual behavior.

Draw a closed outline through the green **SKETCH** button. Closed inner loops
make openings. The feature uses the same exact profile extrusion calculation as
Extrusion; an open or empty outline cannot be committed. The cyan wire previews
the pending sheet without invoking the solid kernel.

Circular and elliptical profile loops publish their extrusion axes, including
inner openings, with the same View display, axis filter and tree controls as
Extrusion. These axes are persisted with the calculated native geometry.
The shared profile classifier supports elliptic openings mixed with ordinary
closed outlines, nested ellipses and disconnected elliptic regions. Polygon
winding follows the extrusion direction so reversing the side also keeps circular
and elliptic inner loops valid. Exact conics reach the kernel; sampled contours
are used only for nesting and intersection validation.

- **First side:** extrude along the selected Sketch normal by the total thickness.
- **Second side:** extrude in the opposite direction by the total thickness.
- **Symmetric:** put half of the total thickness on each side of the Sketch.
- Thickness follows the Part's `SHEETMETAL_THICKNESS` default (1 mm initially).
  **Custom thickness** enables the local numeric field. Unchecking it restores
  the inherited value. Changed document defaults take effect on explicit
  regeneration, as for Bend.

Creation and editing share one internal properties window. SKETCH edits a
transient draft and returns to that window. OK validates, calculates and commits
one history transaction; Cancel discards the pending changes. Editing rolls back
to the feature's input boundary. Native save/reopen and Undo/Redo preserve the
feature, profile, thickness policy and extrusion side.

Faces, edges and vertices use Extrusion's source-based topology ancestry.
Changing the extrusion side or thickness preserves the semantic Start/End and
source-curve identities. Flat creates ordinary solid material, including in
Drawing views; it is not a yellow surface feature. Family Table can vary the
local thickness when Custom thickness is enabled.

Console commands share the GUI's atomic workspace transaction:

```text
flat.create width_mm=40 height_mm=30 direction=symmetric
flat.get container=<id>
flat.set container=<id> thickness_mm=2
flat.set container=<id> thickness_override=false direction=reverse
```

`flat.create` supplies a rectangular initial profile for console use; the GUI
starts with an empty editable Sketch. `thickness_mm` enables the local override;
an explicit `thickness_override` argument takes precedence. These commands are
Part-only. Profile geometry remains editable through the common Sketch commands.

`zima_cpp_flat_command_tests` checks thickness inheritance and overrides, all
three directions and their preview bounds, persistent topology, circular and
elliptical openings and their axes, Sketch dimension changes, a changed work
plane, save/reopen, Undo/Redo, Family thickness and invalid-input rollback.
The application-tools UI contract covers real toolbar/dialog/Sketcher transitions,
empty-profile rejection, Cancel, middle-button OK and the application dropdown.

Modeling Extrusion and Revolution can use a Flat face as their Sketch support,
including in a Part with evaluated Family Table variants. A newly owned Sketch
keeps the pending feature's complete parent/child identity and resolved frame in
the Properties draft. Entering Sketcher or returning to Properties does not
insert a placeholder into the Part or publish an incomplete profile to variants.
Only OK commits the feature. The profile-on-sheet GUI regression exercises Flat
face selection, Sketch entry/return and Cancel for both commands with a calculated
family member.

Bend annotations retain the plane of their individual start, trajectory or end
Sketch. The rotated-frame tests check dimension witnesses and normals in Bend
and Unbend; signed origin dimensions also remain valid at the opposite attachment
endpoint when the prepared start span runs from `-width` to zero.
Properties displays each auxiliary Sketch annotation once, using its pending
frame instead of overlaying the saved and pending frames.

When Bend dimensions are displayed, a selectable **Bend** or **Unbend** label
shows the current state. Double-clicking this label opens Bend Properties. In
Family Table, click the label to bind the active column to the state; each row
offers **Bend**, **Unbend**, or an empty inherited value. The column uses the
parameter's stable secondary identifier. The existing View state button remains
available for directly toggling the feature outside Properties.

The focused Windows run passes ten suites: Flat, Bend, Holes, application tools,
Family Table, dimension identifiers, extrusion limits, profile commands, thin
profiles and surface profiles. The separate monolithic `zima_cpp_contract_tests`
currently stops while loading `tests/fixtures/cross_language/part.prtz`, an obsolete
format-26 fixture. It does not reach its geometry assertions; no legacy reader was
introduced to make that fixture pass.

## Console

`document.settings.get` returns a `sheet_metal` object for Parts:

```json
{"thickness_mm": null, "k_factor": 0.5}
```

`document.settings.set` accepts partial updates through the same workspace
operation as the GUI:

```json
{"command":"document.settings.set","arguments":{"sheet_metal":{"thickness_mm":2.5,"k_factor":0.42}}}
```

Set `thickness_mm` to `null` to clear the default. Invalid values fail before
changing the document. Unchanged values do not create an Undo entry.

## Bend / Unbend

**Bend** is available in the Sheet Metal toolbar and contextual **Insert** menu.
Creation and editing use the same internal properties window, OK/Cancel and
middle-button double-click confirmation. Changes remain transient until OK.
Editing evaluates the existing history boundary before the Bend.

- The command consumes ordinary container placement. With an automatic Base plane,
  selecting a straight outer edge and its narrow planar attachment face puts the
  start profile in that face, its segment along the edge, and thickness into the
  face. The initial sweep tangent leaves the face outwards. This also works on
  the End face of an existing Bend; another perpendicular face is not required.
  Edge direction reversal does not reverse the material side. Base plane remains
  available for an explicit manual choice; FRONT/BACK, rotation and offset remain
  available. These are Bend profile frames, not a different container solver.
- The ordered placement sequence **Edge, containing Plane, Point** uses the last
  point as a station along the edge: its perpendicular projection locates the
  origin, including when the point is the opposite corner of the attachment
  rectangle. The reference row identifies this meaning. A third intersecting
  plane can locate the origin instead. Point-first placement still anchors the
  origin directly to that point. Reference order and identity are persisted in
  the native document; no additional geometry file or cache is required.
- One history container owns three prepared Sketches: the start profile (initially
  a 40 mm segment from 0 to +40), a circular trajectory, and the end profile. Their editors are
  available in the same properties window. The end frame follows the path tangent
  automatically; it has no independent twist.
- Each profile has one non-construction straight segment. The end editor includes
  a fixed construction copy of the start segment and two endpoint difference
  dimensions. Their initial values are zero. Positive entry preserves the current
  side; negative entry reverses it, following the ordinary Sketch dimension rule.
  The initial positive direction widens each end. CLI extension arguments are
  signed: positive widens, negative shortens. End width must remain positive.
- Both start endpoints have C constraints to the horizontal Sketch axis and
  separate horizontal dimensions from the Sketch origin (initially 0 and 40 mm).
  Both end endpoints have C constraints to their axis and difference dimensions
  from the corresponding transported start endpoints. Editing start dimensions
  moves these reference points without replacing Sketch, curve or point IDs.
  During new GUI placement the initial span points toward the selected edge's
  other end: at the opposite corner its endpoint coordinates become `-width`
  and `0`, shown as signed coordinates from the Sketch origin. This preserves the
  width, material side and outgoing tangent. Opening
  Sketcher or editing its geometry ends this automatic initial layout.
- Set the inside radius (0–1,000,000 mm) and angle (0–180 degrees).
  Material extends from the segment toward the rotation axis, at outside radius R+t.
- The prepared path is the **outside** circular arc. Its Sketch radius dimension
  is R+t; editing that dimension updates the inside radius in Properties. The
  **Radius follows thickness** checkbox continuously sets the inside R to t and
  locks the path radius. Unchecking it retains the current effective inside radius.
- Thickness and K factor follow the Part settings. **Local value** enables an
  override for each separately. A missing Part thickness evaluates as 1 mm.
- **Bend** carries the section along the circular trajectory, interpolating the
  endpoint differences across the sweep. Matching profiles use exact revolution;
  a width transition uses a two-section sweep. **Unbend** connects the profiles
  along `angle_radians * (R + K*t)`. It shows an axis at the developed region's
  midpoint. K is a manufacturing input, not inferred from the geometry. The bent
  and flat simplified solids need not have equal volume when K differs from 0.5.
- At exactly zero degrees the feature contributes no material, keeps its history
  identity and can be edited back to a positive angle. It has no selectable faces
  at that boundary. This avoids a degenerate OCCT solid.
  The trajectory editor is disabled at zero angle; change the property angle to
  restore it. The last nondegenerate path retains its curve and point identities.
- An inside radius of zero is supported for **matching profiles**, including a
  180-degree fold. The inner cylindrical face collapses and is absent; surviving
  faces retain their identities. This is the geometric foundation for a future
  Hem command. Variable-width R=0 bends and zero-length developments are rejected
  atomically. Contact between attached sheet legs is not handled by this command.
- The cyan wire preview is generated analytically without OCCT. OK and explicit
  Regenerate calculate the solid. Document default changes affect existing Bends
  on the next calculation; switching tabs does not regenerate them.

Start and End retain their respective semantic face identities across Bend and
Unbend. Face and rim identities derive from the feature, section, source segment
and endpoints. Their geometry changes while their persisted ancestry stays stable.
Neither OCCT traversal order nor preview edges define persistent references.
Calculated station rims and vertices carry Start/End ancestry from the authored
path point, profile, curve and point. Their persisted original references are
available for subsequent Bend placement, including after Bend/Unbend.
Double-click the Bend in View to inspect its dimensions. **Unbend/Bend** appears
in the View and calculates one undoable state change without opening Properties.
Radius, angle and endpoint differences also support inline dimension editing.
All three Sketch editors remain pending until the owning properties window is
confirmed. Cancel discards them together.

Ordinary circular body edges remain available to the Drawing radius-measurement
command. For a width transition, the side edges need not be circles: the authored
outside path radius and the endpoint dimensions are available through Drawing
Show/Erase model dimensions. The path radius annotation is omitted in Unbend and
at zero angle. Do not interpret a width-transition edge as a circular arc.

A circular Bend with matching start/end sections retains analytic lines and
cylindrical surfaces throughout the exact revolution calculation. Converting
those sections to NURBS unnecessarily produced general revolution surfaces and
could stall volume integration for a rotated 180-degree Bend. Variable-width
sections continue to use the general Sweep calculation.

Native Part INI format **30** / JSON payload **54** stores Flat and Bend parameters, the
owned Flat profile, Bend start Sketch and both embedded Bend Sketches;
`config/templates/start_part.prtz` uses that format. Earlier Part
formats are intentionally unsupported. Assembly format is unchanged.

Console commands `bend.create`, `bend.get` and `bend.set` use the same workspace
transaction as the GUI. Creation accepts `width_mm` (default 40), `radius_mm`,
`angle_degrees`, `thickness_mm`, `k_factor`, `thickness_override`,
`k_factor_override`, `radius_follows_thickness`, `first_extension_mm`,
`last_extension_mm`, `state` (`bend` or `unbend`), `name` and `document`.
Editing uses `container` and the same parameters except width; edit the owned
Sketch to change width. Supplying thickness or K enables its override unless the
corresponding override flag explicitly says false. Readback reports effective values.

```json
{"command":"bend.create","arguments":{"width_mm":40,"radius_mm":5,"angle_degrees":90}}
{"command":"bend.set","arguments":{"container":"<id>","state":"unbend"}}
```

## Reference study (2026-09-16)

### Open issue: trajectory endpoint references during Unbend

The user's `Projects/03.prtz` contains Flat, Bend, and a second Flat. The second
Flat references the Bend End cap, the end-profile segment, and the circular
trajectory's end point. Switching to Unbend moves the cap and the end profile,
but the authored circular trajectory remains in its bent frame. Its endpoint
therefore conflicts with the other two placement references. The Flat correctly
retains its last valid placement and reports an invalid reference set; the
identities themselves are still present.

Reproduced through the native CLI on 2026-09-16. In a disposable copy, replacing
only that third reference with the end profile's first endpoint keeps placement
valid through Unbend and back to Bend. The original user document was not changed.
This is a verified workaround, not a trajectory-reference fix. Release 2026091605
retains this limitation. The user deferred its resolution to the next session.

Next work must define how the Bend's evaluated trajectory references follow the
developed state while retaining the editable authored arc, radius/angle dimensions,
and stable source identities. View, picking and downstream reference geometry
must agree. Do not weaken the general placement solver to accept contradictory
references. Verify both state changes, end-profile/trajectory attachments,
Undo/Redo, regeneration and native save/reopen.

### Three-Sketch study

The user's saved `Projects/01.prtz` example has a circular path of radius 5 mm
and sweep 45 degrees. Its parallel profiles span -2 to 40 mm (42 mm total) and
-5 to 50 mm (55 mm total), giving endpoint extensions of 3 and 10 mm. Their
directions have no twist. The arc starts at (50, 40, 0.5) and ends at
(50, 43.535534, -0.964466) mm. Its geometric length is 3.926991 mm.
These values were read from persisted Sketch geometry, without changing the file.

With material extending toward the arc centre, this R5 trajectory describes the
outside surface. At 1 mm thickness its corresponding inside radius is 4 mm.
The native example was not modified or converted during development.

## Implemented Bend verification

The 2026-09-16 profile/dimension repair passed twelve focused Windows contracts:
owned-profile frames, profile-on-sheet GUI, Bend attachment GUI, Sketcher,
Family Table, Sketch dimension commands, profile commands, dimension layout,
application tools GUI, Sketch dimension entry GUI, Flat and Bend. The two updated
GUI expectations were rerun after correcting the test family setup and replacing
the former unsigned-coordinate expectation. Logs are
`build/profile-bend-final-tests.log` and `build/profile-bend-final-retest.log`.
The saved `part.prtz` reproduction also passed Flat-face selection, owned Sketch
entry/return/Cancel, annotation-plane checks for both Bends, and common-picker
selection of their state labels into Family Table. A separate corrected copy
passed start-coordinate edits -45/-35/-40 mm and angle edits 60/120/90 degrees
without changing its start frame. The source file's original SHA-256 was retained.

The Family Table tests cover Bend/Unbend overrides, an inherited state, editing
the state from an instance, generic changes, native save/cold reopen and rejection
of invalid state values. State labels share the ordinary viewer candidate list;
their presentation test verifies text-only geometry at three zoom levels.

The Bend command contract checks analytical volumes for ordinary, variable-width,
flat and zero-radius bends; actual Sketch radius/angle/difference edits; inherited
and overridden parameters; the radius/thickness link; referenced Start/End planes;
stable surviving face identities; rejected edits; zero angle; Undo/Redo; native
save/reopen; Drawing radius measurement and model annotation; and Family Table
variants. At 180 degrees cap matching checks the authored section boundary as well
as its plane, including when the R=0 caps touch along the fold axis.

The application-tools GUI contract opens all three editors, returns to pending
Properties, checks Cancel and OK, edits the first history feature, confirms with
middle-button double-click, changes state in View and edits radius, angle and both
endpoint dimensions inline. It also verifies that leaving inspection hides the
state button. Captures: `build/bend-properties.png` and
`build/bend-three-sketch-view.png`.

Windows Release verification on 2026-09-16 passed twelve distinct focused
contracts across the final verification runs: Bend, application tools, Sweep,
Curve3D Sweep, Family Table, Holes, dimension layout, Drawing measurement,
Sketch dimension entry, Sketch dimension commands, dimension identifiers and
model dimension layout. Logs: `build/bend-final-tests.log`,
`build/bend-identities-tests.log`, and `build/bend-shared-sketch-tests.log`.
The final shared-Sketch run passed all three tests in 11.17 seconds. GUI and CLI
built successfully; the properties and View captures were visually inspected.
This is focused verification, not a claim that the entire repository suite ran.

The subsequent attachment repair passed 25 focused contracts on the rebuilt
Windows GUI/CLI, including 16 calculated Bend-to-Bend cases and 16 actual GUI
attachment scenarios. The matrix covers rotated 180-degree geometry, both edge
directions, off-edge point projection, start/end dimension edits, Bend/Unbend,
Sketcher return and native persistence. See the final verification section in
[Container Placement Analysis](CONTAINER_PLACEMENT_ANALYSIS.md#final-verification-of-the-combined-repair-2026-09-16).
