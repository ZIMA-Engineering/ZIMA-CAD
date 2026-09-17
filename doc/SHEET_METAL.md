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
Changing these defaults calculates Parts containing Flat or Bend before committing
the settings and geometry together. Calculation errors leave the previous state
intact. Parts without sheet features keep their calculated geometry. The settings
belong to the active Part source, including while editing it inside an Assembly;
Assembly mate solving and Assembly-owned operations still require their own
explicit regeneration.

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

### Automatic Flat attachment to Bend

Flat retains ordinary free placement. Selecting a straight outer boundary of a
Bend Start or End cap in the first reference field activates automatic attachment.
Inner boundaries, curved Bend edges and thickness edges are excluded from this
shortcut. Hover and confirmation use the same eligibility check and still obey
the user's selection filters.

The edge, its joining cap and a native endpoint fill the placement references.
The profile lies in the outer tangent plane and its positive Y direction leaves
the cap. Thickness defaults to **Second side**, toward the inner radius, and
inherits the source Bend thickness. The profile remains an ordinary editable
closed Sketch; the command does not prescribe its outline. Direction remains
editable, but changing it can intentionally remove the full-thickness connection.

Both edge endpoints are supplied as external Point references in the owned
Sketch. Their stable IDs can be used by Sketch constraints and dimensions;
source geometry updates their cached positions. **Other edge endpoint** changes
the placement origin without reversing the material side. The derived face,
orientation and profile plane cannot be overridden while attached. **Manual
placement references** returns to ordinary placement and retains the external
points as explicit Sketch references.

Drawing a rectangle snapped to both external points preserves the endpoint
bindings. Its automatic directional constraints omit only a redundant relation;
conflicting constraints are still rejected. Regeneration compares attached Flat
profiles as well as placements, so changed source width recalculates the body
after the endpoint-driven Sketch changes.

Attachment consumes persisted original topology and the existing container
placement solver. Preview, picking and opening Properties do not traverse OCCT.
Explicit source recalculation updates the attachment frame and inherited
thickness. Missing source geometry leaves an unresolved reference, never a
nearest-edge substitution. A zero-angle Bend has no solid boundary to attach to.

The attachment flag is stored in Part JSON 58 / INI 34; Assembly JSON 40 / INI 28
and both tracked start templates accompany this format update. Legacy native
documents are unsupported.

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
  the inherited value. Confirming changed document sheet defaults calculates
  the Part immediately, including dependent Bends, in the same transaction.

Creation and editing share one internal properties window. The **Switch** button
beside **Direction** cycles First side, Second side and Symmetric through the same
preview and parameter update as the dropdown. SKETCH edits a
transient draft and returns to that window. OK validates, calculates and commits
one history transaction; Cancel discards the pending changes. Flat and Bend OK
explicitly calculate the Part even when the parameters are unchanged, so
confirming also restores missing
calculated geometry. An unchanged definition adds no Undo entry. Cancel never
triggers that calculation. Editing rolls back
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
flat.create edge_owner=<bend-id> edge_key=<outer-cap-edge-key> height_mm=30
flat.set container=<id> origin_last=true
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

### Persisted sheet topology and automatic attachment

Flat and Bend remain ordinary solid features and can be mixed with Modeling
operations in one Body history. Their calculated native face references also
carry `SheetFaceRole` and material thickness. Flat labels its authored Start/End
faces as SideA/SideB and its boundary walls as ThicknessFace. Bend labels the
authored outer/inner surfaces as SideA/SideB and its end/side walls as
ThicknessFace. Curvature does not imply Unknown: cylindrical Bend surfaces have
known roles. New faces without a proven sheet role remain Unknown.

Roles are attributes, not topology identities. Existing parent/child identities
are unchanged. During explicit calculation the viewer packet stores adjacent
face identities and endpoint identities for edges. Two thickness faces identify
a thickness edge; one thickness face and one principal side identify a boundary
edge. Classification uses that persisted adjacency, never relative edge length
or an OCCT traversal during interaction. Flat and Bend placement do not offer
thickness edges. Ordinary Modeling selection is unaffected.

