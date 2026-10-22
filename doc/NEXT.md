# Development follow-ups and historical design notes

## Current work index - 2026-10-22

Use [current project status](PROJECT_STATUS.md) for implemented capabilities,
development changes awaiting release and the actual remaining queue. Family Table,
multibody Parts, Sketch dragging/Trim/offsets, native text/symbol editing,
2D/3D/helical Sweep, Sections, surface tools, sheet-metal states, DXF workflows
and document Undo/Redo already exist. Their former foundation requests are not
new TODOs. Detailed dated notes below preserve design context and verification
ideas; they do not override the current contracts or newer user instructions.

Finish current corrections and verified Windows release first. The user's
2026-10-22 next-session order is recorded below and takes precedence over older
queue notes. The ordered Up To surface chain and signed correction, surface projection, File
Manager prototypes, remaining library studies, FEM, direct meshes and voice/pointer
context retain the scope and order in the [current queue](FASTENER_REMAINING_WORK.md).
Two-part locking nuts and STEP-derived Sketch offsets are outside that scope.

These accumulated design notes include historical stages. Check current focused
contracts and [ROADMAP.md](../ROADMAP.md) before selecting work; newer explicit
user instructions and completed native features supersede older pending items.

## Restart handoff - 2026-10-22

### Additional user steering before publication

The user explicitly requested these additional corrections in the same batch:

1. Explicit Symmetric constraint: first-point hover works, second-point hover
   is hidden although clicking it and selecting the axis creates the relation.
   Trace command-scoped confirmed selection; do not change solver equations.
2. Slot: retain a visible first-centre marker while choosing the second centre.
   Prefer an applicable Symmetric offer over C and H/V, using the existing
   X/Y/construction-axis symmetry inference. Preview and confirmation must agree;
   preserve the ordinary snaps where symmetry does not apply. Verify dimensions,
   successive edits/drags, native reopen and Undo/Redo with the existing matrix.
3. The user approved trying a true numeric Feature direction parameter (+1/-1)
   visible with the Feature parameters and available to Relations. Crossing zero
   with the purple handle and entering a negative extent should consume the same
   existing independent side-reversal state. Preserve panel identities/values,
   locks and references. Relation recomputation must set a deterministic direction,
   never repeatedly toggle it. Trace existing bindings and dimensional units first;
   reuse the persisted reversal flag without adding a native field if possible.
   The subsequent explicit request includes Revolution: negative angle and drag
   across zero reverse direction. Two numeric Revolution sides must not pass
   each other while dragging; clamp the edited side rather than push the other
   panel. Mixed Extrusion/Revolution does not need that angular restriction.
4. Feature double-click dimension inspection: after a single click/unchanged
   dimension edit, double-click no longer hides dimensions until Regenerate.
   Reproduce both the click and unchanged-editor paths; retain no-op document and
   calculation behavior. Do not use regeneration as a selection-state repair.
5. New Feature defaults: Point and Text unchecked for every Feature type.
   Preserve stored choices when opening existing definitions. The persisted
   fields are already mandatory; changing construction defaults must not
   reinterpret loaded models or require a format change.
6. Hole direction after local-XY/camera changes: a newly face-attached Hole
   appears to cut outward. Trace the actual selected normal/material side and
   command-local defaults; protect persisted sides and existing hole geometry.
7. Sketch Reference cannot consume a Feature axis, including Sweep. Check
   picking and native axis-to-reference conversion, retaining source identity
   and read-only ownership. Reuse existing external analytical line support.
8. Last Drill Point in the user-owned `POKUS.prtz`: partial planar hole bottoms
   cannot receive a drill tip. Work only on a copy and preserve the original
   hash. Investigate a cone defined by the original bore axis/radius and clipped
   by the real current Body, with explicit source identity and ambiguity checks.

The user explicitly made this entire list a pre-publication gate on 2026-10-22.
Do not release the earlier 63da743 binary before these requests are handled.

The user subsequently requires testing the normal Windows build personally
before publication. Do not publish automatically after the automated checks.
The Opening defect is the **last Opening in `Projects/POKUS.prtz`**, placed by
an axis and the opposite end face. Both axes have +Z direction; only one entry
face is compatible with a fixed world -Z bore. See
[the focused diagnosis](OPENING_AXIS_ENTRY_DIRECTION.md) and
[the current verification report](PRE_RELEASE_CORRECTIONS_20261022.md).
The user considered pointing the Opening Origin axis inward but did not require
that change. Preserve the common Origin/placement contract; initialize the new
Opening's existing direction from the entry side instead.

At 12:18 on 2026-10-22 the focused corrections are built in the normal
`build/cpp-windows-release` GUI/CLI. Slot 34-case GUI, Symmetry hover, Feature
inspection/defaults, Feature/Sweep axis Reference, partial Drill Point, Opening
axis entry and existing Opening/reference/whole-Origin checks pass. Localization
coverage and all five language UI checks pass. Exact evidence and earlier probe
failures are recorded in the verification report above. The user has been told
these corrections can be tried through the normal `zima-cad.bat`.
Current corrections and numeric-direction changes remain uncommitted on main;
preserve private config/library/user artifacts when staging the final batch.

