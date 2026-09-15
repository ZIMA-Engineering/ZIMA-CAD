# Assembly references and mates

This document defines how ZIMA-CAD selects and stores geometry for Assembly mates.
A mate belongs to original component geometry, not a transient result face of the
whole Assembly.

## Source and target references

Editing an inserted component supplies a pair:

1. The **Part reference** belongs to original Bodies of the component being placed.
2. The **Assembly reference** belongs to another immediate component occurrence,
   or the Origin/construction geometry of the immediate owning Assembly.

Instance paths distinguish repeated occurrences of the same Part. A component
cannot mate to itself. A parent Assembly cannot directly position a subassembly's
internal component: activate that subassembly first.

## Origins, mate types and degrees of freedom

The built-in Part-document Origin derives from the saved source Part identity and
exact occurrence placement. It needs neither a result-solid cache nor an open
source file. Body/container Origin references are supplied in component reference
geometry at insertion and explicit regeneration.

The tree and View can select a point, axis or plane of a Part, Body or container
Origin, or the owning Assembly Origin. Selecting the owning Assembly's whole
Origin in the tree automatically pairs both Origins with three zero-offset mates:
**XY–XY, YZ–YZ and XZ–XZ**. Open **Part Properties** or **Assembly Properties**
is sufficient; neither prior source-Origin selection nor arming a target field is
required. Existing rows are replaced together as a pending change committed only
by OK. In an activated subassembly, select its own Origin; a higher Assembly's
Origin cannot substitute for it.

Selecting another whole Origin fills the selected side of three rows with its
point and X/Y axes. The exact owner and occurrence are saved; a Body Origin is not
replaced by the document Origin. Individual planes, axes and points remain
manually selectable.

Geometry determines mate type: point–point, axis–axis or face–face. Faces also
allow an angle mate. Axes offer coaxiality only; Axis Angle was removed. The menu
prevents types incompatible with the geometry. Changing reference kind clears an
incompatible opposite side. On opening, the dialog also normalizes an incorrect
type in a pending row; only **OK** saves that change.

Properties show remaining degrees of freedom and calculated X/Y/Z and RX/RY/RZ.
Coordinates determined by valid mates are not editable. Freedom counts derive from
independent geometric equations, not populated-row count. Coaxial axes retain
translation and rotation along the axis; adding an end plane leaves only rotation.
Coincident complete Origins remove all six freedoms. One oblique free motion can
change multiple coordinates. Counting uses actual spatial translations/rotations:
Euler-angle singularity at RY = 90° does not create extra freedom. A fully
constrained Part keeps all placement and rotation fields locked there too.

All valid rows of a component are solved together. Face Angle uses motion allowed
by other mates: a coaxial, end-positioned Part rotates around the common axis while
preserving contact. Results are accepted only when all equations hold. Later rows
cannot violate earlier rows. **Flip** for coaxiality and face coincidence toggles
aligned/opposed normal or axis directions. Selecting a new pair stores the nearer
orientation in the toggle; later calculation does not reinterpret it from current
placement. Repeated toggling therefore restores the direction, including after
save/reopen. Flipping an angle mate requests its supplementary angle.

An unsatisfiable combination reports **Mate conflict**. Preview retains the last
valid placement, **OK** cannot save an invalid result, and **Cancel** discards
pending edits. Invalid value-mate drag steps follow the same rule.

Preview uses saved reference geometry without OCCT and preserves surrounding
components during nested editing. **OK** saves solved placement and mates;
**Cancel** restores original component state.

## Moving by the Origin

A selected immediate Part or subassembly shows a purple point at its source-
document Origin, with the same 6-pixel radius as Part manipulators. It uses the
common View picker and works with Properties closed. Selecting another object or
clearing selection hides it.

Dragging projects cursor motion onto remaining **translational** freedoms from
the same geometric equations used by mate calculation. A free Part moves in the
view plane; coaxiality allows axial motion; a plane mate allows in-plane motion.
Grounded or fully constrained Parts do not move. Rotational freedom alone does not
allow the Origin handle to alter placement or angles; use angle values and their
controls for rotation. This also applies to oblique directions and rotated
subassemblies.