Selecting a straight sheet boundary edge for Bend fills its edge, joining face
and endpoint references. The chosen edge starts the **outer radius**. The angle
remains 0–180 degrees; selecting the corresponding opposite boundary edge changes
the side. The joining face and orientation are derived and cannot be changed
independently in automatic mode. The confirmed edge's nearer endpoint is the
initial origin; **Other edge endpoint** switches that origin without reversing
the material side. The edge and endpoint can also be replaced through the
reference table. **Manual placement references** returns to ordinary placement.

The start Sketch lies directly at the container origin. Bend has no additional
profile-plane offset or manual Sketch plane selector. Its work plane derives
from placement; the trajectory plane also passes through the origin, while the
end Sketch follows the circular arc endpoint and tangent. Each start endpoint is
referenced to a native edge endpoint and has an initially zero horizontal offset
dimension. Both zero dimensions remain visible. Positive initial offsets shorten
the span inward; a nonpositive resulting width is rejected. Automatic attachment
follows the source sheet thickness on explicit regeneration.

When the attachment edge changes, offset dimensions retain their point identities
and source ancestry. Start/End vertices of Flat share the parent Sketch point;
outer/inner vertices of Bend share the parent profile point. Selecting the
opposite boundary therefore preserves asymmetric offsets on their corresponding
ends, even if the new edge has the opposite tangent direction. Regression tests
use 3 mm and 11 mm offsets, both origins, repeated opposite-edge replacements,
reversed edge parameterization and persistent profile-point identities.

Console attachment uses `bend.create edge_owner=<container> edge_key=<semantic>`.
The same arguments on `bend.set` replace the edge; `origin_last=true/false`
selects one of its two persisted endpoints. Geometry and validation use the same
workspace transaction as GUI OK.

Sheet metadata is stored in native documents, including saved original-reference
packets and assembly/drawing copies. Current format versions are Part INI 36 /
JSON 60, Assembly INI 30 / JSON 42, and Drawing INI 18 / JSON 10. The tracked start templates
use those versions; there is no legacy-format migration path.

**Bend** is available in the Sheet Metal toolbar and contextual **Insert** menu.
Creation and editing use the same internal properties window, OK/Cancel and
middle-button double-click confirmation. Changes remain transient until OK.
Editing evaluates the existing history boundary before the Bend.

- The command consumes ordinary container placement. In manual reference mode,
  selecting a straight outer edge and its narrow planar attachment face puts the
  start profile in that face, its segment along the edge, and thickness into the
  face. The initial sweep tangent leaves the face outwards. This also works on
  the End face of an existing Bend; another perpendicular face is not required.
  Edge direction reversal does not reverse the material side. The profile plane
  is derived automatically and its offset is always zero. Manual container
  placement retains its ordinary reference contract.
- The ordered placement sequence **Edge, containing Plane, Point** uses the last
  point as a station along the edge: its perpendicular projection locates the
  origin, including when the point is the opposite corner of the attachment
  rectangle. The reference row identifies this meaning. A third intersecting
  plane can locate the origin instead. Point-first placement still anchors the
  origin directly to that point. Reference order and identity are persisted in
  the native document; no additional geometry file or cache is required.
- One history container owns three prepared Sketches: the start profile (initially
  a 40 mm segment from 0 to +40), a circular trajectory with an optional tangent
  continuation, and the end profile. Their editors are
  available in the same properties window. The end frame follows the path tangent
  automatically at the end of the arc; it has no independent twist.
- To add a straight continuation, draw one non-construction segment from the
  arc's end in the trajectory Sketch. It must share that endpoint and extend
  forward. OK aligns it with the outgoing tangent, stores a tangent constraint
  and adds an editable length dimension unless the segment already has one.
  Initial H/V inference on that new segment is replaced by its feature-owned
  tangent direction, so a later angle edit does not pin it to a Sketch axis.
  Deleting the segment restores an ordinary Bend. The command remains **Bend**.
  Both original profile Sketches remain at the ends of the arc; the continuation
  uses the end profile's width without further taper. Its length is unchanged by
  angle/radius edits and Unbend. The native auxiliary Sketch stores the segment,
  its endpoints, constraint and dimension; no new format field is required.