Subsequent progress: numeric direction, Relations and negative linear extent
entry pass native and actual mouse/View GUI checks in the normal build; see
[the plan and exact evidence](FEATURE_DIRECTION_PARAMETER_PLAN.md). The user's
subsequent Revolution extension passes, including stopping a dragged numeric
angle at the opposite end without changing its panel, negative View angles,
32 exact end contacts and 64 gaps at suitable requested precision. Isolated
regeneration of all 1,046 Part-based factory assets passes; verified files are
installed while preserving the user's two modified factory working definitions.
Six templates are refreshed and template/new-document/FORM/library GUI checks
pass. Normal Windows GUI/CLI and localization/unit checks pass. The user has
been invited to test `zima-cad.bat`; publication still waits for that response.

On 2026-10-22 the user explicitly requested direct changes in the normal version,
without another experimental build, to save disk space. Use the existing main
build for development. The required committed-source release staging is separate;
do not publish 2026102201 until this current scope has been verified.

The whole-Origin correction is complete and pushed as
`63da7430d3749c9f4b2d8d9a87211b65f4dfb3cf`; its normal build/GUI smoke passes.
The release tag is still local/unpublished. Candidate reuse failed before source
replacement during its second temporary committed-source export (disk full);
`C:/zcb/p2201/c` remains on its previous 28c74c0 source/build for verified reuse.
The user approved cleanup. Old signed 2001/2002/2003 local ZIPs were retired after
identity checks; latest verified signed 2004 is retained. Old generated JSON
diagnostics were gzip-compressed with round-trip SHA-256 verification. Imports
of 372 Release modules were checked before removing 160 unused Debug Qt copies
from generated main/fastener runtime folders (original Qt SDK retained). C: then
had approximately 13 GB free. Reports are under `build/disk-space-*20261022*`.
No user projects, private library variants, source files or current Release
dependencies were removed. Main runtime smoke after that cleanup remains due.

The user plans a PC restart after the current work is completed. Confirm that
the normal repository-root `zima-cad.bat` is ready before inviting testing. Keep
that launcher stable. The current build identity is `2026102201`; release
acceptance/publication status is in [its record](releases/2026102201.md) and the
[Feature direction verification](verification/20261022-feature-side-reversal.md).
Check those records and Git status after restart rather than assuming a running
build or process survived.

The user's subsequent test found a whole-Origin profile orientation defect.
The approved correction is implemented and verified; see [its GUI reproduction
and acceptance](PROFILE_WHOLE_ORIGIN_CORRECTION.md). Feature and Sketch now inherit
aligned positive axes through the button and Tree paths. Stored models are not
rewritten. Windows publication still requires final package acceptance. The
original release candidate compiled but its
private package source copy exhausted C:. Only that incomplete package copy
was removed. The candidate build/source stage remains at `C:/zcb/p2201/c` for
verified timestamp-preserving reuse after the final approved fix commit.

The accepted selection contract is the actual analytical creation/Properties
preview wire, at normal width and visible through the model. Whole-solid hover,
click and inspection never fill faces. Individual placement/reference selection
retains its exact offered current Face fragment and wire boundary. Resolve the
original ancestry only for reference storage. Do not restore old result-edge
selection heuristics or extra thick azure strokes.

Next, continue the approved [Sketch intersection plan](SKETCH_INTERSECTION_REFERENCE_PLAN.md):
Face × Sketch plane, multiple disconnected branches with stable selected-branch
identity, and Reference/Intersection-to-outline integration with native analytic
circle/arc preservation. The native 51-case calculation foundation exists;
the editor commands, complete persisted branch definition and their five-language
UI are unfinished. The user approved that native-format scope on 2026-10-21;
do not request the same approval again. Required start-template and affected
factory-asset regeneration still apply when implementing the final definition.

Then finish the H/V view adapter for quarter-turns and flipped Sketch views,
preserving internal constraint semantics, and the remaining simultaneous H/V
offers while entering the second segment endpoint. Endpoint equality and rounded
corner annotation corrections already have finite native/GUI regression
evidence; do not restart them as new features. Follow the point-junction and
dimension/edit/drag matrix rules in AGENTS.md.

The user data in `1000.prtz` and `L30X30X3` must be re-identified from current
files before further reproduction; use private test copies. Preserve the user's
modified `PWN-CSN021665-M10.0.prtz`, its artwork, `ZE-TEXT.symz`, metadata and
other uncommitted work. The release factory verification uses the accepted Git
definitions separately. Interoperability bookmarks are in
[external resources](EXTERNAL_RESOURCES.md).

