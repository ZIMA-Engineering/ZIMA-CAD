# Modeling follow-up agreed on 2026-11-19

Current status: Projection is published as Windows 2026112004, including the
preceding General Surface, Twist, sheet attachment and mixed Thin Loft/Shell
repairs. See the [Projection verification record](verification/20261120-projection.md)
and [release record](releases/2026112004.md) for the finite coverage and delivery
checks. The earlier owned RIP, Sketch Fillet, cylindrical Up To and placement
repairs retain their [2026112001 verification record](verification/20261120-owned-rip-and-sketch-fillet.md).
The user authorized FEM implementation next; its agreed workflow and outstanding
technical choices are recorded below. This does not claim that FEM already works.

The preceding implementation completed for Windows build 2026111905. Its
[verification matrix and explicit limits](verification/20261119-modeling-completion.md)
supersede the earlier partial status below. Windows 2026111906 is published;
committed-source packaging, signing and anonymous public download checks passed.
The release record contains the exact artifact identities and verification.
That publication build is 2026111906; it additionally builds the new Bend
end-constraint test in both platform packagers and requires its registration.

The user authorized finishing the entire list, commit, push and a Windows GitHub
release on 2026-11-19. This supersedes the earlier wait-for-review instruction.

Post-publication reports add RIP Sketch Tree selection and the half-height Bend
in automatic conversion of `25.prtz`. Their fixes and finite tests are tracked
in the [split-boundary verification record](verification/20261119-sheet-rip-and-split-boundaries.md)
and [Windows 2026111907 release record](releases/2026111907.md).

## Completion checklist

### Current repair and delivery order (2026-11-20)

The user reaffirmed completing all discussed repairs, starting with the failing
Thin rectangle/circle/slot Loft, before the next commit and push. The earlier
approved checkpoint `9099dd97586d17cfda9ce195de50664e04ffabcf` is already pushed.
Its committed Windows candidate may be installed locally for testing while the
remaining repairs continue; do not describe that checkpoint as completing the
remaining Thin Loft defect. The user has closed ZIMA-CAD and explicitly permits
replacement of the normal local binary. Preserve `zima-cad.bat` and user data.

Review the completed General Surface references/context, Solid Twist centroid
axes and attached-feature transport, Twisted Sheet dimensions/axis placement,
Flat-axis transport during Unbend/Bend Back, sheet operation controls and Sheet
Transition preview against the finite evidence in
[the follow-up record](verification/20261120-sketch-reference-text-followup.md).
Keep the unobserved disappearance symptoms distinct from reproduced defects.
Off-contour conversion points are a verified repair; manual point placement was
an investigative hypothesis, not a separately confirmed outstanding defect.

After the remaining repairs and verification, commit and push, then continue
the agreed Projection feature design below. Projection/FEM design is not part of
the repair acceptance claim or authorization to silently extend native formats.

The Thin mixed Loft repair and the additionally requested Shell verification
now pass the finite native, document and GUI matrices in
[the repair acceptance record](verification/20261120-repair-acceptance-followup.md).
Version 2026112003 carries the shared fallback; the prior checkpoint remains
an independently identifiable build. The committed Windows candidate passed
its packaging gates and is installed under the normal development launcher.
See the [artifact identities and verification limits](releases/2026112003.md).

The subsequent [Projection implementation proposal](PROJECTION_FEATURE_DESIGN.md)
specifies the agreed three outputs, shared Feature lifecycle, native reference
ancestry, preview/calculation boundary and acceptance matrix. It is design work;
the required native-contract extension has not been implemented or approved by
this repair commit.

The agreed sequence on 2026-11-20 is to verify the current repairs and publish
their Windows binary first, then repair the newly reported cylindrical Up To
preview, repair the 3D Section dialog and finish the outstanding Sheet Transition
work before starting the subsequent Projection/FEM features.

### 3D Section dialog report (2026-11-20)

The user confirmed this concerns model-space Sections, not Drawing sections.
In `26.prtz`, the A-A Section Properties screenshot shows the Default Origin
button detached above the title bar, displaced controls and no visible Section
origin preview in the model. Review ownership/layout of the shared placement
controls and the existing Section preview initialization. The user explicitly
requires the same review in both Part and Assembly contexts. Verify resizing,
reference selection, origin visibility, OK/Cancel, reopening Properties,
persistence and both themes, retaining Assembly occurrence ownership/context.
Do not infer permission to change the protected shared placement solver from
this presentation defect. This is subsequent work after the current package.

### Cylindrical Up To preview report (2026-11-20)

While the 2026112001 package was building, the user tested the local repaired
build and reported a missing azure creation/Properties preview in `26.prtz`,
`Tažení 05`. The screenshot shows Side 1 `Up To`, target `Tažení 01 / Plocha`,
direction -1, XZ Sketch plane with 64 mm offset, zero draft and additive Solid
output. The input Sketch and placement planes appear, but the extrusion preview
does not. Native cylindrical calculation tests do not establish this GUI path.
The user explicitly requested completing the current package before this repair.
Trace the established authored preview generator and verify its curved target
termination, both directions, edits, Cancel and ordinary inspection without
introducing a solid calculation for selection. This is not fixed by 2026112001.

### Body Measurement report (2026-11-20)

The user questions Body Measurement in `26.prtz`. The screenshot shows no
reported properties and explicitly states that the measured history has a
calculation error; `Tažení 06` is marked uncalculated in the Tree. This is an
observed failure-state presentation, not yet evidence of an incorrect physical
calculation. Verify independent volume/area/centroid/inertia values on valid
geometry and inspect the behavior with an invalid history feature and retained
last valid material. The user confirmed that deleting the red feature restores
measurement and explicitly requested measuring the retained displayed valid
Body even while a feature is uncalculated. Clearly label that result as the
last valid state excluding the failed feature; do not present it as a successful
final-history calculation. Preserve the established inspector contract: only Save
inserts a record; OK, Cancel and middle-button confirmation only close it.