- Each profile has one non-construction straight segment. The end editor includes
  a fixed construction copy of the start segment and two endpoint difference
  dimensions. Their initial values are zero. Positive entry preserves the current
  side; negative entry reverses it, following the ordinary Sketch dimension rule.
  The initial positive direction widens each end. CLI extension arguments are
  signed: positive widens, negative shortens. End width must remain positive.
- In manual reference mode both start endpoints have C constraints to the horizontal Sketch axis and
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
  **Custom radius** is unchecked by default: the inside R follows thickness t,
  and both the radius field and path radius are locked. Checking it enables
  manual radius editing, starting from the current effective inside radius.
  Unchecking it restores thickness inheritance. Editing preserves the saved choice.
  The properties preview remains hidden until valid references fix all three
  rotations. Automatic sheet-edge selection supplies that attachment at once;
  manual placement requires at least two independent references. A remaining
  translation along the attachment edge may still be edited numerically.
- Thickness and K factor follow the Part settings. **Local value** enables an
  override for each separately. A missing Part thickness evaluates as 1 mm.
- **Bend** carries the section along the circular trajectory, interpolating the
  endpoint differences across the sweep. Matching profiles use exact revolution;
  a width transition uses a two-section sweep. **Unbend** connects the profiles
  along `angle_radians * (R + K*t)`. It shows an axis at the developed region's
  midpoint. K is a manufacturing input, not inferred from the geometry. The bent
  and flat simplified solids need not have equal volume when K differs from 0.5.
- At exactly zero degrees an arc-only feature contributes no material, keeps its history
  identity and can be edited back to a positive angle. It has no selectable faces
  at that boundary. This avoids a degenerate OCCT solid.
  If a continuation exists, its straight material remains at zero degrees.
  The trajectory editor is disabled at zero angle; change the property angle to
  restore it. The last nondegenerate path retains its curve and point identities.
- An inside radius of zero is supported for **matching profiles**, including a
  180-degree fold. The inner cylindrical face collapses and is absent; surviving
  faces retain their identities. The Hem preset selects this configuration.
  Variable-width R=0 bends and zero-length developments are rejected
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

### Definition-derived dimension direction (2026-09-17)

Linear Sketch dimensions publish their measurement direction separately from
their value and annotation-plane normal. In particular, zero Bend start
coordinates and end-profile differences retain their Sketch X/Y direction.
The common presentation and drag paths consume this direction; Bend does not
have a separate renderer. Existing visibility rules remain unchanged.
Zero angular presentations use the tangent of their coincident rays in the
annotation plane instead of a screen-axis substitute.

Body placement, Assembly occurrences (including nested occurrences), transient
View transforms and Drawing source transforms carry this direction as a vector.
Native viewer packets and Drawing dimension geometry preserve it. The Part and
Assembly format identities and their start templates were updated together.
Focused verification covers zero/nonzero and signed coordinates in rotated
planes, zero-angle presentation, layout dragging, packet persistence and repeated
and nested Assembly occurrences.

Windows GUI and CLI builds succeeded. All 21 selected contracts passed: ten
core contracts (including Bend, Sketcher, Assembly, edge treatments, dimension
layouts and Drawing annotations) and eleven View/layout contracts. The Assembly
test's obsolete Python-format fixture was replaced with a current native nested
Assembly, and its format assertion was updated. Logs are
`build/dimension-direction-core-tests.log`,
`build/dimension-direction-assembly-tests.log` (the corrected Assembly rerun),
and `build/dimension-direction-view-tests.log`.
The actual View captures `build/zero-dimension-planes.png`,
`Projects/test/fillet-purple-grips.png` and
`Projects/test/chamfer-purple-grips.png` were visually inspected.
This is focused verification, not a full repository test-suite run.

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

The 2026-09-17 sheet-role and automatic attachment implementation passed eleven
distinct focused Windows contracts: Flat, Bend, Holes, Profile, Assembly, UI,
translations, occurrence reference transforms, Bend attachment GUI, automatic
attachment dialog and Drawing source picker. The rebuilt GUI tests exercise
actual Flat-edge picking, creation/editing, all three Sketcher roundtrips and
native reopen. Core tests cover all eight rectangular Flat boundary edges,
both endpoints, 0/90/180-degree angles, inherited thickness, persisted topology
parents, and unequal 3/11 mm offsets across edge and endpoint replacement and
reversed parameterization. Automatic Bend-to-Bend attachment also checks both
endpoints in sixteen rotated/unrotated and reversed-edge source cases.