Disk space is limited. The 2026-10-22 cleanup removed only two superseded unsigned
candidate ZIPs after verifying retained signed archives, and losslessly compressed
old large diagnostic JSONL files with SHA-256 readback. It reclaimed about 3 GB.
Do not delete current Qt/OCCT SDKs, user projects or retained signed releases.
Avoid accumulating candidate extractions and bulk fixture copies. Linux execution
is still unverified; its completion requires the Linux host.

## Performance and Scaling

### Next session agreed on 2026-09-26

- **Implemented locally (2026-09-26):** ordinary Qt Tree selection, tabs,
  menus and buttons now use the platform style. Explicit CAD hover/input states
  are azure; confirmed geometry, inspection, pending wire and active/editing
  Tree rows are green. Icon accents and separators were audited together;
  persisted Drawing/symbol pens retain their original colors. Confirmation
  checkmarks remain green on hover. See [visual conventions](UI_VISUAL_CONVENTIONS.md).
  UI, Drawing UI, five-language catalog/UI validation, whole-Origin placement,
  selection filters and a Windows 11 interaction check passed. This is a local
  development update; the published Windows build 2026092601 is unchanged.
  Follow-up: neutral icons/reference text now follow live light/dark theme
  changes; Insert here markers are red. Theme switching, selected-icon contrast,
  UI, Drawing and five-language checks passed.
- Complete Point placement persistence on circular/curved surfaces. Preserve
  the intuitive first View click at the picked position and numeric locks;
  verify reopening, regeneration, Undo/Redo and both surface sides.
- Complete engineering symbol support, particularly geometric tolerances,
  datum-related annotations and weld symbols, using the existing native
  annotation and localization contracts.
- General NURBS surface placement remains future work when native surface
  support is available; imported general STEP surfaces need their own persisted
  support representation. Do not infer an infinite plane from one display triangle.
- The 2026-09-26 all-dialog layout audit reports vertical movement of
  `mirrorReferences` and `patternLinearDirections` when enlarging Linear Pattern.
  Reproduced in cs/en/de/fr/ru at 1366 and 1920 pixel window widths (10 of 300
  dialog combinations). This is separate from the Feature/Sweep changes.

- Keep performance work as a continuing project priority and test routinely on
  real Parts with dense sketches, long feature histories and many successive
  fillets; small demonstration models are not sufficient performance tests.
- Add repeatable timing/regression benchmarks for document open/save, history
  evaluation, Fillet creation/editing, Sketcher entry/finish, dimension editing,
  topology naming, triangulation, silhouette generation and GPU scene upload.
- Use the designer's natural inspection time as an explicit background-work
  opportunity. While the user is rotating, zooming or studying an unchanged
  model, low-priority workers may prepare likely next-use data such as history-
  boundary caches, viewer meshes from already calculated/persisted geometry,
  spatial picking indexes and Sketcher inference candidates. Interactive input
  always has priority; background work must be bounded, preemptible and
  coalesced to the newest request rather than building a stale queue. Workers
  consume immutable document/view snapshots and publish an atomic result only
  when its document revision and interaction context are still current.
  Speculation must never mutate the live document, commit commands, or invoke
  hidden OCCT body calculations: OCCT remains limited to an explicitly
  requested OK/regeneration, which may itself continue in a background worker
  and atomically replace the last valid displayed result when complete. Add
  scheduling telemetry and tests proving that navigation/input latency wins
  over background throughput and stale results are discarded.
- Eliminate no-op and duplicate regeneration when closing Properties for every
  editable history container. The primitive-solid path is the first confirmed
  case: pressing OK without changing a Box, Sphere, Cylinder, Cone, Pyramid or
  Wedge writes the identical definition and runs `regenerate_model()` from
  `_update_solid_object()`, then the shared dialog-finished handler runs a
  second full regeneration. Give all container editors one shared comparison
  of their persisted geometry-affecting definition before and after the
  dialog. Unchanged OK must only leave rollback/edit presentation and restore
  the cached full-history view; changed OK must commit atomically and perform
  exactly one regeneration, invalidating from the earliest affected history
  boundary rather than the whole Part where possible. Benchmark this on long
  real Parts and add parameterized tests for every container type asserting
  zero body calculations for unchanged OK and one for changed OK.
- Extend the implemented per-history-step OCCT Shape cache so invalidation is
  explicit and starts only at the earliest changed feature. Preserve cached
  prefix shapes and topology registries across late-feature edits and Undo/Redo.
- Continue using the persisted, signature-validated compressed BREP result for
  fast `.prtz` cold starts, including Parts containing embedded STEP data.
  Interactive File/Open now decodes the document and prepares a coarse large-
  STEP display mesh in a worker; add compatibility/version diagnostics and
  measure cache size, decoding time and fallback regeneration on larger
  documents.
- Carry Shape and `TopologyRegistry` together through one central linear
  evaluator instead of allowing feature commands to reconstruct topology or
  replay earlier history independently.