### Tree history ordering request (2026-11-20)

The user requests drag reordering of Fillets and both Measurement and Body
Measurement history entries. Reuse `HistoryTreeWidget`,
`tree_item_reorder_enabled`, `move_part_history` and the existing dependency
checks rather than a separate ordering path. Inspect the current eligibility
and Tree row roles first: common Part containers already participate in the
shared path. Reordering Fillets changes their calculation order; a measurement
should evaluate its intended history boundary after movement. Verify actual
drag/drop, valid/invalid dependency moves with a clear explanation, recalculated
values, identity, owning Body scope, Undo/Redo and native save/reopen. Preserve
current rejection of broken dependency moves unless a separately agreed change
is required. This follows completion of the current Windows package.

### 2D Sweep station-count review (2026-11-20)

The user reports an apparently extra station in `26.prtz` / `2D tažení 01`.
The screenshot has two path segments and four table rows: 1-start, 1-end,
2-start, 2-end. Row 2-start explicitly inherits from 1-end. The user clarified
that the path consists of one connected segment and one arc and expects three
geometric stations (start, shared junction, end), with the junction displayed
once. Determine whether
these are intentional separate endpoint slots at one shared geometric junction
or whether calculation/preview duplicates an actual station. Check the exact
path topology, placement of profiles, inherited versus explicit profiles and
result continuity. Do not delete a structural row merely to reduce the table
count; preserve its documented ability to override or inherit a profile when
presenting the agreed three geometric stations. Distinguish a duplicate table
entry from an actual extra calculated station. Any necessary persisted-contract
change retains its separate approval requirement. This follows the current
Windows package; an extra calculated station is not yet established.

The user also requests that authoring a Sweep profile in Sketcher retain the
previous profile Sketches as azure read-only context, labeled by station with
their point numbers. Reuse the established positioned profile preview and point
correspondence, respecting actual frames and active-Sketch edit ownership. Check
successive station editing, correct numbering/placement, visibility, Cancel,
saved versus inherited profiles and absence of document changes from display.

Implemented on 2026-11-20 without a native format change: one row per persisted
geometric junction, an incoming/outgoing selector retaining both definitions,
and read-only positioned profile context in Sketcher. The Windows checks,
representative model result, and explicit verification gaps are recorded in
`verification/20261120-sweep-stations-context.md`.

### Agreed cursor-centered orbit (2026-11-20)

The user proposes a future toggle immediately to the right of Restore View,
using an Origin-shaped icon with the three axis colors shown in the View.
When enabled, an orbit gesture uses the pointer position at the initial
middle-button press as its pivot. Keep that pivot fixed throughout the drag;
the next gesture may start at a different pointer position.

The agreed depth rule intersects the initial pointer ray with the plane
parallel to the screen through the current orbit center. This keeps the current
orbit-center depth and camera scale; it does not snap to the closest model face.
Changing the pivot must not cause an initial camera jump. The agreed default
is disabled to preserve established navigation, with an explicit highlighted
enabled state. The user explicitly approved these default/depth details. This
records the agreed future behavior, not a completed implementation.

Preserve middle-button double-click confirmation, short-click command behavior,
navigation modifiers and panning. Review the existing navigation state machine
before implementation. Verify off/on behavior, empty View space, orthographic
and perspective cameras, repeated gestures, no initial jump, both themes and
Part/Assembly editing contexts. Localize the action and tooltip in all five
languages. This belongs to future work after the active repairs.

### Agreed cylindrical angular placement reference (2026-11-20)

After the active repairs, add an explicit Angle reference type to shared
container placement. The user approved this scope after discussing the risk
to other placed containers. A cylindrical face supplies the rotation axis and
orientation; another positional reference may set axial distance. A subsequent
planar reference in Angle mode supplies the zero direction: project its oriented
normal onto the plane perpendicular to the cylinder axis and measure toward the
radial direction from that axis to the container origin. Reject a reference
whose projection has no direction. Preserve the reference orientation/side and
show both rays and a directed angular dimension in the View. The reference row
uses Angle instead of Face and degrees instead of linear offset.

Existing Face references keep their current meaning. Trace the shared solver,
DOF reporting and persistence before implementation; test mixed references,
whole-Origin placement, orientation, replacement, save/reopen and Undo/Redo.
On 2026-11-20 the user explicitly approved extending native reference persistence
with Angle mode and a value in degrees, including regeneration and verification
of affected Part/Assembly templates and factory assets, save/reopen and Undo/Redo,
and isolated Toyota project copies checked against the verified backup. Preserve
the originals and report the older unsupported project files as a verification
gap rather than silently converting them. This records approved implementation
scope, not a completed feature or a completed format change.

### Subsequent FEM and surface-modeling design (2026-11-20)

The user authorized proceeding with FEM implementation after completing the
current Projection commit/push, Windows GitHub publication and local binary
delivery. Finish that release first, then implement the agreed FEM workflow.
Gmsh and volumetric meshing of solids and sheet-metal Bodies are agreed below.
Trace shared contracts and obtain the required scoped agreement before changing
native persistence or protected container placement; this authorization does not
waive those repository safeguards. Technical integration and numerical acceptance
remain work to perform, not completed capabilities.

Windows 2026112004 is now published and delivered locally. FEM work has started:
the [technical design](FEM_DESIGN.md) and
[initial native Gmsh verification](verification/20261120-fem-mesher.md) record the
first checkpoint. The user approved the scoped native FEM persistence extension
on 2026-11-20, including affected template/factory verification and protected
Toyota copies; the shared placement solver is excluded from changes. The native
definition/transaction implementation is now in progress. No FEM UI or solver is
included in that published release.

