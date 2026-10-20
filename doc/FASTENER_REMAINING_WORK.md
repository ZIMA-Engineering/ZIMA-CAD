# Remaining fastener authoring - 2026-10-20

Windows 2026102002 is published and immutable. The following work is authorized
for a subsequent version. This is a completion checklist, not a claim that these
families already contain accepted native models.

| Family | Current state | Remaining work |
| --- | --- | --- |
| Historical wing nuts | Six pressed M3-M10 Parts installed; four private cast studies remain | The accepted pressed path moves the axisymmetric hub seam out of the wing plane. The exact failing case, 210 successive edits, M3/M10 mouse edits, six native Drawings and five-language pages passed; see [acceptance](PRESSED_WING_NUT_LIBRARY.md). Earlier private candidates remain rejected. Cast perimeter/root transitions, M16 source-radius disagreement and M24 indicative mass remain outstanding. |
| Non-metallic insert lock nuts | Thirteen ISO 10511:2012 Parts, M3-M36, installed | User-selected single steel Part with a blue symbolic insert Body; both use steel density. All thirteen physical checks and 143 successive edit/persistence checks passed. M3/M36 native GUI scenarios and six native Drawings passed. Five-language tables retain the separate archived CSN dimensions. See [acceptance scope](INSERT_LOCK_NUT_LIBRARY.md); committed-source/release acceptance remains pending with the other families. |
| Formed-nose tab washers | Thirteen formed M4-M27 native Flat/Bend/Flat Parts installed beside the preserved flat family | Original CSN 02 1753 (1962) preview reviewed; M4/M22 outside diameters differ from the archived shared table. All 273 successive edits, original identities, actual M4/M27 GUI edits, unfolding, thirteen seating Assemblies and six framed dimensioned Drawings passed. See [acceptance](FORMED_NOSE_WASHER_LIBRARY.md); committed-source/release acceptance remains pending. |
| Toothed and serrated washers | Archived dimensions/illustration; no factory models | Review the three distinct historical constructions and available ISO correspondence; author actual teeth, slits and formed geometry without substituting an ordinary plain washer; verify dependent edits and drawings. |
| Spherical wheel washers | All five compressed native bores 12.5-22.5 mm installed | The two smallest explicitly select R/h6 within the original ±0.5 mm radius and +1/0 mm height tolerances. Each pair of additions passed 30 successive edits, actual GUI and six Drawings; the small pair has the company frame/title block at 2:1. All 32 preceding binary assets are unchanged. See [finite acceptance](verification/20261020-shaped-fasteners.md). |
| Two-part locking nuts | Table only | Explicitly excluded by the user on 2026-10-20; do not implement in this follow-up. |
| Native Linux execution | Shared source and factory data; Windows verification exists | Native Linux build, GUI/regression execution and package acceptance require a Linux host; a Windows layout simulation is not native Linux verification. |

Every installed model must pass explicit Regenerate, independent physical mass,
saved Relations, repeated dimension changes, native save/reopen and Undo/Redo.
Representative actual GUI editing, native axes and associative dimensioned
reference drawings are required. New names, descriptions and tables cover cs,
en, de, fr and ru. Keep custom user libraries and unrelated working changes.

The already installed cap, coupling, flange, spherical/conical wheel, knurled
and weld-nut families are not outstanding model-authoring work. Their finite
verification gaps remain documented in their own records. Known unresolved
source facts must not be disguised by a different density, radius, standard
designation, geometry tolerance or a copied indicative catalog mass.

The lock-nut representation decision was changed explicitly by the user on
2026-10-20: use one steel Part and indicate the insert by color, without an
Assembly or per-Body material extension. Its reported mass is a documented
homogeneous-steel approximation, including the symbolic insert. It is not a
claim of the physical mass of an actual steel/PA6 product.

After accepted installation, update the documentation and translations, commit
and push the reviewed files, then create and verify a new signed Windows release
through the repository distribution pipeline. Do not replace 2026102002 assets
or move its source tag.

## Requested follow-up after nut completion

### Program corrections reported during completion (2026-10-20)