- Continue replacing whole-result topology references with original-solid
  identities and Boolean intersection-curve provenance. Fillet now prefers an
  original history-solid edge and uses result topology only for a newly picked
  genuine Boolean intersection; apply the same rule to Chamfer and remaining
  persistent clients.
- After the basic C++ history and selection model is stable, add invisible
  Fillet/Chamfer algorithm dispatch. Persist edge provenance explicitly as
  `ORIGINAL_ENTITY_EDGE` or `OPERATIONAL_BODY_EDGE`; never infer it from
  coincident coordinates during regeneration. Known original Box/Cylinder/
  Cone/Extrusion edges may use specialized analytic chamfer or fillet builders
  when all earlier history leaves the required local neighborhood intact.
  Genuine Boolean/intersection edges use the general OCCT operation at the
  exact input boundary. Later history cannot affect this decision. Benchmark
  both paths and enable a specialization only after deterministic geometry,
  validity-range and performance comparisons pass. The user keeps one command
  and one Properties contract regardless of the selected internal algorithm.
- Add central `entity_id -> entity`, `entity_id -> parent` and
  `entity_id -> owning history object` indexes. Replace repeated recursive tree
  searches in visibility, selection, attachment and scene-building hot paths.
- Keep Sketcher editing isolated at its history boundary. Later features must
  be absent both visually and computationally until the sketch is finished;
  displaying a 2D sketch or its dimensions must never evaluate downstream
  solids or fillets.
- Keep the live `SketchModel` in memory during an editing session and batch
  compound operations such as Rectangle. Avoid repeated full JSON
  parse/serialize cycles and solve constraints only when the operation requires
  it.
- Make tree selection, dimension interaction and ordinary highlight changes
  update only UI state. They must not rebuild geometry, recombine the viewer
  scene, re-upload unchanged buffers or recalculate silhouettes.
- Retain bounded caches for triangulation, silhouettes and GPU-ready buffers at
  multiple recently used history boundaries so entering and leaving Sketcher
  remains fast on fully filleted bodies.
- Profile large nested Assemblies separately and then implement true GPU mesh
  instancing: upload one immutable vertex/index buffer for each distinct source
  Part mesh and draw every occurrence with its own instance transform, stable
  occurrence ID and appearance data. Moving or mating a component should update
  only its transform/instance record, without copying or CPU-transforming the
  complete mesh and without re-uploading unchanged geometry.
- Preserve the viewer-selection contract while introducing GPU instancing.
  Hover, LMB confirmation and RMB cycling must still consume one ordered
  candidate list and identify the exact nested occurrence. Add an occurrence-ID
  picking buffer or a measured spatial-index alternative; do not fall back to
  whole-body highlighting or result-body topology references.
- Treat GPU instancing as a measured Assembly scaling project, not as a CAD
  kernel replacement. Keep OCCT B-Rep calculations on CPU, benchmark CPU mesh
  preparation, buffer upload, frame time, picking latency and component-move
  latency before and after the change, and retain the new path only where those
  measurements demonstrate a material improvement.
- Show optional developer performance diagnostics with per-stage timings and
  entity/face/edge/triangle counts, making new quadratic or repeated work
  visible before it becomes a user-facing regression.

## Technical Drawing production follow-ups

The Drawing foundation exists: sheets/views, supported projected and section/detail
workflows, Family variants, associative dimensions/chains, Show/Erase, movable
annotations, frames/title blocks, BOM regions/balloons, symbols and PDF/DXF/image
output. See [Drawings](DRAWINGS.md), [the manual](USER_MANUAL.md) and
[command coverage](DRAWING_COMMANDS.md).

Continue real Part/Assembly cases: curved silhouettes, coincident boundaries,
hidden geometry, reference repair after model edits, additional annotation/table
workflows and production flat patterns. Preserve actual source/occurrence identity
and paper-space millimetres. Interactive views use current calculated geometry;
precise output preparation belongs to explicit printing/vector export, not tab
activation, view placement, panning or zooming.

## DXF Interchange

Implemented: supported DXF import into ordinary native Sketch geometry from
Sketcher, Part and Assembly, and Sketch/Drawing DXF export. See
[import scope](IGES_DXF_IMPORT.md), [export commands](EXPORT_COMMANDS.md) and
[Sketch commands](SKETCH_COMMANDS.md).
Remaining work is representative unit/layer/entity round-trip verification and
additional explicitly scoped entities, preserving exact native geometry and
reference identity. Do not reimplement the existing parser or import workflow.

## Unified Import and Export

The shared import/export framework, supported STEP/IGES/DXF paths, nested
Assembly exchange and PNG/JPEG View export are implemented. STEP Part import
creates Bodies and Assembly import preserves ordinary native sources and repeated
occurrences. See [STEP](STEP_IMPORT_EXPORT.md), [import](IMPORT_COMMANDS.md),
[export](EXPORT_COMMANDS.md) and [View output](VIEW_EXPORT_COMMAND.md).
Continue representative unit, product hierarchy, appearance and format-option
verification. Keep imported reference data in the native documents and use the
calculated current state for export; do not reread external files on passive display.