Logs: `build/sheet-drawing-ui-tests.log`, `build/sheet-drawing-regression.log`
and `build/sheet-bend-final-tests.log`. The wider Drawing contract still fails
its pre-existing shared title-block dimension check
(`Shared 10mm master dimension cannot drive its equal lengths`); the new
Drawing source-picker contract passes. This is focused verification, not a
claim that the entire repository suite passes.

The subsequent properties refinement passed six focused contracts across
`build/sheet-preview-confirm-tests.log`, `build/sheet-settings-confirm-tests.log`
and the corrected final rerun `build/sheet-settings-final-tests.log`: Bend
attachment GUI, shared UI, Flat, Bend, metadata commands and application-tools
GUI. Coverage includes the new inherited-radius default and Cancel reset,
hidden unattached preview annotations, unchanged OK restoring missing body data
without an extra Undo entry, and immediate thickness/K-factor calculation with
atomic settings/geometry Undo and Redo. Earlier failed expectations for a 5 mm
default radius and deferred sheet-settings calculation were updated to the
requested behavior. The local Windows GUI was rebuilt successfully.

The subsequent Flat-to-Bend attachment change passed ten focused Windows
contracts in 88.55 seconds (`build/flat-attachment-final-tests.log`): Flat,
Bend, automatic attachment dialog, shared UI, Sketcher, Assembly, Sketch
references, profiles, application tools and Bend attachment GUI. The final
profile-plane visibility refinement passed both affected GUI contracts again
in 12.15 seconds (`build/flat-attachment-ui-final-tests.log`).

Coverage includes 35/90/180-degree source bends, both folded and unfolded
states, 1/2 mm thickness, rotated source frames and reversed edge
parameterization. Native endpoint identities survive save/reopen. Full history
tests switch an attached Bend between the two opposite edges of a Flat, extend
its width from 40 to 52 mm and check the endpoint-driven Flat and resulting
volume. A zero-angle source correctly breaks the solid-edge attachment and
restoring 90 degrees resolves it again. Independent attached thickness is
rejected without changing history; Undo/Redo restores calculated geometry.

The real application test selects a Bend End boundary, draws the attached
rectangle, enters and leaves Sketcher, confirms Flat and reopens its native
definition. The capture `build/flat-bend-attachment.png` was visually inspected.
Windows GUI and CLI builds succeeded; the normal development launcher remains
`zima-cad.bat`. This is focused verification, not a complete repository suite.

### External point contacts and localized Bend state (2026-09-17)

Sketch creation preserves complete external reference identifiers, including
colon-separated attachment endpoint IDs. Endpoint/corner snapping uses the native
point reference relation. A segment or rectangle side can infer C (incidence)
or M (midpoint) against an external point. These relations retain an associative
construction point and follow refreshed source coordinates. Broken references,
disabled inference relations and the viewer selection filter exclude candidates.

Double-clicking the Bend state annotation opens an inline choice between the
localized bent and unbent states. Choosing a state calculates and commits it;
dismissing the selector leaves the state unchanged. The View annotation,
Properties and Family Table use the same localized state names without English
parentheses in Czech.

### Initial 03.prtz constraint diagnosis (resolved below) (2026-09-17)

The five-feature example is distinct from the earlier trajectory issue.
Before the material-state fix below, the second Bend could unfold, while unfolding the first Bend conflicted with a C relation
in the last Flat: its rectangle corner references a side face of the Flat after
the first Bend. That face intersection changes from the horizontal line Y=150 mm
to the vertical line X=360.808873 mm. The rectangle's attached edge fixes the
corner's X coordinate at 204.384094 mm, so both requirements cannot hold.
Removing only this C relation in a disposable copy allows the first Bend to
unfold. The original document is unchanged; the application must not silently
remove a user's geometric constraint to force a state change.