#### Part-level FEM creation and editing

The user confirmed the following interaction on 2026-11-20 while the Projection
Windows release was being prepared. This is agreed design, not an implemented
FEM command. The mesher direction was subsequently agreed below; choosing an
analysis solver still needs the remaining technical design discussion.

- Offer **Create FEM** at the Part's Body-list level.
- Open one shared internal Properties dialog and select a source Body or the
  result of a Boolean operation through the existing reference-entry controls.
  The source reference identifies the actual selected model result, not a Tree
  row or a viewer-only approximation.
- OK creates a new FEM Body within the same Part; Cancel leaves the Part unchanged.
- The FEM Body's context menu exposes **Properties**, reopening the same dialog
  with its saved definition and allowing source replacement. Creation and editing
  use one dialog and the normal OK/Cancel transaction contract.
- Retain a one-way associative source dependency. Source changes invalidate the
  corresponding analysis state; meshing and solving remain explicit actions.
  A broken source remains visible as an unresolved dependency for repair.
- The user explicitly confirmed shared source geometry rather than a duplicated
  CAD Body/history. The source owns its calculated geometry; the FEM Body refers
  to and displays that same result. Its own data are the analysis definition,
  mesh and results. Persist the source geometry once within the Part and retain
  the FEM reference and required analysis data in the native document.
  After source regeneration, display its current geometry and mark prior mesh
  and results as stale until the user explicitly remeshes/recalculates.

Proposed display behavior: while a FEM Body is active, display its shared source
geometry once, without superimposing a second independently drawn source Body.
Keep both source and FEM identities in the Tree. Offer Geometry, Mesh, Geometry
with mesh, and later Results display modes, with condition symbols overlaid.
Deactivation restores ordinary Part display without changing persisted source
visibility. This display proposal was explained during the ongoing discussion;
it is not an implemented viewer contract.

The user proposed top-right workspace selectors, following the application's
existing arrangement: **FEM | Mesh | Results**. These select the presentation and
right-hand command set within the same active FEM Body. Distinguish the complete
lifecycle **Definition -> Mesh generation -> Calculation -> Results/animation**:
mesh generation does not start the solver, calculation has its own explicit
command and progress, and animation belongs to inspecting calculated results.
Switching workspace modes must not calculate geometry, generate a mesh or run a
solver. Results are unavailable before calculation and clearly marked stale
after relevant input changes. These are design requirements, not implemented
workspace modes.

Color-coded display belongs to this workflow. Mesh inspection may color element
quality or assigned regions; result inspection colors the selected calculated
quantity, with a legend, units and value range. These meanings must be explicit
and distinct. Provide an optional mesh-edge overlay for readable result maps.
Do not present a mesh-quality color as a solved physical result.
Display the color legend at the side of the View, with numerical tick labels,
the selected quantity's name and its unit. During animation keep the displayed
range fixed across the selected playback sequence, so an unchanged color retains
the same numerical meaning from frame to frame.

Results inspection also offers deformation animation with Play/Pause, a scrubber,
playback speed and an explicit displayed deformation scale. For static analysis,
animate interpolation from the undeformed shape to the solved deformation;
identify it as visualization, not a computed time history. Actual time-dependent
motion belongs to a later dynamic-analysis definition and its calculated time
states. Animation does not alter the source CAD Body or rerun the solver.

#### Shared FEM condition container

The user confirmed one shared condition element and Properties dialog, following
the existing Feature command. The active FEM Body's right-hand command menu offers
separate Force, Pressure and Constraint entries (later Temperature and other
conditions). Each entry opens the same dialog with its type preselected in a
dropdown; changing that dropdown selects the condition type within the same
container. Do not implement independent placement or Sketch dialogs per type.

- Reuse the ordinary container placement and reference-entry contract. Its origin
  provides the default point location, and its plane supports an editable input
  Sketch and projected curves or surface regions. An offset authoring plane is
  distinct from the actual analysis region on the referenced Body.
- A condition applies to a referenced point, Sketch path/projected curve, or
  bounded surface region. Force and Constraint share this region-selection
  mechanism. Validate the region against the selected condition type; pressure
  remains an area-based condition, rather than acquiring point-force semantics.
- Default directional input follows the oriented container normal, with reversal.
  Preserve the distinction between a fixed direction and pressure following the
  local normals of a curved target surface. Constraints use the same region and
  local frame to specify which applicable movement components are restrained;
  selecting a region alone does not imply that every component is fixed.
- Keep placement and applicable region references when changing type; display
  the parameters appropriate to the selected physical condition. Do not silently
  reinterpret incompatible quantities or units from the previous type.
- Future temperature and other condition types reuse this container and region
  definition while exposing only their meaningful parameters. This remains
  agreed interaction design, not an implemented analysis capability.

Display condition-specific symbols at the actual assigned region: a force arrow
and identifier/value (for example `F1 = 5000 N`), several pressure arrows with one
shared pressure label, a fixed-support symbol with a hatched base, and a temperature
symbol/value. A partial restraint must identify its restrained directions instead
of masquerading as a fully fixed support. Symbols retain their owning condition
identity. Single click selects and synchronizes the Tree; double-click parameter
inspection and context Properties follow the ordinary shared interaction contract.