With Properties closed, mouse release saves one move reversed by one **Undo**.
**Escape** during dragging restores the pre-gesture placement. With Properties
open, the handle respects pending mates and the move remains preview until **OK**;
**Cancel** restores original placement and references. A subassembly always owns
placement of its immediate components. Activate it before moving an internal Part;
the surrounding Assembly remains visible.

## What the View actually selects

Assembly subtraction can make displayed geometry differ from its source Part.
The mate picker nevertheless tests persisted meshes of components' original source
Bodies. It neither traverses the result OCCT compound nor creates references from
faces introduced by Assembly cuts.

Consequently:

- Chamfers, fillets and Assembly cuts must not redirect selection to another
  result face.
- The same original face remains a reference even when partly cut away in the
  Assembly result.
- Final Assembly faces without an original owner are rejected for mating.
- Orange hover and azure confirmation identify the same canonical reference saved
  in the document.

## Persisted reference representation

Faces are stored as `AssemblyFaceRef`, circular edges as `AssemblyEdgeRef`.
Descriptors contain exact instance identity and stable source `FaceRef`/`EdgeRef`.
The `.asmz` never stores face/edge positions within a result mesh.

Circular-edge analytical data (`origin`, `direction`, `radius`) and cylindrical-
face data (`origin`, `axis`, `radius`) are persisted viewer data. The same mechanism
can create an Axis container at a cylinder centre in a Part or activated Assembly
instance without live OCCT topology traversal. An optional subsequent planar
reference locates the axis Origin at the plane/centreline intersection.

Opening Properties and highlighting use saved data. OCCT may run only on explicit
body calculation, such as **OK** or model regeneration; hover, selection and dialog
opening must not silently recalculate topology.

## Missing source data

Regenerated Parts store original Bodies and semantic face/edge/vertex mappings in
`BodyResult.source_bodies`. Incomplete derived data can lack these records. The
application must never infer a reference from a result face in that case.

Recovery:

1. Open the source Part.
2. Run **Regenerate**.
3. Return to the Assembly and run **Regenerate** to obtain current reference data
   from the open Part.
4. Open component Properties. Saving the source Part first is unnecessary.

A single-container imported Part can use its final imported Body as the original
source because no earlier parametric result exists.

## State after model changes

References explicitly report `RESOLVED`, `MISSING` or `AMBIGUOUS`. Missing or
ambiguous mates remain saved but do not participate in solving. They are never
automatically replaced by similar faces with different runtime indices. Derived
caches containing runtime identity alone are stale and rebuilt from persisted
source data.

See [Stable Topology Naming](STABLE_TOPOLOGY_NAMING.md).

## Suppression and dependency graph

Suppression is different from hiding. **Hide/Show** changes only occurrence
presentation; all its mates and references remain available. **Suppress** removes
the occurrence from the active Assembly model.

Dependencies derive directly from persisted mate descriptors and sketch external
references scoped as `assembly_component`. Graph edges run from a dependent
occurrence to the exact occurrence supplying its target reference. The graph uses
stable occurrence identity, not component names or shared source Part IDs alone.
Building it neither traverses OCCT geometry nor calculates bodies.

Manual suppression recursively causes effective suppression of all dependents.
This derived state is not written as their manual flag. Restoring the source
therefore releases only dependents without another suppressed source, preserving
other manual suppression. Missing components and unresolved persisted references
are not deleted from mate records. An active component with such an error is red
and remains repairable.

## Manual component movement

The purple Origin point in component Properties supports interactive movement.
Before changing coordinates, proposed translation is projected into freedoms
allowed by valid mates:

- A plane mate removes motion along the target-plane normal.
- A coaxial mate removes both components perpendicular to the target axis,
  allowing only axial movement.
- Multiple mates intersect their allowed directions. Without translational
  freedom, dragging cannot move the component.