The subsequent identity audit confirms that all five Sketch IDs, native segment
IDs and point IDs remain unchanged across the diagnostic state change. Both
Bends reference distinct base edges; each edge occurs exactly once in persisted
original reference geometry. The C source face retains its owner and semantic
key, and its original rectangle-side ancestry. Its evaluated world plane rotates
from Y=159 mm to X=275.248936748 mm. `project_external_face_plane` intersects that
current world plane with the consuming Sketch, which changes the effective C
condition from a free height coordinate to the attachment-controlled width.
This is a state-evaluation limitation, not evidence that the user's box design
or its cross-branch design relationship is wrong. A systematic solution must
preserve that design relationship across folded/unfolded states.

The box requirement also distinguishes intended attachment through the base
from unintended joining between neighboring free walls. Current additive solid
operations use general OCCT Fuse; there is no dedicated free-corner joining
policy. Original reference meshes are captured from each feature operand before
that fuse, so this particular C failure is not caused by fused result topology
being mistaken for the source face. The material-state regression below now checks free-wall corner separation for
this box; it does not establish a general corner-contact policy.

### Material-state reference evaluation (2026-09-17)

An unfolded Part first calculates its folded design reference state during the
explicit calculation transaction. The same native Sketches, constraints and
source identities are evaluated against this design state, then used by the
requested Bend states. Unfolding does not replace the referenced rectangles or
silently remove their cross-branch relations. Changing a source dimension still
updates dependent Sketch geometry; reference coordinates are not frozen values.

The Part persists `sheet_reference_state` with original design reference geometry,
Sketch frames and the owning Body frames. Reference creation for existing Sketches
and refreshing their reference snapshots consume this native data without OCCT.
No required sidecar or external geometry cache is introduced. Fully folded Parts
store an empty snapshot. Part JSON 59 / INI 35 and Assembly JSON 41 / INI 29 are the
current schemas; both tracked start templates have been updated.

Regeneration does not project an old calculated source mesh into a newly resolved
Sketch frame. It advances the geometry pass first. Equivalent projections within
the Sketch solver tolerance do not oscillate between folded and displayed frame
rounding differences. The shared container placement solver is unchanged.

`cpp/tests/fixtures/sheet/box-cross-branch.prtz` retains the five-feature box and
its cross-branch C constraint, without cached bodies. The Bend regression checks
all four state combinations, repeated regeneration, stable corner/segment/reference
identities, unchanged constraints, source wall height changing from 150 to 175 mm
while unfolded, native save/reopen, reference refresh/recreation and Undo/Redo.
It also rejects a result edge joining faces belonging to both free walls.

Verification: the 11 selected suites passed after updating the general bounded-axis
fixture to supply its persisted axis explicitly. The production placement solver
was not changed for that fixture. See `build/sheet-state-final-tests.log` (10 passing
suites and the old fixture failure) and `build/sheet-state-contract-test.log` (the
corrected general contract suite passing). Windows GUI and CLI were rebuilt.

A separate native review copy, `Projects/03-overeno.prtz`, was saved through the
CLI after toggling both Bends and returning them to the folded state. At that checkpoint it used INI 34 and an independent document namespace; local reference document
IDs were remapped by the native Save As operation. The original `Projects/03.prtz`
remains unchanged. `build/03-unbend-check/verified-results.txt` records the actual
state-change commands and their successful results.


### Hem preset and the 180-degree View edit check (2026-09-17)

Bend Properties exposes a localized **Hem** checkbox. It sets the existing
parameters to 180 degrees, zero inner radius, and an independent radius. The
outer radius remains the material thickness. Angle and radius controls are
unavailable while this preset is active. Disabling it restores the previous
pending settings; reopening a saved hem and disabling it starts from 90 degrees
with the radius linked to thickness. No extra persistent mode or format change
is needed. Cancel retains the stored feature.

The GUI regression covers the preset, confirmation, reopening, and direct View
angle editing to 180 degrees. Existing geometry tests cover a zero-radius hem,
its developed state, and rejection of unsupported unequal start/end widths.
The three Bend, Flat, and application-tools suites passed in
`build/bend-hem-tests3.log`.

### Reference failures belong to their history owner