The interaction requirements above are sufficient for a coherent FEM design.
The user clarified that materials already belong to the Part: reuse the source
Part's assigned material and the shared material system, without a second FEM
material copy or independent catalog. Existing factory materials already expose
Young's modulus, Poisson's ratio, shear modulus, density and thermal properties
with units. Current native Part storage uses `physical_parameters`,
`physical_parameter_units` and `material_parameter_descriptions`; preserve this
self-contained material definition. Verify its required properties and unit conversions;
missing data must be resolved through that shared system.
Before implementation, settle the first supported analysis kind and element
families, then choose the mesher and solver against those
requirements. Static mechanical analysis is the proposed first delivery; thermal
analyses and actual dynamics are subsequent proposals, not confirmed solver scope.

#### Explicit mesh generation and calculation

After defining conditions, the active FEM Body exposes **Generate mesh**. Its
settings include a mesh size and applicable local refinement; the generated mesh
is displayed for inspection. Meshing is an explicit action, separate from
**Calculate**. Conditions remain attached to CAD geometry and are resolved onto
the new mesh after regeneration, rather than retaining obsolete mesh node IDs.
This sequence is agreed design; mesher, solver and their detailed settings remain
to be selected during the subsequent FEM design work.

Mesh controls belong to the native ZIMA-CAD UI: element size in document length
units, overall refinement and local refinement of selected geometric regions.
The proposed implementation integrates an established mesher rather than
introducing a new unverified meshing algorithm. Verify the selected dependency
and its integration on Windows and Linux before committing to that choice.
The user explicitly requires volume meshes for sheet-metal Bodies as well as
ordinary solids. Mesh the actual material thickness and provide suitable local
refinement through that thickness; do not automatically idealize sheet metal as
a shell/midsurface model. Beam/truss models and any later explicit surface-only
analysis remain separate future scope. This does not yet select a numerical
element formulation or solver backend.