## Display Properties

Object/occurrence styles, opacity and semantic per-face appearance overrides
are implemented. See [appearance commands](APPEARANCE_COMMANDS.md) and
[model interaction](MODELING_INTERACTION.md). Preserve source-Part inheritance,
independent Assembly occurrence overrides, native identities and restoration of
temporary editing presentation. Continue split/merge/reference-recovery and
transparent-object selection cases. Neutral graphics follow live themes; explicit
hover/confirmation colors follow the current [UI convention](UI_VISUAL_CONVENTIONS.md),
not the obsolete orange/cyan proposal.
The proposed temporary **Make others transparent** editing shortcut and restoration
policy remain a separate follow-up; existing opacity support alone does not
establish that shortcut's implementation.

## Model Sections

Implemented in Part/Assembly with persisted definitions, exact component scope,
shared placement and Drawing integration. See [Sections](SECTIONS.md) and
[section commands](SECTION_COMMANDS.md). Extend production cases and diagnostics
without changing source solids or inventing permanent references from transient
cut contours. Passive display and Properties consume calculated data; they must
not invoke hidden OCCT work.

## Surface Modeling

Implemented: explicit Surface results in profile Features and General Surface,
Fill, Sewing, Surface Intersection, Surface Trim and Surfaces from solid. These
are native Modeling commands; a separate future workspace is not required to
use them. See [surface tools](NETWORK_SURFACE.md) and
[profile surface results](SURFACE_PROFILES.md).

Remaining work includes additional supported joining/closing, trimming and
reference-recovery cases, plus the separately agreed Feature projection and
ordered limiting-surface extension. Do not imply an arbitrary open surface is
a solid or promise that every proposed surface can cut one. Preserve explicit
material sides, source identity, preview/OK/Cancel ownership and the protected
native contracts. Blender-style direct meshes are a separate planned workflow.

## 3D Curve Container

Implemented: ordered native Point definitions, shared placement/reference entry,
point insertion/reordering and Polyline/Spline paths used by Sweep. See
[3D Curve and Sweep](3D_CURVE_AND_SWEEP.md). Continue real-project reference and
editing verification rather than schedule the foundation again.

## Sweep, Helix and Springs

2D/3D Sweep, profile stations/Loft, Thin modes and Helical Sweep are implemented;
see [3D](3D_CURVE_AND_SWEEP.md), [2D](SWEEP_2D.md) and [helical](HELICAL_SWEEP.md).
Continue supported orientation/corner/reference cases and measured performance.
A dedicated spring command with engineering end conditions remains a design
task. Existing Helical Sweep is usable today; it is not the same promise as that
future semantic spring workflow. Agree its minimal parameters with the user.

## Basic Piping

- Build the first Pipe feature as a thin semantic layer over a 3D Curve and
  Sweep: route reference, outside diameter, wall thickness or inside diameter,
  material and stable Start/End connector frames.
- Support one continuous bent tube first. A polyline route may generate tangent
  bends from one minimum bend radius while preserving its original route-point
  identities and deterministic straight/arc segment identities.
- Keep a full piping-system application (catalog elbows, tees, reducers,
  flanges, standards, connectivity and BOM) as a later layer. Do not burden the
  first useful bent-tube feature with that complete system.
- Route points and bend parameters obey mandatory own-value fallback when a
  driving reference disappears.

## Holes and Cosmetic Threads

- Hole is now a semantic history container with blind, Up To and Through All
  depth, optional countersinks/drill point and an independent cosmetic thread.
- Standalone Thread now stores standard, designation, nominal and profile
  diameters, pitch, side, handedness and threaded length. Embedded catalogs
  cover metric ISO, Whitworth BSW and cylindrical G/BSPP threads; tapered pipe
  threads and tolerance classes remain future work.
- Thread deliberately has no helical BREP geometry or Boolean contribution.
  Explicit calculation creates separate grey OCCT sheet geometry for its root
  and nominal cylinders plus finite-length runout cones. The sheets do not
  change solid volume, but every later subtractive history operation trims
  them with the same OCCT tool. Thread shares Extrusion's one-side, two-side
  and symmetric extents and independent Length, Up To and Through All ends;
  it deliberately has no offset work plane.
- Thread runout respects thread side: an external thread keeps the nominal
  sheet continuous, while an internal thread keeps the root sheet continuous;
  both retain two distinct cylindrical sheets and their runout cones.
  The end cone reverses the start cone's radii, and Automatic side detection
  distinguishes an axis in material from an axis in a void.
- Drill Point is a standalone subtractive history container placed by the
  common container workflow from a hole axis and its circular planar bottom
  face. Explicit OCCT calculation derives the radius from that stable face
  and subtracts the 360-degree drill cone. Its only editable parameter is the
  included angle (118 degrees by default), also exposed as an editable angular
  View dimension.