- An angle mate alone does not restrict translation.

The Assembly solver does not run during movement. The component and its dependent
chain move, with View redraws coalesced into short intervals. On release, the
solver validates and commits the result. Source-component reference frames move
with the component during dragging, so validation does not mistake allowed motion
for a changed local reference.

Numeric fields and View dimensions use [shared value locks](NUMERIC_VALUE_LOCKS.md),
including one-time capture of the current value during reference entry.

## Signed angles and opening sources (2026-09-09)

Face Angle accepts −180° to +180°. Sign uses an oriented target-reference frame:
an axis/plane from other component mates takes priority, otherwise the target
Origin frame is used. Flip reverses the target normal. Numeric entry, measurement
and dimension dragging share orientation. Nonzero angular dimensions appear during
placement and after reopening Properties, including mates to the Assembly's own
Origin. At 0° and ±180°, normals are parallel and rotational freedoms reflect that.

Component context menus in the tree and View offer **Open**. The source Part or
subassembly opens in its own tab; an already open document is reused in its current
state without duplication. **Active** still edits a component in the top Assembly's
context. Regenerate is above the View, not duplicated below Revolution in the
right panel.

Owned sketches of Assembly Extrusion/Revolution survive returning to Properties,
re-entering Sketcher and saving. Editing a saved operation displays its active
sketch over the input model. OK commits the complete container; Cancel restores
original state.

## Regeneration and dimension display (2026-09-10)

**Regenerate** refreshes sources throughout nested Assemblies. Current in-memory
state of open Parts/Assemblies is authoritative. Closed sources load from saved
`.prtz`/`.asmz` paths relative to their immediate Assembly, without opening tabs.
Document identity must match the insertion reference. Cycles and unavailable
sources reject calculation before committing new parent state. Tab switches and
Part saves do not recalculate the parent Assembly. Regeneration preserves zoom,
rotation and pan; Fit remains a separate command. Current source geometry display
sharing is described in [ASSEMBLY_GEOMETRY_SHARING.md](ASSEMBLY_GEOMETRY_SHARING.md).

The **Dimensions** toggle in View menus and above the View hides/shows model
dimensions. Hidden dimensions cannot be picked. It changes neither mate parameters
nor Drawing Show/Erase.

A plane–plane angular dimension centres on an existing coaxial mate's target axis
when that axis is parallel to the dimension axis. Otherwise its centre lies on
the plane-intersection line near the selected references. Arc arms follow plane
directions rather than normals. This affects presentation only, not references,
angle values or component placement solving.

Confirming a component in the View/tree offers only its placement dimensions in
the Assembly being edited. Empty clicks, deselection and shared middle-button
termination hide them. The Dimensions toggle permits this temporary visibility;
it does not show every Assembly mate at once. Rendering and picker candidates
share the filter without changing saved references.

## Origins during feature creation (2026-09-11)

Feature dialogs with the shared **Origin** action (Extrusion, Revolution, Mirror,
construction features, for example) do not automatically offer every component
Origin in an Assembly. The displayed Assembly's main Origin remains visible by
default. Request other Origins with **Origin**, then click the exact Part or
subassembly in the View/tree. Clicking again hides that Origin. Nested subassemblies
can be selected directly in the tree.

Visibility belongs to the exact occurrence path, without enabling other copies of
the same source. Existing local-container Origin selection within an active Part
uses the same action. Hidden Origins are excluded from both hover and confirmation:
rendering and the common candidate list share one filter. Closing the dialog ends
the temporary policy. Placement, ownership and reference persistence are unchanged.

Verification: all 44 Windows CTest tests passed (425.67 s). After adding integration
coverage to `verify_component_references`, the application rebuilt and targeted
`zima_cpp_workspace_startup_contract` with `ZIMA_VERIFY_COMPONENT_REFERENCE_ONLY=1`
passed (8.10 s). It opens Protrusion and Mirror over a nested Assembly, requests a
subassembly Origin using the actual button/tree, and checks occurrence isolation
and restoration after Cancel.