The user approved Gmsh as the mesher direction on 2026-11-20, using its native C++
integration and OpenCASCADE support. Acceptance must exercise holes, small fillets and thin
or twisted sheet-metal volumes, including refinement through the thickness,
while preserving CAD region identities for conditions. Evaluate compatibility
with the shared OCCT pin on both platforms; do not introduce divergent modeling
implementations. The specific version pin and completed integration are not yet
verified. The [official Gmsh site](https://gmsh.info/) documents GPL-2.0-or-later
with an external-library linking exception; this repository declares
GPL-3.0-or-later. Preserve upstream notices and distribute the corresponding
source and license when shipping the dependency.

#### Loads, projected regions and element families

The user agreed that distributed loads belong to CAD curves or surface regions,
independently of mesh node numbering. Meshing or remeshing must derive their
element contributions again. This is follow-up design, not an implemented FEM
capability or an additional acceptance claim for the repairs below.

- Project ordinary Sketch geometry onto selected existing surfaces. The three
  agreed output modes are Sketch (on-surface curves), Split (partition the
  existing faces without changing material), and Surface (extract an enclosed
  surface patch as an independent surface element).
  On curved support surfaces the projected output is generally a spatial curve,
  while its input remains an ordinary editable planar Sketch.
  Closed contours can bound interior regions;
  open contours must actually separate a region using existing boundaries.
  Resolve multiple intersections and the chosen material side explicitly in
  the preview, including periodic surfaces and connected face sets.
- The agreed UI home is a Projection type in the shared Feature command,
  consuming its existing Sketch, placement, Properties and preview lifecycle.
  Its agreed Czech name is `Promítnutí`, English `Projection`; the Tree uses
  the ordinary numbered feature name, for example `Promítnutí 01`.
  FEM load authoring remains a separate analysis command referencing the result.
  Projection uses one direction, initially the Sketch normal, with reversal
  and an explicit direction reference. Multiple selected target faces form a
  shared Up-to target; use the first positive hit per projection ray and show
  missing or ambiguous portions in the preview. Thickness belongs to downstream
  shell properties or a separate surface-thickening operation; Projection has
  no Thin output. Rotational wrapping is a distinct future mapping,
  not an additional ordinary projection branch. This Projection implementation
  and its scoped new-type persistence extension are now complete; see the
  [finite acceptance report](verification/20261120-projection.md). FEM consumption
  of those outputs remains subsequent implementation work.
- The ordinary View shows the projected result on its targets. Double-click
  inspection additionally exposes the original input Sketch in its own plane
  and its existing dimensions. Dimension double-click uses the established
  value editor; confirmed changes propagate through Projection and its
  dependents. Inspection alone neither calculates geometry nor changes the
  document or Undo history. Reuse the shared View/Tree inspection and authored
  preview paths, preserving the distinction between the source Sketch and its
  projected geometry.
- Projection Properties shows both the original Sketch and the projected
  preview simultaneously, visually distinguishing input from derived output.
  Changes to the source Sketch, direction or targets update the pending preview;
  OK commits and Cancel restores the original state. These are two visible
  geometric representations, not two independently editable source Sketches.
- Distinguish total force, force per unit length and traction/pressure per unit
  area. Distinguish a fixed vector direction from pressure following the local
  surface normal. Use consistent equivalent element/nodal loads based on the
  element interpolation and integration over the loaded portion, including
  higher-order nodes and applicable rotational degrees of freedom. Do not
  divide a force equally among selected nodes or attach it to nearest nodes.
- Check the resultant force, moment and equivalent work, appropriate contact
  benchmarks and mesh convergence. A prescribed pressure patch is a different
  analysis input from a contact pair whose active contact area and pressure
  emerge from the solution. Do not impose an elliptical contact patch merely
  because a pin and bushing were selected.
- Keep geometric and analysis references stable through edits and remeshing.
  Report lost or ambiguous regions as broken dependencies instead of assigning
  loads to another region. Different analysis cases may reuse the same region
  with different load definitions.
- A skeleton carrying a support surface can define aircraft skin panels by
  projected boundaries and extracted patches. A wire-only skeleton first needs
  a constructed support surface. A shell surface can have double curvature;
  it is not automatically a developable sheet-metal part.
- The FEM design also includes frame models whose centerlines are ordinary
  Sketch curves or 3D curves. Assign material, cross-section and section
  orientation independently from the centerline. Distinguish beam members
  carrying axial, shear, bending and torsional resultants from axial-only
  truss members. The user expects spatially coincident points to become one
  FEM node within a small geometric tolerance. Frame joints default to rigid
  connections, with explicit hinges or individual rotational releases available.
  Screen-space crossings at different 3D positions do not connect. Reuse the
  geometric load-reference model across beam, shell and solid analyses, with
  the appropriate dimensional units and integration for each element type.

Technical references: [OCCT projection](https://www.occt3d.com/dev/doc/refman/html/class_b_rep_proj___projection.html),
[OCCT shape splitting](https://www.occt3d.com/dev/doc/overview/html/occt_user_guides__modeling_algos.html),
[consistent equivalent loads](https://www.lusas.com/user_area/theory/equivalent_force_distribution.html),
and [contact formulations](https://www.feacluster.com/CalculiX/ccx_2.18/doc/ccx/node139.html).

### Additional requirements agreed on 2026-11-20

The repairs below are implemented in the 2026112001 working batch. Their
[finite verification matrix and remaining publication gates](verification/20261120-owned-rip-and-sketch-fillet.md)
supersede the earlier investigation status in this section. The user explicitly
approved the narrow shared pole-orientation correction before implementation.

- Deleting an arc/curve with virtual rounding, including a complete selected
  chain, reports `Sketch geometry does not exist`. The native removal count
  incorrectly counts dependent rounding as another directly selected object
  for non-segment supports. Verify individual/batch deletion, Undo/Redo and
  preservation of remaining contacts and dimensions.
- Sequentially referencing three datum planes for Extrusion 07 in `26.prtz`
  leaves RX=-90 degrees, RY=45 degrees, zero corrections and one rotational
  degree of freedom. The user confirmed separate plane picks, not whole-Origin
  entry. Investigate preservation of the unconstrained orientation before
  deciding whether the protected shared placement contract needs a change.
- Inspect the last Extrusion in `26.prtz`: the user reports that an Up-to
  cylindrical face is not supported. Reproduce selection and exact termination
  independently on a private copy, and verify applicable forward/reverse and
  two-sided variants before accepting a repair.
- Replace the Sketcher Curve fillet interaction with one internal Properties
  window: radius at the top, two shared curve-reference fields below. Accept
  two preselected curves or start with empty fields. Show a transient exact
  fillet preview after both references are present, update it with the radius,
  and commit only on OK in one Undo step. Cancel preserves the original Sketch.
  Use `Zaoblení` / `Fillet` with all five translations and a purple quarter-arc
  icon with white endpoint dots. Verify the mandatory Sketcher edit/drag matrix.

These requirements supersede the earlier through-solid RIP interpretation. They
are not covered by the 2026111907 completion or release claims.

- Sheet from Body must expose ordinary context-menu Properties, using its
  creation dialog for editing the existing feature. Opening displays the real
  input boundary; unchanged OK does not calculate or add Undo, and Cancel
  restores the unchanged document and full-history presentation.
- Confirming with no selections creates only the existing non-geometric
  replacement boundary in the active Body. It hides preceding solid material
  without generating Flats/Bends or choosing a source face implicitly. This
  must survive save/reopen, regeneration, rollback, deletion and Undo/Redo.
- RIP entry creates/edits Sketches owned by Sheet from Body. Their ordinary
  exact curves act as local face edges for sheet-branch traversal from the
  Bend's material side, rather than cutters projected through the entire solid.
  Support arbitrary native curve types and multiple curves sharing a real
  endpoint, including starts at a Fillet end; exclude construction geometry.
  Preserve all material regions and independently defined sheet branches.
- Releasing the complete conversion leaves its RIP inputs as ordinary editable
  standalone Sketches, retaining geometry, dimensions and constraints without
  continuing to drive the released Flats/Bends. Do not silently delete them or
  introduce a standalone RIP modeling operation. Releasing an individual RIP
  requires releasing the conversion; removing it from conversion Properties
  instead requests recalculation without that RIP.
- Reproduce and repair the blank Sketcher/context after conversion creation,
  deletion and entering Sketch Properties. Include the reported transient
  Sketch deletion failure repaired by Undo, Save and a repeated deletion.
- The private 25.prtz diagnostic confirms a valid nonzero user line; conversion
  encounters a generated boundary edge with two identical sampled points.
  The user also reported lost sheet regions and a second OK that leaves the
  failed command open. These remain verification gates, not user input errors.

Investigations use isolated private copies. On 2026-11-20 the user explicitly
approved extending the native Sheet-from-Body definition with owned RIP
Sketches, including template/factory verification and verification on copies
of ZE0176-TOYOTA-G92K1X. Eleven native project files were copied and SHA-256
verified before the extension; the local backup manifest is
`build/rip25-20261120/protected-native-backup/manifest.json`. Original project
files must remain untouched. This approval does not establish compatibility
with the project's previously unsupported legacy documents.

- Cached complete Fillet routes, symmetric/two-sided current face picking and
  exact curved Sheet-from-Body continuation partitions are implemented.
- Shared unresolved-definition confirmation covers Part and Assembly modeling
  calculation paths. Actual Fillet/Chamfer GUI and eight Assembly cut variants
  verify the first/second OK transaction; every command-specific validation
  combination is not exhaustively covered.
- Angled Parallel + C survives actual GUI Trim, subsequent edits and reopening.
- Virtual curve corner rounding and the separate native Curve fillet command
  are implemented without automatically inserting a radius dimension.
- View-only Flat-end dimensions/constraints and propagation to the following
  Bend are implemented, with planar full-material Up-to limits.
- Independent branches, parallel calculation, source deletion/release, history,
  relative-zero corrections, CC and embedded Sketch repair remain covered by
  their mandatory/native/GUI regression paths.
- The broad placement and Sweep3D test fixtures now reflect the established
  first-reference orientation and nested-table resizing contracts. The protected
  placement implementation was not changed.
- Five-language coverage and catalog/UI validation pass. The stable root BAT
  continues to launch the local native C++ executable.
- Commit, push and signed Windows publication are authorized; exact artifact
  acceptance is tracked in [the release record](releases/2026111906.md).

The later Sheet Transition discussion supplied no concrete new behavior and
remains a future specification task. Linux execution belongs on the Linux host.

## Earlier repair evidence (before the completion batch)

### Two-sided Fillet route regression (`24.prtz`)

The saved production copy reproduced a preview identity defect: fused Start/End
side faces retain two valid authored aliases. The support packet previously
omitted both guides on the straight cap edges, leaving only the intermediate
Fillet arc in the preview. Its internal vertices were then incorrectly stored
as the whole route's endpoints. Resolve these guide supports using the selected
cap edge's exact Feature-side ancestry; retain both native face aliases.

After regeneration, actual GUI selection from each of the three members on
both caps of `24.prtz` calculates R = 4 mm with ZIMA. All six cases pass native
reopening, Undo/Redo and unchanged OK without another calculation. The private
GUI also verifies first/second OK for a deliberately stale terminal reference,
native reopening and Undo/Redo. Logs: `build/fillet24/gui-seed*.log` and
`stale-seed0.log`. The user's original is untouched.

The ordinary edge-treatment command regression now includes one-sided,
symmetric and unequal two-sided Features, both caps, forward/reversed routes,
three successive radii, complete preview supports/terminals, native cold
reopening and Undo/Redo. Explicit regeneration prunes missing members while
retaining a wholly unresolved route. It can rediscover new tangent interior
members only when every survivor and both persisted terminals match; partial
selections must not expand. Tests cover this repair, full reference loss and
deliberate partial selection. Removing the source corner round creates a sharp
right-angle route that the existing sweep cannot calculate; that definition
remains unresolved, and restoring the source automatically restores its arc
and successful calculation. This is not a claim that arbitrary sharp route
corners are now supported. Current native matrix log: `command4-tests.log`.
The ten rebuilt native regression suites pass (286.92 seconds total): direct
batch, support ancestry, planar Fillet/Chamfer, treatment command/query,
Sheet-from-Body exchange, Bend, Profile and unresolved confirmation. See
`build/fillet24/regression-tests.log`.

The final normal Windows build also passes all six actual-cap selections,
the stale-terminal first/second OK case, converted Bend face-intersection C
editing and all five UI languages. Logs are `build/fillet24/normal-*.log`.
The root `zima-cad.bat` launches this rebuilt local executable. These checks
do not constitute a packaged Windows release or Linux verification.

No new format, localized UI text, platform branch, or shared placement behavior
is introduced by these route repairs. Native aliases remain the source of
identity. Linux execution remains unverified.

The following results concern the current working changes, not an already
published release. Diagnostic logs and copies live under
`build/sheet-conversion-20261119/` and are not product dependencies.

- Native Bend end-profile matrix: 24 combinations of endpoint, native/external
  support, orientation, deleted/reference/suppressed difference dimension;
  successive numeric changes and permitted/locked drags, independent endpoint
  equations, reference identity, source immutability, persistence and Undo/Redo.
  The mandatory `zima_cpp_bend_end_constraints_contract` passes. Embedded GUI
  dimension/segment deletion, return to Properties, repair entry and Cancel
  pass. The converted-end GUI test adds an isolated preceding support Body to
  the diagnostic copy and verifies real face/plane intersection display,
  point-first C picking, independent support equations, return to Properties,
  commit and native reopening without recreating the deleted dimension or
  changing the source (`g-select16.log`). The attempted extrapolation of a
  distant finite cap intersection correctly rejects C; it is not an infinite
  construction line. This does not reproduce every possible face the user
  could have picked in the original transient draft.
- Conversion baseline: native tests cover computed/user-relative corrections,
  both endpoints, signed zero, repeated edits, source radius changes, release,
  dimension identity, cold/warm calculation and native reopening. Baseline
  changes preserve user deltas; release displays effective absolute values.
  Actual View edits of both output corrections through 0.25, -0.5 and 0,
  save/reopen and Undo/Redo pass. First-profile attachment dimensions are absent.
- Source deletion: converted-sheet tests cover source Fillet, source Extrusion
  and source Body removal, frozen geometry/frames, cold regeneration, native
  reopening and repeated Undo/Redo. RIP Sketch deletion also passes: one
  transaction, released children, unchanged volume, cold native reopening and
  two successive Undo/Redo cycles (`rip-deletion-tests.log`).
- Parallel with endpoint C: six actual GUI scenarios pass with slanted native
  and external supports, repeated drags, equations, read-only source checks,
  persistence and Undo/Redo. The native Parallel/Trim matrix and existing actual
  GUI Trim junction test pass, including parallel-fragment identity, dragging,
  persistence and Undo/Redo. The GUI fixture is horizontal; the exact angled
  gesture reported in 21.prtz has not been reproduced.
- Corner Closure: native Profile Bend combinations 00/01/10/11 and output
  corrections pass. Actual Bend View edits 10/01/00, persistence and Undo/Redo
  pass. No additional persisted corner state was introduced.
- The saved 23.prtz cursor ends at Body entry 4 while the converted group ends
  at entry 13. The actual Tree drag now restores all 13 rows and visible sheet
  surfaces. Source picking uses the approved metadata-only fallback order:
  converted child, original solid, source Fillet. Adjacent faces are no longer
  added as aliases of the child's material. Exact occlusion filtering of all
  original source surfaces is not claimed.
- Common unresolved-definition confirmation: native new/edit Fillet, Chamfer
  and Shell tests pass. Actual Fillet/Chamfer GUI tests pass for first-error OK,
  changed draft, unchanged second OK without another calculation, middle-button
  confirmation, save/reopen, Undo/Redo and unchanged valid OK. Both GUI tests
  also passed in the normal local `zima-cad-cpp.exe` launched by the root BAT
  (`confirmation-fillet-normal-gui2.log`, `confirmation-chamfer-normal-gui2.log`). Other command
  families and Assembly-owned commands remain verification/implementation
  gates; do not describe this as universal coverage yet.
- Six existing native Fillet/Chamfer suites pass after rebuilding: command,
  query, direct batch, support ancestry, planar Fillet and planar Chamfer
  (`fillet-regression-tests.log`). The new/edit confirmation test also verifies
  cold regeneration of the saved unresolved Fillet/Chamfer definition, not only
  loading its saved display cache (`confirmation-cold-tests.log`).
- Factory inventory: 1,185 native assets, 14 independent Bend assets, zero
  bound Sheet-from-Body assets, six start templates. All 14 Bend assets and
  four Part templates pass current calculation/save/reopen on isolated copies.
  Seven protected production Parts are rejected by both the previous runtime
  and the changed runtime as unsupported format; this is an existing gap, not
  verified compatibility. Verified backups exist and originals are unchanged.
  GUI template creation passes in millimetres and inches in all five languages:
  Part, Sheet, Skeleton and Assembly, active editable context and ordinary
  commands, component insertion and save/reopen (`g-template-mm.log`,
  `g-template-in.log`).
- Mandatory native independent-branch cases pass for separation, point/edge/face
  contact, overlap, containment and common-parent branches. The actual GUI
  attachment matrix passes 24 fragment/Flat/Bend/Profile/endpoint/midpoint cases
  (`g-branches.log`). Parallel calculation remains enabled.
- Localization coverage and catalog validation pass with the new common
  confirmation notice and Corner Closure strings in all five languages.
- The 19-test native regression batch passes 18 tests. The broad UI suite stops
  in `verify_container_frame_dialog`: Edge/Plane/Point has FRONT plus a
  position-only plane, while the test expects TOP as well. The affected shared
  placement path is unchanged by this follow-up and is protected; a separate
  old-runtime reproduction has not been performed. Do not weaken the test or
  change the shared placement contract to conceal this failure.
- The separate dialog layout matrix exercises 660 combinations; five fail for
  Sweep3D surplus-height allocation at 1920 pixels, one in each language.
  Sweep3D layout was not modified by this follow-up. Grouped-history Tree tests
  pass with expanded and collapsed conversion rows (`tree6.log`).
- Windows checks use the native C++ runtime. Linux has not been built or tested
  in this Windows session. No separate platform modeling implementation exists.

The generalized Sketcher Fillet and View-only Flat-end requirements below were
subsequently implemented in 2026111905. Historical failures and incomplete
coverage above describe the earlier repair batch; the current matrix records
the completed work and remaining finite verification limits.

## Sheet from Body follow-up

- Bound Bend first-profile attachment dimensions must not be exposed as
  corrections. The attachment stays on its source edge; editable corrections
  belong to the second/output profile and propagate to the following Flat.
  Identify the profile by role, never by a zero value: output +0/-0 corrections
  retain their independent side meaning. Preserve ordinary independent Bends
  and do not silently erase existing authored offsets.
- Bound Bend output correction dimensions use a user-relative zero. Preserve
  the computed conversion allowance separately from the authored user delta;
  the effective value is their sum. A source radius/geometry edit recalculates
  the allowance while preserving the user delta. After release/explode, retain
  the identical geometry and display the effective absolute value in the native
  independent Bend. Preserve signed-zero side identity, dimension IDs,
  save/reopen and Undo/Redo. This was requested on 2026-11-19; it is not merely
  a label change. Assess the persisted data contract before implementation.
  The user explicitly approved the needed native-format change on 2026-11-19.
  Inventory and verify affected factory assets/templates and isolated copies of
  the protected production project; preserve private originals.
- Ordinary Sheet Metal picking keeps converted Flats/Bends first. A source
  Fillet should be offered on its actual rounded face, not over an adjacent
  Flat. Use existing picking data without additional geometry calculation.
  The user accepts source Solid before Fillet as a fallback if exact filtering
  requires disproportionate work.
- Saved 23.prtz reproduces a disappearing result after moving Insert here
  before Sheet from Body, adding subtractive Extrusion 02, and returning the
  marker to the end. Investigate actual history cursor, calculation and scene
  publication independently. Use only the copy under
  `build/sheet-conversion-20261119/`, never overwrite the user's original.
- Deleting a source Solid/Fillet should release the dependent Sheet from Body
  using its existing release-bindings operation. Preserve the last valid sheet
  shape and ordinary editable Flat/Bend definitions. Deletion and release form
  one Undo transaction; Undo restores both the source and associations. Verify
  regeneration, repair, repeated Undo/Redo and native save/reopen.

## Save an unresolved modeling definition

The user requested this common behavior for modeling commands:

1. The first OK attempts the ordinary calculation. On failure, keep the draft
   open, show the actual error, and explain that a second OK will save the
   unresolved definition.
2. A second OK with the exact same draft saves that definition in one Undo
   transaction, closes the dialog and marks the history feature as invalid.
3. Changing parameters or references clears this acknowledgement. The next OK
   must attempt calculation again. A changed source/input revision must also
   invalidate acknowledgement of an earlier failure.
4. The implementation retains the real input body and authored unresolved
   definition, with existing last-valid-frame handling for broken references.
   It does not persist an edited feature's former successful result as an
   additional display solid. Dependent features must recognize the unresolved
   dependency; retained input geometry must not masquerade as a successful
   new material calculation.
5. Cancel discards pending edits. Save/reopen and Undo/Redo must preserve both
   the authored definition and its unresolved state.

Trace the shared command and history transaction paths before implementation.
Reuse their error/status and last-valid-result mechanisms, preserve the protected
placement solver, and assess whether any required persisted contract changes.
If the native format must change, obtain the existing format-scope agreement
before changing it. Review all applicable modeling command families rather than
adding a Fillet-only confirmation shortcut. Localize feedback in all five
languages and verify the common middle-button confirmation path.

## Reported Fillet failure in 21.prtz

The user reports that a formerly working case fails with ZIMA and OCCT Fillet.
Their failed pending definition cannot currently be saved because OK fails;
the two-step confirmation above addresses that reproducibility obstacle.

A read-only copy of the file saved at 13:05 on 2026-11-19 was obtained. Its
SHA-256 is
`65a739ae6e6e27b0396b36a578d7cb1ceeebd4fd039137d384da4e7f005a1222`.
It contains an Extrusion and ZIMA `Zaoblení 01`, radius 6 mm, with both route
ends set to Cut. Three warm
and three cold recalculations of this saved definition passed, preserving
volume 3541629.6975073777 mm3 and reference status, including native reopen.
This does not reproduce or disprove the unsaved failed attempt. The failing
radius/route/other pending changes remain unknown. Do not invent them or
rewrite the original user model.

Private diagnostic artifacts remain outside Git under
`build/sketch-followup-20261119/`. Before accepting a repair, compare the
previously working Fillet/Chamfer catalog, dependent reference behavior and
transaction recovery as well as the exact newly captured failed definition.

## Sketcher contact, Trim and rounding

### Bend end-profile editing and intersection reference

The user became trapped in the embedded Bend end-profile Sketch of 23.prtz
after deleting an endpoint dimension and attempting C contact with an
intersection of a Flat face. Finishing the Sketch was reproduced in the live
application: `Bend endpoint difference dimension is missing.` The outer window
also refused to close until the edit was completed or cancelled. A Save As copy
was captured under `build/sheet-conversion-20261119/23-end-profile-pending.prtz`;
this saves the committed model, not necessarily its transient embedded draft.

- An endpoint may be driven by native Sketch geometry/constraints instead of
  the generated difference dimension. Preserve the segment/point identities
  and actual solved endpoint; do not recreate a deleted driving dimension.
- An incomplete or invalid embedded Sketch must allow return to its owning
  Properties and Cancel. Validation belongs to the attempted model commit;
  an error must not trap the user in a hidden-parent edit session.
- The face/Sketch-plane intersection must appear as an ordinary reference
  curve in the Sketch plane and participate in C contact through its stable,
  read-only external reference. Verify display, picking and solving separately.
- Verify draft Undo/Redo, repeated endpoint edits and drags, replacement of a
  dimension with C on native/external support, regeneration and save/reopen.
  Add the missing-dimension exit case to GUI regression coverage.

The user separately reported, while constructing the angled outline in 21.prtz:

- Finishing a segment with endpoint C on a support line should still offer
  Parallel to another slanted segment. Contact inference must not suppress a
  compatible directional relation.
- Parallelism was reportedly lost after Trim. Existing native Parallel Trim
  tests already cover several cases; reproduce the actual gesture and graph
  before changing the restoration path or assuming the relation was persisted.
- Preserve established virtual corner rounding. Generalization to line/arc,
  spline and elliptical-arc supports is separate follow-up work.

The accepted separate Sketcher Fillet command takes two curves and a one-time
initial radius, trims the curves and inserts a real tangent arc. Do not create
an automatic driving radius dimension. The user may add dimensions or Equal
Radius later. Preserve native shared-point incidence and independent contacts;
reject unsuccessful attempts atomically. Use one Undo transaction.

Follow the mandatory Sketcher matrix: independent equations, repeated numeric
edits and point drags, applicable dimension/lock/DOF states, both selection
orders, orientations, contact sides, native/read-only external supports, Trim,
save/reopen, Undo/Redo and actual GUI interaction. Record unverified variants.
After the agreed Sketcher work, revisit the sheet transition with the user.

## View-only Sheet Profile end correction

The earlier discussion remains design work: control the end of a generated Flat
using ordinary View dimensions and stable dimension IDs consumed by sequential
calculation. Respect the target surface, profile angle and full material
thickness so material does not protrude through the target. The following Bend
must inherit the corrected endpoint. No additional Properties fields were
requested. This is not included in release 2026111904.

## Bend corner closure in the View

The user clarified and approved the exact View interaction on 2026-11-19:
show `CC = 0` or `CC = 1` beside each of the Bend's two endpoint locations
(`CC` means Corner Closure). Zero disables and one enables that endpoint's
existing corner flag. Double-click edits the value through the ordinary
parameter interaction. Hover explains the full localized Corner Closure name.
Reuse the existing First end / Second end settings and calculation; do not add
a new Properties control or duplicate state. Make this work for generated
Profile Bends as well, preserving both endpoint identities, user corrections,
downstream propagation, native persistence, Cancel and Undo/Redo. Localize the
full label/tooltip in cs/en/de/fr/ru; the agreed CC abbreviation is shared.