## Container Orientation and Reference Geometry

- Exercise the shared six-DOF properties workflow for Point, Axis, Plane,
  Sketch, Protrusion and Revolve on real feature chains. Positional references,
  optional FRONT/TOP orientation mapping, RX/RY/RZ corrections and work-plane
  offset are now separate concerns.
- Add regression coverage for an offset profile with base RY at the gimbal-lock
  boundary plus local RX correction. The Sketch plane, preview wire, calculated
  body, yellow dimension and purple manipulator must share one transform.
- Preserve Protrusion/Revolve direction-defined Start and End identities across
  one-sided, two-sided and symmetric mode changes. Flip must exchange the two
  side values only where they are independently defined.
- Continue extending semantic topology references from the now-supported
  container planar-face/orientation constraints to edge, vertex and curved-face
  container constraints.
- Keep positional references independent from explicit rotational references
  and keep the work-plane offset independent from both. Report remaining
  X/Y/Z/RX/RY/RZ degrees of freedom clearly.
- Enforce the mandatory own-value fallback contract from
  `STABLE_TOPOLOGY_NAMING.md` for every reference-driven parameter. A resolved
  reference refreshes its last valid fallback; a missing, ambiguous or
  incompatible reference retains usable geometry from that value while
  remaining visibly broken and repairable. Never replace it with zero or a
  current runtime topology index.
- Continue testing the implemented two-row mapping to Front, Back, Top, Bottom,
  Left and Right, including automatic prevention of parallel mappings and the
  valid empty-mapping fallback to the container's local frame.
- Verify generated locked axes for circular protrusions, cylinders, cones and
  spheres after edit, regeneration, save and reload.

## Feature Extent Targets

- **Implemented:** planar face/datum-plane Up-to and subtractive Through-all.
  An inclined supporting plane produces an inclined terminal face; calculation
  clips with a clean infinite plane derived from persisted origin/normal,
  never with the bounded trimming wires of the selected face.
- **Implemented:** cylindrical Up-to targets persist the selected analytic
  cylinder, validate sampled profile rays against its first positive
  intersection, reject mixed/missing/ambiguous hits, and at OK/regeneration
  clip the extrusion with the corresponding cylindrical volume.
- **Implemented:** spherical and conical targets use the same persisted
  analytic/ray-validation contract and OCCT volume clipping as cylinders.
- **Implemented:** a general NURBS/B-spline target persists its selected-face
  triangles for the OCCT-free wire preview. OK/regeneration resolves the stable
  face reference, intersects the exact OCCT face and clips with that surface;
  every sampled profile ray must hit the selected face in one common direction.
- **Implemented:** plane, sphere, cylinder and cone intersections are evaluated
  by one shared analytic solver used by both the cyan wire preview and body
  calculation. Invalid targets distinguish unresolved reference, parallel
  direction, missing profile coverage and a profile crossing the target.
- For cylinders and spheres choose the first positive intersection in the
  requested direction and persist the selected inside/outside side. Reject the
  operation if any required profile ray misses the target or is ambiguous.
- General curved Up-to does not use one scalar preview endpoint: every profile
  ray owns its terminal point on the target surface. The maximum distance is
  used only as safe construction overrun before exact clipping.
- A target point defines the plane through that point perpendicular to the
  extrusion direction. Do not offer an axis as a target until its geometric
  meaning is unambiguous for the selected direction.
- Start and End each persist the complete independent condition, stable target
  payload, offset, direction and last valid calculated length. Flip exchanges
  those side definitions where appropriate; symmetric mode keeps stable
  Start/End identities.
- Planned extension, clarified on 2026-10-20: multiple Up To targets form an
  ordered consecutive surface chain. Adjacent surfaces intersect to define
  the joined limiting shape; this is not a nearest-hit priority/fallback list.
  Keep explicit Up/Down reordering and signed correction.
  Native persisted changes require the agreed approval before implementation.
- Use the shared reference-entry list, stable item identity and explicit Up/Down
  reordering for the planned chain. Validate adjacent intersections and report
  gaps; retained patch sides and extension beyond selected face bounds still
  need design. Reuse current placement without changing its protected contract.
- Persist the branch/side decision and one frozen analytic fallback descriptor
  for every successfully resolved target: plane origin/normal, cylinder
  axis/radius/range, sphere centre/radius or cone axis/apex/angle, expressed in
  a stable owner frame. If a reference disappears, keep the Up-to intent and
  visibly broken reference, calculate against that frozen target where it
  still produces a valid closed solid, and automatically resume only when the
  same stable reference returns. A single last-valid length is sufficient only
  for a plane perpendicular to the extrusion direction; otherwise preserve
  the last valid calculated feature body as the final fallback rather than
  flattening an inclined or curved terminal face.