- The approved Sketch reference correction now preserves native Circle/Arc
  semantics for exact circular projections in External Reference and Reference
  to contour. Noncircular projections retain exact NURBS. The approved native
  projection metadata preserves corrections, arc direction and circle seams
  after Trim. Lost or incompatible sources retain the last usable support and
  owned geometry with an unresolved identity; the same source can repair the
  link and explicit detachment preserves the owned shape. Native equations,
  repeated dimensions/drags, native persistence, Undo/Redo, real contour/tangent
  GUI, affected factory data, start templates and all five language catalogs
  passed. See [verification](verification/20261020-external-circular-profiles.md)
  for the finite matrix, separate axis-point policy and Linux/package gaps.
- Disk-space emergency: removed two unsigned candidate ZIP copies, a disposable
  portable-font runtime, exact checksum-verified 2026101906 test source/runtime
  copies and four obsolete local signed archives. The complete latest 2026102002
  verification installation was retained after automatic approval review rejected
  removing its source copy. Approximately 10.4 GiB of generated copies were
  removed; the observed free space afterward was 12.5 GiB. Current development
  binaries, dependencies, user settings, projects, source and factory libraries
  were preserved. Local receipts: `build/disk-cleanup-20261020-*`.
- User file `Projects/31.prtz`: two unlocked driving centre-distance dimensions
  in three equal circles were rejected despite two remaining degrees of freedom.
  The same synthetic case failed before the correction and passes afterward.
  The circular simultaneous fitter had restricted EqualRadius to arcs. Adding
  native circle/circle pairs fixes both actual file edits while preserving the
  existing mixed circle/arc rejection path. Locked-distance centre-drag
  projection also needs that fit when sequential projection fails. The finite
  256-case native matrix, 64 additional base-centre drag cases and actual-file
  GUI checks pass. See [verification](verification/20261020-equal-circle-dimension-edits.md)
  for independent equations, repeated edits, persistence and coverage gaps.
- The user remodeled `PWN-CSN021665-M10.0.prtz` interactively: symmetric wing
  placement and inward root rounding, contained by the central hub. Preserve the
  user's changed asset and compare its authored history against Git before
  revising the generator or restoring dimension-dependent Relations. The user
  removed the driving dimension Relations because deleting an Fx dimension
  prevented Save; the saved copy retains its mass and stock expressions.
- Approved desired missing-Relations behavior: preserve authored source, skip
  unavailable dimension assignments and dependent expressions, identify affected
  rows, and still evaluate independent valid physical parameters during Save.
  Save must not change driving geometry. The transient Relation subset,
  dependency propagation and editor diagnostics are implemented; native Save,
  Undo/Redo, save/reopen, real dialog and all five language checks pass. See
  [verification](verification/20261020-missing-dimension-relations.md).
  Do not silently discard all Relations or treat stale dependent values as
  newly calculated. Package acceptance remains pending.
- New requested shared contextual command helper, named **Vladimír** by the
  user: extend the existing green Section guidance into reusable command-state
  guidance. Show the active task, expected next selection, rejection reason and
  confirmation/cancellation controls from actual command state. Address solver
  and Relations safety first; preserve protected shared placement contracts and
  localize every new user-visible message in all five supported languages.
  The agreed presentation is upper-left text without any background panel,
  Light/Dark-aware main text and a green name accent. Keep the tone positive,
  factual and actionable. Add global `Vladimir=YES/NO`, default `YES`, with an
  immediately effective control in Global Settings.
  The shared status-based first implementation and Relations guidance now pass
  native widget and integrated main-window checks, all five languages and both
  themes. See [verification](verification/20261020-vladimir-command-guidance.md)
  for scope and remaining coverage; this is not a claim of bespoke guidance for
  every command. Release-package and native Linux acceptance remain pending.

- An update on another machine could show Engineering Tables as an unavailable
  directory. Reproduction found that File Manager resolved factory source paths
  against shared writable configuration instead of the running version's factory
  configuration. Version ownership and missing-source completion are corrected;
  custom paths, order and disabled states are preserved. Portable settings,
  actual File Manager navigation and an isolated signed update introducing a
  new directory passed. See [verification](verification/20261020-factory-source-updates.md).
  Acceptance in the next committed release package remains pending.
