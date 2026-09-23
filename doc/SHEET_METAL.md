# Sheet Metal

See [Sheet from Body and flat-pattern DXF](SHEET_EXCHANGE.md) for independent
solid-to-sheet reconstruction and the active Body's direct manufacturing export.

See [Sheet Profile corner closure](SHEET_CORNER.md) for the optional curved
transition at either endpoint, its gap allowance and verification.

The current material-creation commands are **Flat**, **Sheet Profile**,
**Revolved Sheet**, **Twisted Sheet** and **Sheet transition**, followed by **Sheet Cut**, **Unbend**
and **Bend Back**. Earlier sections use Bend and
Sheet Revolution for the latter two creators; their internal feature and CLI
identifiers remain unchanged. See [Sheet Cut and material-space boundaries](#sheet-cut-and-material-space-boundaries-2026-09-17)
for Sheet Cut and current naming. The separate state operations are described in
[Sheet state history operations](SHEET_STATE_DEVELOPMENT.md). They replace the
former per-profile folded-state switch throughout the GUI, CLI and native model.

**Sheet transition** owns two editable Sketches: a semicircle and an open rounded
half-rectangle. Its ordinary placement controls position the main Origin; a second
nested Origin provides relative XYZ translation and rotation. Two SKETCH buttons
open the seeded profiles. It creates editable
planar panels and finite-radius bends, with inward thickness and neutral-layer
development. See [Sheet transition](TRANSITION_SURFACE_PROTOTYPE.md#native-sheet-transition-command)
for inputs, supported orientations and verification. Its inner-skin bend axes
are available through the same Drawing Show/Erase mechanism.

Unbend and Bend Back process all eligible regions by default. Check **Select
individual features** to pick a subset in the View or Tree; a second click
on a selected feature removes it from the list. All and individual selection
are mutually exclusive checkboxes; the active mode cannot be unchecked without
selecting the other mode. The developed result displays a bend axis
on the inner skin of each Sheet Profile and Revolved Sheet, halfway through its
angular span. Use Drawing **Show/Erase > Axes** to display the line and attach a
drawing dimension to it. A cone's line follows the middle generator of its
developed sector. The original rotation axis is hidden while the Revolved Sheet
is developed and restored by Bend Back.

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
- **Sheet Cut tolerance** is the permitted calculation deviation, initially
  **0.05 mm**. Its configuration and native document storage are described in
  [Sheet Cut](#sheet-cut-and-material-space-boundaries-2026-09-17).

Defaults are stored in the native Part. Thickness uses the shared document
parameter `SHEETMETAL_THICKNESS`; K factor uses the existing material property,
with dimensionless unit `1`. These are existing native metadata stores; no file
format change, sidecar, or template conversion is needed. Loading another material
can change its K factor while leaving the document thickness intact.

OK commits the settings together. Cancel discards pending edits. The shared
properties-window behavior includes confirmation by middle-button double-click
over the owning View. Undo/Redo and native save/reopen retain the settings.
Changing the material defaults calculates Parts containing Flat, Sheet Profile
or Revolved Sheet before committing
the settings and geometry together. Calculation errors leave the previous state
intact. Parts without sheet features keep their calculated geometry. The settings
belong to the active Part source, including while editing it inside an Assembly;
Assembly mate solving and Assembly-owned operations still require their own
explicit regeneration.

Sheet Metal Properties is an editing shortcut, so it is absent from **Insert**.
Assembly file settings have no Sheet Metal page.

## Twisted Sheet

The 2026-09-19 attachment correction stores `attachment_material_side` in the
native feature. The joining face determines this side independently of endpoint
selection and twist direction. Preview, solid loft and material-space mapping
use the same side. The native Part format is now INI **41** / JSON **65**;
the start Part was regenerated with the current serializer and verified through
the GUI New-document path. Assembly and Drawing formats are unchanged.
Earlier Part formats are unsupported; no migration branch was added.

The reported profile-side example is retained as
`cpp/tests/fixtures/sheet/profile-side-twist.prtz`. Its starting section now
occupies Y=1..2, matching the parent thickness face, instead of Y=2..3.
Regression checks cover both endpoints, zero through three preceding Sheet
Profiles, Unbend, Bend Back and persisted state restoration. The source project
was left unchanged; a calculated repair is available locally at
`Projects/test/01-twist-repaired.prtz`.

Rotated Sheet Cut tests independently verify the removed volume of a rectangular
projection through a plane as profile area times thickness divided by the
absolute direction/normal dot product. Cases include a Z rotation, combined XYZ
rotation, both projection directions, saved placement and cold regeneration.
A jointly rotated cylindrical sheet/cut also passes Unbend, native reopen,
cold calculation and Bend Back. These checks required no Sheet Cut product-code
change. Fifteen distinct focused native/GUI contracts passed. Evidence:
`build/twist-final-native.log`, `build/twist-cut-final-tests.log`,
`build/twist-final-regressions.log` and `build/twist-final-ui-cut.log` (its GUI
case passed; its earlier test-fixture filename collision is superseded by the
separate passing final cut log).

**Twisted Sheet** is a direct parametric sheet feature and does not open
Sketcher. Free placement uses the ordinary container placement contract. The
feature creates a rectangular strip centered on its local axis from width,
twist length, total twist angle, twist direction and thickness. Thickness is
symmetric about the neutral surface.

Selecting a straight sheet boundary edge in the first placement row switches
to attached mode. Thickness and width are inherited from the source edge and
become read-only. The selected physical edge is offset to the middle of the
source thickness, so the twist axis passes through the neutral start line. The
edge, its joining thickness face and one persisted endpoint provide the same
unmodified placement references used by the other sheet attachment commands.
Thickness edges are rejected.

Creation shows no strip or operation-axis preview until the first placement
entity is entered. Changing dimensions alone does not reveal an unplaced strip.
Editing an existing feature displays its preview immediately. The operation
axis runs through the starting section's centre; the placed Origin stays on
the selected source endpoint.

The cyan preview is analytical and never invokes OCCT. A real strip must be
clamped at both ends, so its longitudinal boundaries leave and enter the end
faces tangent to the straight twist axis. The authored twist law uses equal
smooth transition zones at the start and end, a constant-rate middle zone and
zero twist rate and acceleration at both clamps. OK interpolates the sampled
sections as one smooth loft; its longitudinal boundaries are BSplines rather
than a chain of short edges. Sampling remains bounded by the document's sheet
tolerance. The authored start/end profiles, their four points and the output
boundary edges have semantic ZIMA identities; sample indices and OCCT traversal
order do not define persistent identity. The output boundary can therefore
drive another sheet feature.

Unbend flattens Twisted Sheet using a documented manufacturing approximation.
A twisted planar strip is not an exactly developable cylinder or cone, so the
initial developed length is the mean longitudinal-fibre length on the neutral
surface across the complete strip width and through the authored transition
law. **Flat pattern correction**
is a signed millimetre value stored directly in Twisted Sheet and added to that
calculated length. The properties window displays the resulting developed
length. This lets a measured trial part correct the next flat pattern without a
hidden material coefficient. Bend Back rebuilds the authored formed feature,
so repeated state changes do not accumulate the correction or numerical drift.

For formed axial length `L`, width `W`, half-width `h = W/2`, total twist in
radians `a` and normalized authored angle progress `p(t)`, the neutral-fibre
rate at width coordinate `x` is
`sqrt(1 + (x a p'(t) / L)²)`. The implementation integrates this expression
analytically across the width and with a fixed Simpson rule along the length;
its zero-angle limit is `L`. The signed flat-pattern correction is added after
this calculation. Unbend still produces a rectangular blank. Sheet Cut accepts
the formed BSpline skins as well as the flat state, and Bend Back maps complete
intervening cut boundaries through the same forward material law.

## Edge attachment and unfolded presentation

Flat, Sheet Profile, Revolved Sheet and Twisted Sheet consume an eligible sheet
boundary through the common View candidate stream. Hover and LMB therefore use
the active selection filter and confirm the same edge. One confirmed edge is an
atomic attachment input: the feature derives and persists the boundary edge,
joining thickness face and selected endpoint together. The generic container
degrees-of-freedom filter must not reject that edge before the sheet command can
construct this complete reference set. Changing the endpoint reverses the span
along the same physical edge without changing the joining plane.

Unbend and Bend Back remain history-state operations rather than ordinary
selectable model features. Their calculated topology carries the authored sheet
feature as its display owner, so ordinary hover, LMB confirmation and cyan
inspection continue to select the original feature while drawing its current
formed or unfolded wire. When that feature is unfolded, View double-click and
the **Edit** dimension action are disabled because its stored dimensions belong
to the formed historical geometry. **Properties** remains available and uses
the normal history rollback to display and edit the feature at its authored
boundary.

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

### Automatic Flat attachment to sheet end faces

Flat retains ordinary free placement. Selecting a straight outer boundary of a
Sheet Profile or Revolved Sheet Start or End cap in the first reference field
activates automatic attachment. The boundary must belong to the persisted
Side A skin and a terminal thickness face. This includes straight generators
at the ends of cylindrical and conical Revolved Sheets. Inner boundaries,
curved rim edges and thickness edges are excluded from this shortcut. Hover
and confirmation use the same eligibility check and still obey the user's
selection filters.

The edge, its joining cap and a native endpoint fill the placement references.
The profile lies in the outer tangent plane and its positive Y direction leaves
the cap. Thickness defaults to **Second side**, toward the inner radius, and
inherits the source sheet thickness. The profile remains an ordinary editable
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

Flat attachment to Revolved Sheet is covered by cylinder/cone cases with both
terminal caps, both axis orientations, both rotation directions and all three
thickness modes. The checks change the source angle from 35 to 180 degrees,
reverse the edge parameterization, retain endpoint identities through native
save/reopen, and independently check the tangent frame and inward thickness.
Inner and thickness edges remain rejected; a full 360-degree revolution has no
terminal cap to attach to. The application-tools test also offers the actual
terminal edge through the common hover list with the Curves filter, confirms
it by LMB, enters and leaves Sketcher, commits and reloads the resulting Part.
All 48 native combinations and existing Flat/Bend tests passed in
`build/flat-revolution-attachment-tests.log`. The application-tools UI contract
passed after the final GUI rebuild; `build/flat-revolved-sheet-attachment.png`
was visually inspected.

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
packets and assembly/drawing copies. Current format versions are Part INI 38 /
JSON 62, Assembly INI 32 / JSON 44, and Drawing INI 18 / JSON 10. The tracked start templates
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
  a 40 mm segment from 0 to +40), a circular trajectory with a tangent straight
  continuation, and the end profile. A new Sheet Profile starts with a 20 mm
  continuation, its tangent constraint and a driving length dimension. Their editors are
  available in the same properties window. The end frame follows the path tangent
  automatically at the end of the arc; it has no independent twist.
- To restore a deleted straight continuation, draw one non-construction segment
  from the arc's end in the trajectory Sketch. It must share that endpoint and
  extend forward. OK aligns it with the outgoing tangent, stores a tangent
  constraint and adds an editable length dimension unless the segment already has one.
  Initial H/V inference on that new segment is replaced by its feature-owned
  tangent direction, so a later angle edit does not pin it to a Sketch axis.
  Deleting the segment restores an arc-only Sheet Profile.
  Both original profile Sketches remain at the ends of the arc; the continuation
  uses the end profile's width without further taper. Its length is unchanged by
  angle/radius edits and Unbend. The native auxiliary Sketch stores the segment,
  its endpoints, constraint and dimension; no new format field is required.
  Either long boundary edge of that tangent continuation is a valid automatic
  attachment for another Sheet Profile. The shared arc-to-line station has one
  persisted point identity, so both longitudinal edges retain unambiguous endpoint
  references. A downstream profile follows continuation-length and angle edits,
  native save/reopen and fresh regeneration without rebinding its selected edge.
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
- **Sheet Profile** carries the section along the circular trajectory,
  interpolating endpoint differences across the sweep. Matching profiles use
  exact revolution; a width transition uses a two-section sweep. Separate
  **Unbend** uses `angle_radians * (R + K*t)` for its material coordinate map.
  K is a manufacturing input. Simplified bent and flat solids need not have
  equal volume when K differs from 0.5.
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

Start and End face/rim identities derive from the feature, section, source
segment and endpoints. Sheet Profile always retains its original authored
geometry. Unbend and Bend Back publish new original children with explicit
ancestry; they do not overwrite that source geometry or its placement.
Neither OCCT traversal order nor preview edges define persistent references.

Double-click the Sheet Profile in View to inspect its dimensions. Radius, angle
and endpoint differences support inline editing. All three Sketch editors remain
pending until the owning properties window is confirmed. Cancel discards them
together. State changes use separate history commands, with their own OK/Cancel
transaction and selected-region list.

Ordinary circular body edges remain available to the Drawing radius-measurement
command. For a width transition, the side edges need not be circles: the authored
outside path radius and the endpoint dimensions are available through Drawing
Show/Erase model dimensions. The original path radius annotation is omitted at zero angle. Do not interpret a width-transition edge as a circular arc.

A circular Bend with matching start/end sections retains analytic lines and
cylindrical surfaces throughout the exact revolution calculation. Converting
those sections to NURBS unnecessarily produced general revolution surfaces and
could stall volume integration for a rotated 180-degree Bend. Variable-width
sections continue to use the general Sweep calculation.

Native Part INI format **30** / JSON payload **54** stores Flat and Bend parameters, the
owned Flat profile, Bend start Sketch and both embedded Bend Sketches;
`config/templates/START_PART.prtz` uses that format. Earlier Part
formats are intentionally unsupported. Assembly format is unchanged.

Console commands `bend.create`, `bend.get` and `bend.set` use the same workspace
transaction as the GUI. Creation accepts `width_mm` (default 40), `radius_mm`,
`angle_degrees`, `thickness_mm`, `k_factor`, `thickness_override`,
`k_factor_override`, `radius_follows_thickness`, `first_extension_mm`,
`last_extension_mm`, `name` and `document`.
Editing uses `container` and the same parameters except width; edit the owned
Sketch to change width. Supplying thickness or K enables its override unless the
corresponding override flag explicitly says false. Readback reports effective values.

```json
{"command":"bend.create","arguments":{"width_mm":40,"radius_mm":5,"angle_degrees":90}}
{"command":"unbend.create","arguments":{"owners":["<profile-id>"]}}
{"command":"bend_back.create","arguments":{"all":true}}
```

## Reference study (2026-09-16)

The earlier per-profile switch exposed conflicting cross-branch Sketch
references when it moved original geometry. The replacement state operations
preserve original geometry and references, transport derived material regions,
and restore authored frames from their source values. The detailed current
contract and precision checks are in [Sheet state history operations](SHEET_STATE_DEVELOPMENT.md).

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

Family Table selects the presence of the separate Unbend feature through the
ordinary yes/no feature binding. Tests cover independent instances, inheritance,
instance edits, generic changes and cold native reopening. The old numeric
per-profile state parameter and its special cell editor have been removed.

The Bend command contract checks analytical volumes for ordinary, variable-width,
flat and zero-radius bends; actual Sketch radius/angle/difference edits; inherited
and overridden parameters; the radius/thickness link; referenced Start/End planes;
stable surviving face identities; rejected edits; zero angle; Undo/Redo; native
save/reopen; Drawing radius measurement and model annotation; and Family Table
variants. At 180 degrees cap matching checks the authored section boundary as well
as its plane, including when the R=0 caps touch along the fold axis.

The application-tools GUI contract opens all three editors, returns to pending
Properties, checks Cancel and OK, edits the first history feature, confirms with
middle-button double-click and edits radius, angle and both
endpoint dimensions inline. Separate state-command interaction has its own GUI
contract, including repeated Part occurrences in an Assembly. Captures: `build/bend-properties.png` and
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

### External point contacts (2026-09-17)

Sketch creation preserves complete external reference identifiers, including
colon-separated attachment endpoint IDs. Endpoint/corner snapping uses the native
point reference relation. A segment or rectangle side can infer C (incidence)
or M (midpoint) against an external point. These relations retain an associative
construction point and follow refreshed source coordinates. Broken references,
disabled inference relations and the viewer selection filter exclude candidates.

State changes now use **Unbend / Rozvinout** and **Bend Back / Ohnout zpět**.
The per-profile state annotation, inline state chooser and Family state parameter
have been removed. They no longer mutate earlier geometry.

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

The current native format is Part INI 38 / JSON 62 and Assembly INI 32 / JSON 44.
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

Current native versions are Part INI 38 / JSON 62 and Assembly INI 32 / JSON 44.
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

### Revolved Sheet development

The separate Unbend/Bend Back operations support open cylindrical and conical
regions. Cones use an annular neutral-surface sector. Closed 360-degree regions
need a seam definition and are rejected. See [the state-operation contract](SHEET_STATE_DEVELOPMENT.md).

## Sheet Cut and material-space boundaries (2026-09-17)

The material-creating commands are now labelled **Sheet Profile** (formerly
Bend) and **Revolved Sheet** (formerly Sheet Revolution). Their internal feature
kinds and command identifiers remain unchanged. State changes are separate
Unbend and Bend Back history operations.

**Sheet Cut** is a Part-owned history operation in the Sheet Metal toolbar and
Insert menu. Create its placement, enter the owned Sketch, draw a closed profile,
and confirm the shared properties window. As with subtractive Extrusion, choose
the direction, one-sided/two-sided/symmetric extent, and Length, Up To or Through
All ending. The profile is projected within that range onto the reference side
(Side A) of intersected sheet regions
in the current Body. Each projected domain is carried normally through the full
local sheet thickness. It supports planar, cylindrical and conical source skins,
including Sheet Profile and Revolved Sheet. A profile missing every supported
sheet region is rejected without committing a history item.

Sheet Cut is valid after **Unbend**. The cut is authored in the developed
material state, retained as a subtractive material contribution, and mapped back
by a later **Bend Back**. Repeating Unbend returns the same developed cut within
the configured sheet-state tolerance. This follows the same history contract as
an ordinary subtractive Extrusion while preserving Sheet Cut material metadata.

The **Cut method** selector provides two normal-through-thickness calculations:

- **By surface** is the original method and the default. The authored footprint
  belongs to Side A. Its continuation follows the local surface normal, so the
  opening on the opposite skin need not admit the entire original Sketch prism.
- **Profile clearance** removes the complete material fibres needed for the
  original profile to pass through the opening. It includes intersections at
  intermediate thickness depths, preserving islands where they still fit.
  The calculated boundary is shared through thickness; the operation does not
  substitute an ordinary oblique solid cut.

In Profile clearance mode, the selected projection range identifies connected
wall intersections. A reached wall is cut through its entire thickness even
when the requested endpoint lies inside it or the projection approaches Side B.
Disconnected wall intersections beyond that range remain untouched.

The agreed default maximum calculation deviation for Sheet Cut is **0.05 mm**.
This is a permitted geometric approximation, not a mandatory enlargement of
every opening, a manufacturing guarantee, or a change to the modeler's general
linear tolerance. The calculation should use the simplest construction that
meets this bound and still produces a valid solid with the requested profile
clearance. Tighter calculations are not required merely to pursue additional
decimal places. Live wire estimates remain separate from the final body
calculation and do not carry this accuracy guarantee.

The application default is `SheetMetal/CutTolerance` in `config/config.ini`
(millimetres, default `0.05`). A malformed application value logs a warning and
uses `0.05` rather than preventing startup. A new Part, including a source Part
generated by Assembly STEP/IGES import, copies it into its native document
precision setting, `sheet_cut_tolerance`. Opening a Part uses its saved value;
changing another computer's application config cannot silently change the
Part's next calculation. The value is editable in File Settings on the Sheet
Metal page, or through `document.settings.set` with
`precision.sheet_cut_tolerance`. It must be finite and between `0.000001` and
`1` mm; invalid document settings are rejected. Confirming a change recalculates
affected Sheet Cut features; opening the dialog or cancelling does not
calculate or change the document.

The interactive performance target is **1–2 seconds for a typical individual
Sheet Cut** on the development machine. Report that operation separately from
file loading, saving, and regeneration of earlier history. This is a target,
not a guarantee for arbitrarily complex profiles or bodies. The lightweight
wire preview must not wait for the body calculation.

The cyan wire preview shows both the profile projection range and the estimated
cut boundaries on the reached sheet regions. Both use the ordinary cyan wire
style; the preview does not calculate, replace, or shade a result solid.
It consumes persisted surface and viewer data, caches its derived wire, and
updates only when the pending geometry or parameters change. Hover and painting
do not calculate cut geometry. The result body is calculated only after OK or
explicit regeneration; there is no OCCT calculation in the live preview.
The estimate uses the existing surface tessellation and at most five thickness
samples for Profile clearance. A nonplanar Up To target uses its displayed
envelope. Explicit facet, profile-point and interval-work limits bound the
transient calculation, including meshes outside the cut footprint and densely
slotted profiles parallel to the projection. An incomplete or excessively
complex profile retains only the projection wire; partial or stale cut
estimates are never shown. Empty estimates are cached as well, so repeated
view updates do not retry an over-budget calculation. Existing calculated cut
edges can be reused unchanged when opening Properties. Each dialog consumes the
active Body's local input and the existing occurrence transform exactly once.
The shared purple direction and extent controls support direction reversal and
dragging a numeric length. Up To uses the ordinary target-reference picker. Creation
and editing reuse the existing profile dialog, rollback and confirmation rules.
Length limits which sheet regions the projection reaches; it does not create
a partial-thickness pocket. There are no Surface, Thin or add-material modes.
One operation can cut several disconnected sheets in the current Body.
Assembly-owned cuts remain ordinary solid operations; activate a source Part to
create a Sheet Cut.

### Calculation and ownership

1. Build an ordinary projection prism from the authored Sketch and its selected
   direction, extent and end conditions.
2. For By surface, intersect that prism with each actual input Side A face.
   For Profile clearance, identify the connected wall passages reached by that
   range, then determine the complete normal-fibre envelope of the profile
   through those passages. Verify the envelope against the required passage
   volume before accepting it.
3. Record the trimmed surface domain and offset it into the material by the
   source sheet thickness to create a closed cutting tool.
4. Subtract the resulting tool collection from the current body.

For a cylinder, the thickness traversal is radial, rather than parallel to the
Sketch normal. Source skin orientation comes from its occurrence in the input
solid, not the standalone generator face. A simple-offset wall is located through
its source trim edge. Boolean history propagates these pre-authored references;
OCCT face enumeration does not define persistent identity.

Profile clearance uses direct constructions for common cases. A planar
polygonal passage is the union of projected boundary faces. A convex polygonal
prism on a cylinder or cone is evaluated as half-space constraints on axial
position and normal depth at each angular coordinate. Analytic angular events
split its boundary when the active constraints change; the prism may be oblique
to the sheet axis. A circular passage
perpendicular to a cylinder axis has two skin-envelope curves and two analytic
tangency connectors. For a circular or elliptical profile on a cone, fixing the
angular coordinate maps the profile's unit disk affinely into axial position
and normal depth; its extrema inside the thickness strip come from a skin or a
disk tangency. Ellipse axes may be rotated within the profile plane.
These cases avoid repeated thickness-section Booleans. Other curved profiles
use bounded refinement with a Sheet-Cut-tolerance-limited margin and must pass the
same three-dimensional clearance check. Failure to establish clearance rejects
the feature rather than accepting a narrowed opening. Numerical sections are
calculation aids, never persisted topology identities.

Before the sampled envelope is saved, contiguous boundary pieces with the same
authored ancestry are consolidated into one rational B-spline edge. Its small
join tolerance is expressed in physical units within the cut's deviation
budget, and the resulting domain must still pass the clearance check. Separate
runs are distinguished by their ordered neighboring semantic boundaries and
outer/hole role; genuinely ambiguous identities are rejected. Sample depth or
OCCT traversal position never distinguishes persisted cut boundaries.

The analytic cone offset accounts for indirect surface frames explicitly: OCCT
8's cone branch does not apply the handedness correction used by its cylinder
branch. A conical tool crosses the opposite skin by ten Boolean tolerances so
separately represented coincident cones cannot retain a zero-thickness closing
face. Planar and cylindrical tools retain their exact depth at sheet junctions.
This is numerical clearance of the derived tool; saved thickness and material
domains retain their exact authored values.

One cut owns all affected regions. Calculated native BodyResult packets retain
`sheet_cuts`, including the cut owner, source face identity, analytic surface
frame (including both X and Y to preserve handedness), thickness and oriented
rational B-spline trimming loops in surface UV
coordinates. Boundary identities encode their source face and authored projection
or input-edge parents. Subsequent history operations preserve the earlier cut
records. These are original cut definitions, not a second independently editable
model or a promise that later arbitrary solid machining remains developable.

The native Extrusion definition carries `sheet_cut`; the command host exposes it
through `extrusion.create` and `extrusion.set`. Enabling it selects subtraction
and a solid profile while retaining the ordinary Extrusion range parameters.
The `sheet_cut_clearance` parameter selects Profile clearance; false selects
By surface. The mode is persisted and included in the geometry fingerprint.
Native versions are
Part INI 38 / JSON 62 and Assembly INI 32 / JSON 44; both tracked start templates
are updated. Drawing is unchanged. All required data lives in the native document.

### Limits of this step

The saved UV domains belong to the source skin. Separate Unbend/Bend Back
transport actual material contributions in their authored state, including Sheet
Cuts and ordinary additive/subtractive operations. Their limitations and
verification are documented separately. Sheet Cut projects onto all intersected
supported reference skins in the current Body; it has no individual region collector.

### Sheet Cut verification

All thirteen focused suites passed in
`build/sheet-cut-preview-clearance-final-tests.log` (193.49 seconds), including
the GUI, native geometry, wire preview, settings, document creation, and
Assembly import tests. Both copied user-case modes then regenerated, saved,
and reopened successfully. This is focused verification, not a full repository
test run or a new portable release.

The subsequent boundary-consolidation fix passed the complete native Bend
suite in `build/sheet-cut-clearance-pass35-tests.log`. Its final endpoint
preservation and physical metric bounds passed the focused clearance run in
`build/sheet-cut-clearance-pass38-generic.log` and the actual three-cut case in
`build/sheet-cut-clearance-pass38-actual-pins.log`. Consolidation reuses copied
shared endpoints and their bounded tolerances, preserving the original wire
connections without changing source geometry. The generic cylinder/ellipse
case checks unique persisted boundary identities, identical identities after
regeneration, the independent profile-prism test, and the physical-deviation
volume band.

The final GUI/CLI rebuild and the profile/application-tools suites passed in
`build/sheet-cut-finishing-tests.log` (31.55 seconds). Both user-case copies then
regenerated, saved and reopened successfully with the final implementation.
The updated `build/sheet-cut-clearance-properties.png` was visually inspected:
the fixed subtraction mode has no redundant Operation row, and SKETCH and
OK/Cancel remain separate without overlap.

The calculation regression checks independently derived removed volumes for a
Flat, cylindrical Revolved Sheet, curved Sheet Profile and the same Profile with
a tangent straight continuation. Eight cone cases combine positive/negative
taper, reversed generator direction and reversed rotation axis, with exact
independent volume expectations. Separate cases cut through a Flat and an
attached Profile or conical Revolved Sheet with one operation and verify both
source owners survive. Three disconnected Flats test forward/reverse, bounded,
two-sided, symmetric, Through All and Up To ranges, including saved limits.
Conical and circular cuts, consecutive cuts, a closed profile with a retained
inner material island, rejection of a non-intersecting profile, native save/reopen,
fresh recalculation and Undo/Redo are covered. Source surface frames, including
both UV directions, and trimming loops round-trip through the native files.

The application-tools test checks the visible range controls and forbidden
result modes, reverses the purple cue, drags its length endpoint, arms an Up To
reference, and draws and commits a Sheet Cut in the actual Sketcher. It reopens
properties, enters and leaves the owned Sketch, and verifies Cancel does not
commit pending range changes. The screenshots `build/sheet-cut-properties.png`
and `build/sheet-cut-view.png` were visually inspected. The remaining suites cover Bend attachment, history recovery,
general geometry, Assembly persistence, ordinary profile operations and Flat.

Configuration tests cover GUI/CLI override layers, invalid-value warning and
fallback, atomic saving, new-Part defaults, native save/reopen, and preserving
an existing Part when the application default changes. STEP and IGES imports
also save the configured tolerance in their newly created Parts. The Sheet
Metal settings screenshot `build/sheet-metal-settings.png` was visually
inspected; its tolerance field uses the existing internal dialog layout.

The standalone wire-preview test deliberately does not link the OCCT kernel.
It checks both sheet skins and thickness connectors, retained islands,
disconnected sheets outside the range, cylinders and both cone slopes with
reversed material sides, and walls parallel to the projection. It also checks
that unchanged updates reuse the cached wire, changed geometry invalidates it,
incomplete input clears it, and excessive mesh/profile/interval work falls back
to the projection wire without repeated attempts. The GUI test checks both
wires, mode persistence through Sketcher and Cancel, and a translated active
Body next to unrelated visible geometry.

A copied user case, `Projects/01.prtz`, reproduced an indirect conical skin with
a cut domain but no corresponding cone material removal. After correction, a
single operation cuts both the Flat and cone. Its one-sided Reverse projection
matches both-direction Through All; Forward correctly misses the material.
The copy was regenerated with complete face references and no calculation
errors; the original project was not changed.

The Profile clearance copy additionally passes an independent ordinary-prism
check immediately after each of its three Sheet Cuts. Subtracting the original
authored profile prism at a `0.0000001` mm Boolean tolerance changes the result
volume by less than `0.000000688` mm³ in each case (test limit `0.00001` mm³).
The same prisms intersect positive material volumes before their respective
cuts, so these are not empty or misplaced probes. This volume check is not a
measurement of maximum boundary displacement.

The measured individual calculation times are **0.705 s, 0.653 s, and 1.128 s**
with preceding history boundaries reused. A separate full regeneration took
**2.299 s**. These measurements exclude file loading/saving and the additional
verification operations. All three individual cuts meet the agreed 1–2 second
target on the development machine. Evidence:
`build/sheet-cut-clearance-pass38-actual-pins.log`. The final calculated volume
is `170293.6812135352` mm³. The source project's SHA-256 remains
`E141C4CC191DCB528A77F8126860ACF44B7AD73B70975ECC11F776B2B9DCE29C`.

## Separate Unbend and Bend Back (2026-09-18)

**Unbend / Rozvinout** and **Bend Back / Ohnout zpět** are separate history
operations. Each offers all eligible regions or a manual list selected through
the common View picker or Tree. No fixed-plane placement reference is required:
authored feature history determines attachments. Later cuts and additions remain
part of forward history, and returning to a known authored state does not
repeatedly transform an approximated result.

See [Sheet state history operations](SHEET_STATE_DEVELOPMENT.md) for ownership,
cylindrical/conical maps, zero-radius hems, tolerance, commands and verification.
The old per-profile state switch and folded-reference snapshot implementation
have been removed. A future command for bending an existing flat sheet along a
line remains separate work. Closed full-circle regions still need seam support.