- For the planned surface chain, validate the joined limiting shape and signed
  correction explicitly. Missing adjacency, ambiguity or an invalid/open result
  must reject the pending edit without replacing the last valid body. Do not
  silently substitute another target or a distant branch.


## Pattern and Mirror

Implemented: linked Part/Assembly Mirror and linear/circular Pattern, stable
occurrences, dependent/independent mirrored-Part document workflows and their
documented source/identity rules. See [Mirror and Pattern](MIRROR_AND_PATTERN.md),
[derived commands](DERIVED_COPY_COMMANDS.md) and [copies](DOCUMENT_COPY.md).

Possible extensions remain curve/table/reference-driven patterns, multiple
sources and scoped feature patterns beyond current capabilities. Verify survivor
identities, source/reference loss and repeated/nested occurrences before extending
them. Creating a mirrored Part is not a missing foundation anymore.

## Undo and Redo

Document Undo/Redo and shared atomic transactions are implemented. Every changed
command must verify its own transient preview, OK/Cancel, Undo/Redo and native
reopening now. The deferred task is a comprehensive cross-command audit after
the agreed modeling features, not implementation of Undo/Redo from scratch.
See [Part transactions](PART_SESSION_TRANSACTIONS.md) and the current rules.

## Assembly Stabilization

- Exercise the new `.asmz` workflow on real two- and three-component examples.
- Profile large real Assemblies before the next performance changes. During
  component dragging, update only the moved component and its dependent mate
  chain instead of rebuilding the complete scene, tree or highlight state.
  Measure Assembly opening, tab switching and memory use for deeply nested
  Assemblies and keep representative regression timings.
- Add regression timings for insertion of cached and uncached imported Parts.
  Preserve the current single scene rebuild, lazy topology enumeration,
  per-source document cache and compound-based component result.
- Add integration coverage for dependent-scene invalidation when an open Part
  changes, including simultaneous Part/Assembly tabs and multiple windows.
- Continue refining the three paired mate rows now that plane/offset,
  concentric-axis and angular mates, nearest-pose solving and editable in-view
  dimensions are available:
  - clear feedback for incompatible, redundant and unresolved mate sets;
  - explicit reporting of the remaining X/Y/Z/RX/RY/RZ freedoms;
  - robust recovery after a referenced entity disappears;
  - wider coverage of axis-to-plane angular combinations and non-origin datum
    references.
- Original-solid Assembly picks and persisted mate restoration are now lazy.
  Keep whole-result topology only for genuine Boolean intersection references;
  confirmed faces now retain a transient cyan viewport overlay without storing
  a result-body runtime index. Extend equivalent direct overlays to every
  supported persisted edge/axis reference and after document reload.
- Rigid parent motion now propagates through dependent mate chains. Purple
  origin-handle translation is projected directly into the free subspace of
  plane and axis mates before coordinates change; continuous pointer motion
  does not call the solver, which runs only after release for final validation.
  Extend dependency handling to a central assembly solver for multi-parent
  constraint graphs, cycle diagnostics and deterministic recomputation after
  file reload.
- Continue the central stable-topology implementation described in
  `doc/STABLE_TOPOLOGY_NAMING.md`. Box/Wedge faces and Extrusion faces, edges
  and vertices now have semantic identities; Revolve faces, edges and vertices
  have equivalent provenance-based identities. Supported external Sketch
  references survive parent edits, save/reload and automatic descendant
  regeneration. Existing supported ancestry now propagates through the initial
  additive/subtractive Part subset with explicit split/merge ambiguity. New
  section edges and vertices have ZIMA-owned adjacency provenance, including
  circular Extrusion and repeated full center-arc Revolve cuts. Expand this
  coverage to general Boolean results next. A spline can close by reusing its
  first point; closed-spline Extrusion/full Revolve and an Extrusion with a
  mixed segment/arc outer loop plus a closed-spline hole now keep semantic
  topology references. Three-level Extrusion nesting also keeps the island
  provenance and gives repeated cap roles deterministic fragments. Partial
  Revolve now has equivalent outer/hole/island coverage for faces, edges and
  vertices. Repeated non-overlapping cuts through a two-solid feature preserve
  the complete supported face/edge/vertex reference sets across edits and
  save/reload. An additive bridge joining both source solids into one body also
  preserves source and bridge ancestry through a following cut. Crossing
  cylindrical and toroidal cuts retain curved-to-curved intersection ancestry
  through simultaneous radius edits and save/reload. A following closed-spline
  cut also retains the cylindrical ancestry through control-point edits.
  Assembly face selections now use canonical instance-plus-face payloads;
  planar mates on Boolean faces survive source regeneration and report a
  suppressed feature as `MISSING`. Missing and ambiguous mate rows remain
  visible but inactive in the solver, and a temporary Boolean split recovers
  automatically when removed. Extend those identities into the remaining
  Assembly dependency workflows.
  Canonical `AssemblyEdgeRef` now drives circular-edge selection, concentric
  mate frames and highlighting; diameter regeneration and suppression/recovery
  now have integration coverage equivalent to planar mates.
  Missing
  or ambiguous topology must remain unresolved instead of falling back to a
  current numerical index.