- Sheet Transition now inherits document thickness unless **Custom thickness**
  is enabled. The user approved the required native switch. Round and L/U
  transitions, volume/reference identity, Family/Relations, sheet states,
  Undo/Redo, native persistence, templates, actual GUI, units and all five
  languages passed. Open Properties also refresh their existing stylesheet on
  a live theme change. See [verification](verification/20261020-transition-thickness.md).
  Acceptance in the next committed Windows package remains pending.
- External circular/elliptical references need their own selectable center
  crosses. The user approved a persistent center-reference contract, then
  clarified the interaction: select **center + center for C**, and separately
  **curve + curve for equal radii** on circles/arcs. Do not reinterpret selecting
  two circumferences for C as implicit concentricity. Include automatic
  center/radius snapping and ellipse centers; verify dimensions, repeated drags,
  source updates/loss, Undo/Redo and native persistence.

The user's latest order is to complete these current program fixes, update
English documentation and all five locales, commit/push and publish a verified
new Windows release, then resume the unfinished library and modeling work above.

### Further ideas collected during program corrections (2026-10-20)

These are deferred design directions, not implemented capabilities or approval
to change protected persisted contracts. Current corrections and the agreed
library/release order retain priority.

- FEM: the user proposes another Body type inside Part. Scope the analysis,
  mesh ownership, boundary conditions, materials and result storage separately
  before choosing a solver or native representation.
- Voice input for the CAD AI: investigate speech decoding and synchronization
  with the actual hovered/selected object while the user speaks. Keep ordinary
  pointer interaction responsive; do not add continuous geometry calculation
  or costly full-scene capture to mouse movement. Record enough temporal and
  semantic context to resolve pointing without guessing ambiguous references.
  No decoder or external service has been selected.
- Free mesh modeling: the user wants Blender-like direct movement of individual
  or grouped control points, local refinement and optional point constraints.
  Refinement should initially preserve the shape; subsequent editing provides
  local control. Rounding mesh edges should produce editable mesh geometry.
  Do not force every edit into the existing parametric Feature history.
  The user's target applications are shaping an automobile body and a boat
  keel. This establishes smooth engineering surfaces as the intended output,
  with the mesh as an editing means. Evaluate Sketch sections/guides, symmetry,
  boundary and curvature continuity, and accurate native surface conversion
  against representative examples. Faceted conversion alone does not establish
  acceptance of these smooth-surface use cases. Specific continuity targets,
  accuracy and manufacturing requirements remain to be scoped with examples.
- Preserve the established classification of parametric geometry. The agreed
  direction is a separate modeling environment, provisionally **Surfaces**,
  for direct mesh editing. Its Tree lists mesh objects such as **Mesh 1** and
  **Mesh 2**, rather than each point edit. Merging Mesh 2 into Mesh 1 leaves
  Mesh 1 as the result, with Undo/Redo for the merge. The exact native document
  ownership and use of the resulting shape in Part remain open design work.
  This supersedes the initial suggestion to make each mesh rounding operation
  an ordinary history Feature. It does not redefine the separate FEM proposal.
  The user subsequently clarified that a mesh should normally be authored in
  its own Body to keep it organized, while a normal Body may also contain mesh
  geometry alongside parametric geometry. The editing environment must not
  impose an exclusive separate-document restriction. Mixed-Body evaluation,
  dependencies and conversion behavior remain to be designed explicitly.
  Associative constraints from mesh points to native parametric solid geometry
  are a required capability: preserve exact vertex/edge/face ownership and side
  identity, follow source changes during explicit regeneration, and report lost
  references without silently rebinding to another object. The user emphasized
  that these links provide the necessary connection to existing CAD geometry.
- Subsequent brainstorming remains provisional rather than a closed feature
  specification. Mesh editing uses dedicated commands; ordinary Extrusion and
  Revolution do not directly modify mesh geometry. **Create mesh** starts from
  selected faces or a complete Body and produces an independent mesh of the
  current shape. It creates no automatic source-following link. Points follow
  a native surface only after the user explicitly adds that constraint.
- Point groups belong below their mesh in the Tree. They identify sets of
  points for subsequent selection and editing. The user clarified that these
  are point groups, rather than groups of whole meshes. A rounding command
  could collect its resulting points in a named group such as **Rounding 1**;
  that idea does not imply a parametric history operation.