External-reference refresh follows the same recovery contract as body calculation.
A later Sketch constraint conflict no longer rolls back an earlier valid feature
edit. Reference refresh retains the authored Sketch and constraints, records a
persisted diagnostic on its history owner, and emits an errored kernel operation.
The normal recovering history evaluation retains the valid preceding geometry
and removes the failed feature contribution. The existing Tree error presentation
marks the feature red and exposes its diagnostic. No placement contract changes
or automatic constraint deletion are involved.

Validation of an edited feature occurs after reference/placement convergence,
not against an intermediate geometry pass. Property edits continue to validate
the edited feature at their existing rollback boundary; later failures remain
visible diagnostics. Regeneration retries failed references so that correcting
an upstream parameter restores geometry and clears errors automatically.

The current native format is Part INI 36 / JSON 60 and Assembly INI 30 / JSON 42.
`reference_errors` is stored inside the native Part and participates in operation
fingerprints. Failed states therefore reopen with their diagnostics and the
correct valid-prefix geometry. Both start templates are updated.

Regression coverage uses the original box's cross-branch C reference, including
180-degree View-style edits, unchanged attachment identities, disappearance of
the failed wall, native cache reload, recovery at 90 degrees, and Undo/Redo. GUI
coverage checks the actual inline edit and the red Tree row. The contact is never
removed or converted to a height relation in the user document.


Recovery also restores the original reference packet when the kernel reuses a
shorter prefix from a compacted full-history cache. Only owners in that prefix
are exposed. This prevents a valid preceding attachment from losing its source
references while repairing a later failure; the shared placement solver remains
unchanged. A separate kernel regression covers this compacted-prefix case.


The updated local review copy is `Projects/03-overeno-1704.prtz`, created through
native Save As after the current user's source passed 90 -> 180 -> 90 degree
changes. It has its own document identity and current format. The original
`03.prtz` and the user's `03-overeno.prtz` remain unchanged. Evidence is in
`build/review-1704/results.txt`.


Final verification for Windows 2026091704: all thirteen selected suites passed
across `build/release-1704-recovery-tests.log` and
`build/release-1704-final-check-tests.log`. This includes actual View editing,
Tree diagnostics, regeneration recovery, saved failed states and kernel prefix
reference retention. Earlier failed attempts remain diagnostic logs only.

## Tangent continuation verification (after Windows 2026091704)

The optional straight continuation is a local development change after the
published 2026091704 package. The existing root `zima-cad.bat` launches the rebuilt
development application; the published archive remains unchanged.

Eight distinct focused suites passed across
`build/bend-continuation-final-tests.log` (six suites, 105.61 seconds) and
`build/bend-continuation-final-tests2.log` (four suites, 70.72 seconds, two repeated).
The latter run verifies the final direction and inference handling. Coverage
includes actual Properties acceptance and View length editing, Flat attachment,
Sketcher, recovering history, general kernel contracts and 3D sweeps.

The Bend regression checks analytic volumes at 0, 45, 90 and 180 degrees,
180-degree entry through the actual Sketch angle dimension, unchanged continuation
length through property/state changes, Unbend, zero-inner-radius Hem, native
save/reopen, deletion and Undo/Redo. Original arc face identities remain present;
the new end exposes a native sheet boundary edge. Both arc profile Sketches keep
their original placement and identity. New H/V line inference is replaced by T;
the prepared forward direction resolves the tangent solver's opposite branch
when changing angle. An existing authored length dimension is reused.

The GUI acceptance screenshot is `build/bend-continuation-view.png` and was
visually inspected. Earlier failed exploratory test logs are superseded by the
final passing runs. This is focused verification, not a full repository test run.


## Sheet Revolution

The Sheet Metal toolbar and Insert menu expose **Revolution** (`Rotace`). It
uses one owned Sketch containing one non-construction profile segment and an
oriented construction centerline. The initial profile is 40 mm long, with a
parallel axis 10 mm away. Edit the same Sketch to reposition or incline the axis;
there is no circular trajectory Sketch, end-profile Sketch or offset plane.
A parallel profile/axis produces a cylindrical sheet; an inclined pair produces
a conical sheet. The complete section, including thickness, must remain on one
side of the axis. The initial 1 mm, 90-degree cylinder has volume `190*pi mm^3`;
this independent analytical measure is covered by regression tests.