- Add grounding/un-grounding of components and visualize remaining component
  degrees of freedom independently of the mate editor.
- Keep instance placement in the assembly document. In-context feature edits
  intentionally modify the referenced `.prtz`, while assembly cuts never do.
- Exercise in-context component activation, cross-component sketch references,
  assembly cuts and per-instance colors on larger real projects.
- Additive features that do not touch the existing Part body and operations
  that fail to produce a solid now preserve the last valid body and report a
  red feature error instead of exposing a disconnected compound.

## Basic Sketcher Completion

The native Sketcher foundation is implemented: curves/profiles, polygons,
ellipses, text editing and solver anchors, symbols/Explode, supported DXF,
live constraint dragging, dimensions, Trim and offsets. See [Sketcher](SKETCHER.md),
[symbol guide](SYMBOLS_USER_GUIDE.md) and [current corrections](PROJECT_STATUS.md).

Continue repeated-edit/drag matrices, branch stability, finite support equations,
external read-only identity, conflict diagnostics and topology-changing reference
recovery. Do not infer universal convergence from an initial successful solve.
New dialogs reuse the internal Properties and shared confirmation contract:
OK/Cancel, short middle-click reference-entry completion, and middle-button
double-click confirmation. Do not restore obsolete Apply or short-click-OK rules.
Light/dark and all five language checks accompany applicable UI changes.

## Native viewer boundary

- The Python viewer removal is complete and its old checklist is retired. Follow
  [native architecture](CXX_ARCHITECTURE.md) and [viewer selection](VIEWER_SELECTION.md).
- The hidden legacy viewer, unreachable AIS rebuild path and OCCT presentation
  imports have been removed.
- Verify native selection and constraint previews across all reference types.
- Continue stabilizing the implemented native in-view dimension overlays; the
  unreachable AIS implementation was removed during cleanup.
- Protrusion length and Revolve angle overlays now start on their actual offset
  profile plane; extend the same placement rule to future profile features.
- The axis-editor pointer path can no longer enter OCCT presentation or
  highlighting code.

## In-View Dimension and Parameter Representation

- Refine the `Edit` mode so 3D dimensions remain clear and readable for every
  viewing direction and model size.
- Review the placement of dimension lines, plane-like end rectangles and
  editable value fields.
- Prevent projected value fields from overlapping each other or obscuring
  important model geometry.
- Define consistent spacing and automatic offsets for solid dimensions and
  container-position parameters.
- Refine the representation of zero values so they remain visible and
  editable without suggesting a false non-zero distance.
- Finalize the color system:
  - blue for sketches shown in the model;
  - yellow for active parametric/system geometry and selection;
  - cyan for dimensions;
  - orange for the active dimension;
  - red for invalid or conflicting dimensions.
- Test legibility during rotate, pan and zoom, including dark and light
  backgrounds.
- Design angular arc dimensions for container rotation (`RX`, `RY`, `RZ`);
  numeric RX/RY/RZ editing and six-DOF accounting are already implemented.
- Later extend the same visual language from solids and container placement to
  sketches, points, axes and planes.

## Native Viewer Pan and Zoom

- Tune pan and zoom only in `ZimaOpenGLViewer`; the legacy V3d diagnostics are
  obsolete because V3d is no longer the active viewport.
- Test mouse wheels and touchpads at multiple device-pixel ratios.
- Keep cursor anchoring and overlay updates synchronized with the native camera.

## Custom Containers

- Continue with custom entities in the part tree.
- Each Container may contain exactly one user entity (Point, Axis, Sketch or Solid)
  in addition to its mandatory system Origin. Creation commands and `.prtz`
  validation enforce this invariant.
- Editable container position is implemented:
  - X
  - Y
  - Z
- Editable container rotation is implemented:
  - RX
  - RY
  - RZ
- RX/RY/RZ support is implemented in container properties, `.prtz` storage,
  solid rebuild and selected-container local coordinate-system display.
- Container rotation rotates its local coordinate system:
  - point
  - axes
  - planes
- Child solids and sketches now follow the complete parent container transform.
- Continue extending local-coordinate evaluation to future geometry types.
- Position and orientation use the shared reference solver. Up to three
  positional references solve the origin; two bounded orientation slots define
  FRONT/BACK and TOP/BOTTOM/LEFT/RIGHT. RX/RY/RZ are corrections on that base
  frame. Plane, Sketch, Protrusion and Revolve own a separate work-plane offset
  that never changes the container origin.
- Extend attachment references beyond Box faces and add dependency-cycle
  detection before supporting general feature chains.
- Wedge is available as a parameterized solid (`length`, `width`, `height`,
  `top_offset`) for testing sloped planar faces. Its semantic face roles include
  `slope`, and plane-on-face attachment supports all six Wedge faces.