- Other candidate commands include basic mesh objects (Cube, sphere, cylinder,
  cone, plane and torus) and subdivision. Distinguish shape-preserving local
  refinement from subdivision with smoothing. Connected faces share native mesh
  vertices and edges; edits must retain that connectivity and explicit point
  constraints as applicable. Primitive details and exact topology are open.
- Sketch-guided deformation: draw a Sketch curve, select a mesh region and use
  the curve as a shaping guide, for example for bending or an indentation.
  Consider adjustable influence and a smooth transition into the surrounding
  mesh. The user accepted distinguishing a one-time deformation from an
  explicitly requested associative link that follows later Sketch changes.
  No deformation algorithm, persistence extension or implementation is selected.
- Mesh conversion must eventually produce actual native BRep Shell or Solid
  geometry, so that the freely shaped result can enter ordinary CAD workflows.
  A Solid requires a closed, valid shell. Distinguish preservation of mesh
  facets from reconstruction of smooth surfaces with an explicit geometric
  tolerance. Validate boundaries, connectivity and self-intersections; do not
  present an arbitrary mesh as a verified solid. Conversion scope, algorithms,
  source/result association and persisted representation remain design work.
- AI-assisted generative design is a further target use case, including parts
  for 3D printing. The user specifies functional interfaces, for example a pin
  at A and attachments at B/C. The future workflow also needs explicit loads,
  supports, permitted/protected space, material, manufacturing constraints and
  an optimization objective such as minimum mass subject to strength/stiffness
  requirements. Keep these engineering inputs distinct from the editable mesh.
  Candidate generation, mesh shaping, FEM evaluation and manufacturing checks
  form an iterative loop, with the required interfaces retained. Native geometry
  and print export are downstream outputs. This is a desired workflow, not a
  verified optimizer, FEM implementation or guarantee of a global optimum.

### Active drawing correction, requested on 2026-10-20

The user requested independently selectable horizontal and vertical strokes of
an end-on hole-axis cross for manual angular, linear and chain dimensions.
Previously the renderer drew the cross while the measurement model retained only
the projected centre of the physical 3D axis. A physical axis viewed end-on must
not silently acquire an arbitrary line direction. The two displayed strokes need
distinct associative identities, retaining the current centre attachment.
The user explicitly approved this extension of persisted Drawing reference
semantics on 2026-10-20. Part/Assembly formats and protected container
placement are outside its scope. Verify both strokes and selection orders,
repeated occurrences, edge/axis combinations, exact hover highlighting, command
Cancel, Undo/Redo, regeneration, visibility, camera and scale changes, and native
save/reopen. References that cease to exist must become unresolved rather than
silently rebinding to another direction. This work has program priority; keep
the fastener completion list active. The implementation and finite Windows
native/GUI checks are complete; see [verification](DRAWING_AXIS_CROSS_DIMENSIONS.md).
Native Linux and release-package acceptance remain pending.

The user also asked about balloon attachment at arbitrary curve locations and
inside a component face. The common measurement picker already has continuous
curve contacts for symbols, but balloon entry does not enable that option.
Face attachment needs exact source-face and occurrence ownership, visible-depth
picking, associative contact persistence and BOM row resolution. The user then
explicitly limited the follow-up to arbitrary curve contacts, after completing
the ongoing work. Implement that smaller extension using the existing curve
contact path. Do not implement face attachment in this task. The curve extension
and native/GUI checks are complete; see
[curve-contact verification](DRAWING_BALLOON_CURVE_CONTACTS.md).

### Corrected private geometry diagnostic

A parametric external fan-washer prototype subsequently reproduced a separate
program defect in midpoint-centred rectangular Sketches: their initial solve
passed, but repeated width/thickness edits and real off-path mouse drags did not.
Program correction takes priority over installing those private candidates.
The bounded correction and finite regression matrix are recorded in
[midpoint profile verification](verification/20261020-midpoint-profile-editing.md).
It adds no native format or protected placement contract change.