Free placement inherits Part sheet thickness unless **Custom thickness** is
checked. The existing Revolution controls select the first side, opposite side
or symmetric thickness, independently from the forward/reverse rotation and
one-sided, two-sided or symmetric angular extent. Angle limits match Modeling
Revolution, including a 360-degree total maximum.

Selecting a straight native sheet boundary in the first placement reference
uses the same edge, joining face and endpoint contract as Bend. The profile's
endpoints become inherited external point references with editable end offsets.
Thickness and its material side follow the selected sheet. The axis remains
editable in the one Sketch. Thickness edges are excluded from both offering and
confirmation. Derived joining-face/orientation controls cannot be independently
replaced in attached mode.

The operation reuses the native Revolution definition and calculation, with
explicit `sheet_metal`, `sheet_attachment` and `thickness_override` fields. It is
additive and Part-owned, including when editing a Part within an Assembly.
Assembly cutter creation cannot reinterpret it as a subtraction. Principal
faces and thickness faces carry sheet roles derived from authored thin-profile
ancestry; the viewer does not classify them through live kernel traversal.
The CLI `revolution.create` and `revolution.set` expose `sheet_metal` and
`thickness_override` alongside the existing profile/axis/angle arguments.

Current native versions are Part INI 36 / JSON 60 and Assembly INI 30 / JSON 42.
Both start templates are updated. Drawing remains INI 18 / JSON 10. No required
sidecar is introduced. Unfolding Sheet Revolution is deliberately not implemented
in this step; cylindrical and conical developments need a separate design.

### Sheet Revolution verification

The Windows native build and ten focused suites passed. The initial three-suite
run is recorded in `build/sheet-revolution-tests.log`; seven additional suites
are recorded in `build/sheet-revolution-regression-tests.log`. After extending
GUI coverage, the application-tools suite passed again in
`build/sheet-revolution-ui-tests.log`.

Checks cover analytical cylinder/cone volumes, 90/180/360-degree rotation,
angular extent modes, thickness sides and inheritance, persisted sheet face
roles, attachment to a Flat boundary, inherited endpoint references, native
save/reopen, rejection of subtraction and Undo/Redo. The actual application
dialog additionally exercises free creation, edge attachment, entering and
leaving the owned Sketch, property reopening and Cancel without committing
pending thickness. The acceptance image `build/sheet-revolution-view.png` was
visually inspected. Regression coverage includes Bend attachment, Flat,
Sketcher, history recovery, general geometry, sweeps and Assembly persistence.
This is focused verification, not a full repository test run.

### Attached Revolution Sketcher frame correction

The initial Sheet Revolution implementation calculated the attached profile and
wire preview correctly but entered its new owned Sketch through a generic Sketch
history carrier. That carrier discarded the sheet-specific edge/material-side
frame policy and resolved a different editing plane. Free creation did not expose
the discrepancy.

Entering the owned Sketch now retains the pending Sheet Revolution as its
transient owner. Its existing document resolver supplies the same frame as the
rotation calculation. This is a feature-specific carrier correction; it does not
change the shared placement solver or commit model geometry on Sketcher entry.
The GUI regression compares the displayed X/Y axes and their origin against the
resolved attached profile both during creation and after reopening Properties.
It reproduced the defect before the fix in
`build/sheet-rotation-frame-before.log`.
After correction, all five suites in
`build/sheet-rotation-frame-fixed-tests.log` passed (212.39 seconds): Sketcher
return frames, Bend attachment, profile commands, application tools with the new
creation/reopening frame assertions, and Bend/Sheet Revolution calculations.
The local Windows executable was rebuilt successfully; this verification does
not constitute a new portable release.

### Subsequent unfolding design

The next design should derive a neutral surface from the authored profile, axis
and sheet thickness, and distinguish cylindrical and conical cases. Flat output
must retain explicit ancestry to the same profile endpoints and sheet sides;
changing folded state must not substitute unrelated reference owners. The
neutral-surface rule, seam for a complete revolution and behavior of attached
downstream features require separate implementation and verification. No unfold
result or manufacturing allowance for this feature is claimed by the current
geometry-creation tests.