Save As intentionally exports a copy and keeps the source document active.
The initial private hub trace mistakenly continued in that source session.
Its suppression snapshots therefore did not isolate operations, and its modified
private M3 source candidates are not eligible for installation. The preserved
pre-edit exports in `build/pressed-hub-trace-v1` retain the original full histories.
No factory library was edited by that trace.

The corrected `tmp/trace-pressed-hub.py` explicitly checks the source remains
active after Save As, closes it, reopens the exported copy, and verifies the
active absolute path before each mutation. It checks SHA-256 before/after and
after every stage. `build/pressed-hub-trace-guarded-v1/results.json` confirms
both source exports were unchanged. The corrected trace reproduces the geometry
problem: the hub alone has volume 58.556379 mm3, but its Add leaves the failing
wing-only volume unchanged at 112.233575 mm3. The baseline union is 155.257610
mm3. The final through-hole volumes are respectively 109.542644 and 143.859885
mm3. Both failed and baseline final outputs contain one valid solid, so ordinary
BRep validity alone is insufficient to establish an equivalent union.

Changing Boolean fuzz, non-destructive mode, inverted-solid checking, argument
order or regularity flags did not resolve the isolated failing union. A private
analytic-endpoint experiment changed the result into an open shell and was
reverted; it is not a product fix. The independent inspector now reports actual
solid count and material-property state as well as volume. Product geometry and
Save As behavior remain unchanged. Further native authoring must use fresh
candidates and repeat acceptance checks; these private probes are not release
evidence.

On 2026-10-20 the user proposed extending the modeling Feature's **Up To** end
condition to multiple selected faces, including disconnected faces, with a
signed positive/negative correction from the resulting contact. Complete the
current nuts first. This is a deferred feature request, not implemented behavior
and not a reason to alter the ongoing fastener assets.

Before implementation, trace the current Feature endpoint/target collection,
kernel limiting operation, GUI reference-entry controls, persisted end condition
and regeneration paths. Resolve the treatment of gaps, missing intersections,
overlapping or differently oriented supports and which local part of the profile
terminates on which selected face. Reuse the shared reference-entry controls and
preserve the permanent geometry-side identity, including zero correction. Scope
ordinary Extrusion/Revolution and any other applicable Feature modes explicitly.
Read the performance and geometry-side contracts, and obtain the separately
required approval before any protected shared container-placement change or
native persisted-contract extension. Merely consuming existing target lists
does not authorize changing those contracts.

The user clarified and agreed the intended surface ordering on 2026-10-20:
targets form a consecutive chain. The first surface intersects the second,
the second intersects the third, and so on; adjacent intersections determine
the joined limiting shape. This is not a per-ray priority list with fallback
to the next target. Retain selection order, with explicit Up/Down reordering
through the existing shared list controls. Validate adjacency and identify
missing intersections. The retained side of each patch and whether underlying
supports may extend beyond selected face bounds still require explicit design.
After the libraries, finish English documentation and all five translations,
commit and push, then verify and publish the Windows release before beginning
this Feature work. Include subtractive Features limited by shaped supports.

The user also requested considering **Sketch projection onto a face** through
the same Feature interface. Candidate inputs are a source planar Sketch, one or
more target faces and a projection direction (Sketch normal or a selected axis).
For curved supports, the result must be actual associative 3D curves, not a
planar Sketch pretending to lie on a curved surface. Review existing Surface
Feature and 3D-curve paths before adding a mode. Specify clipping to actual face
boundaries, gaps, multiple intersections, misses and tangential singularities;
retain source identities and explicitly reject ambiguous unsupported cases.
Regeneration must follow the source Sketch and target geometry. Splitting a
face or using the curves for a later cut are downstream operations to scope
separately. The interface proposal is recorded; no projection feature has been
implemented or verified yet.

The user reaffirmed retaining the existing Feature principle: source Sketch,
Extrusion or Revolution path, Up To targets, signed correction and chosen result.
Use the existing Feature interaction model when evaluating this extension.
The discussed visibility default is to show the derived result, retain the source
Sketch hidden in the Tree, and expose both in the editing context with ordinary
visibility controls. At zero correction the projected result lies on its target;
nonzero correction offsets the endpoint along the operation path. These are
design intentions to verify, not an implemented promise.
