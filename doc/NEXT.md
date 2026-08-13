# Next Work

## Performance and Scaling

- Keep performance work as a continuing project priority and test routinely on
  real Parts with dense sketches, long feature histories and many successive
  fillets; small demonstration models are not sufficient performance tests.
- Add repeatable timing/regression benchmarks for document open/save, history
  evaluation, Fillet creation/editing, Sketcher entry/finish, dimension editing,
  topology naming, triangulation, silhouette generation and GPU scene upload.
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
- Show optional developer performance diagnostics with per-stage timings and
  entity/face/edge/triangle counts, making new quadratic or repeated work
  visible before it becomes a user-facing regression.

## Current Priority: Complete the Technical Drawing Foundation

- Exercise `.drwz` creation from both `.prtz` and `.asmz` documents on real
  projects and verify source-link recovery after move and rename operations.
- Exercise standard and eight-way 45-degree projected views, first-/third-angle
  rules, parent/child movement, view captions and Delete cleanup on larger real
  parts and assemblies.
- Stabilize the shared model/drawing edge classification, especially curved
  silhouettes, exact tangent views, coincident geometry and occlusion between
  multiple bodies.
- Finish hidden-line classification so every curved boundary is consistently
  visible, hidden or suppressed in all five display modes.
- Complete hover and selection of actual model points, edges and faces through
  a drawing view before building further annotation commands on top of it.
- Extend the first associative yellow linear drawing dimension to the complete
  ISO dimension set, including angular, radial, diameter, ordinate and position
  dimensions, tolerances, leaders, datums and technical symbols.
- Add movable/selectable drawing annotations with consistent hover, selection,
  Delete and property editing.
- Extend the implemented ISO technical font and exact millimetre title-block
  text sizing to the remaining paper-space annotations.
- Extend the implemented editable sheet-frame/title-block templates and
  parameter-driven fields with production zones and table workflows.
- Add sections and details after selection and annotation ownership are stable.
- Keep paper-space geometry in millimetres with the bottom-right sheet origin;
  A4 remains portrait and A3 through A0 remain landscape.
- Keep runtime drawing geometry derived from renderer-owned model topology;
  persisted legacy 2D projection caches are intentionally unsupported.

## DXF Interchange

- Add DXF import directly into the active Sketch. Preserve real dimensions,
  provide an explicit unit choice when the file is ambiguous and map supported
  layers and entities without creating hidden placement corrections.
- Add DXF import commands to the Part and Assembly applications. These commands
  must create or populate an explicitly placed Sketch; DXF is two-dimensional
  reference/profile geometry, not an imported solid body.
- Add DXF export from Sketcher for supported profile and construction geometry,
  with real model-space dimensions and documented handling of layers, text,
  splines and unsupported constraints.
- Keep DXF file parsing separate from Sketch interaction. Import must produce
  normal persisted ZIMA Sketch entities with stable IDs so they can be edited,
  constrained, regenerated and saved like manually created geometry.

## Surface Modeling

- Keep the ordinary Part application solid-only. A closed profile creates an
  ordinary solid and an open profile creates a thin solid; neither silently
  creates a standalone surface body.
- Stabilize the current thin-solid foundation first: wall-side controls,
  Inside/Outside and Start/End identities, self-intersection diagnostics and
  reference recovery when switching between thin and ordinary solid results.
- Add a solid-cut operation driven by a selected surface or surface body. The
  operation must define the retained side explicitly and store stable
  references to the cutting surface.
- Design standalone surface creation as part of an explicit future
  surface-modeling workspace, not as a third implicit Add/Subtract state in
  ordinary Protrusion or Revolve.
- Add **Flip** wherever a Protrusion/Revolve surface participates in trimming
  a solid. Flip reverses the persisted cutting half-space, while an in-view
  arrow previews the currently removed/retained side. Apply recalculates the
  preview and OK commits it through the ordinary staged feature workflow.
- Keep `output_kind` (solid/surface), Boolean combination (add/subtract/none)
  and cutting-side `flip` as separate persisted concepts. Viewer arrows and
  Properties consume these values without reconstructing OCCT topology.
- Design the first surface-modeling application and history entities: surface
  creation, trim, extend, join/sew and surface-body inspection.
- Keep surface bodies distinct from solids in the document model, tree,
  selection filters and operation validation.
- Add a deliberate conversion from a valid closed set of surfaces to a solid;
  do not silently interpret an open surface as a solid body.
- Persist all selection and topology data required for later editing during
  explicit Apply/OK/regeneration. Surface hover, tree display and Properties
  opening must not trigger hidden OCCT reconstruction.

## 3D Curve Container

- Implement a parent 3D Curve container containing an ordered list of normal
  Point containers, not a new 3D Sketcher and not an embedded private point
  representation.
- Every child Point retains the existing container contract: mandatory Origin,
  one Point entity, stable ID, X/Y/Z fallback and ordinary persisted placement
  references.
- The order of Point containers in the tree is the path order. **Insert here**
  determines the insertion position; delete and reorder operations update the
  path deterministically.
- The first implementation draws a 3D polyline between the evaluated Point
  origins. Keep the representation ready for a later Polyline/Spline mode and
  use the result as the first Sweep path input.
- Viewer display, hover and selection consume the persisted ordered points.
  OCCT may create the calculated edge/wire only at Apply, OK or regeneration.

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
- Continue testing the implemented two-row mapping to Front, Back, Top, Bottom,
  Left and Right, including automatic prevention of parallel mappings and the
  valid empty-mapping fallback to the container's local frame.
- Verify generated locked axes for circular protrusions, cylinders, cones and
  spheres after edit, regeneration, save and reload.

## Assembly Stabilization

- Exercise the new `.asmz` workflow on real two- and three-component examples.
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
- Rigid parent motion now propagates through dependent mate chains. Extend this
  to a central assembly dependency solver for multi-parent constraint graphs,
  cycle diagnostics and deterministic recomputation after file reload.
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

- Treat the current Sketcher feature set as the first functionally complete
  version. Keep further work focused on editing polish, solver quality,
  performance and regression fixes instead of expanding the basic tool set.
- Complete editing of Sketch text after creation: reopen its text properties,
  change content, font/height, alignment and placement, regenerate the contour
  geometry without losing its stable Sketch entity ownership, and support
  Undo/Redo plus save/reload.
- Add intelligent entity input: inference, snapping, continuation and clear
  previews while creating connected geometry.
- Implement one central Sketch cancel action shared by the `Esc` key and a
  toolbar button. The first invocation cancels the in-progress point/entity or
  current candidate, the next returns to Select, and Select-mode Escape clears
  hover/selection. It must not close a Properties window or discard its staged
  values.
- Implement one matching Sketch confirm action shared by a short middle click
  and view-focused `Enter`. An editor-focused Enter commits only that text or
  numeric value and must never also submit geometry or close Properties.
- Extend right-button candidate cycling from overlapping geometry to the valid
  inference/constraint variants of the entity being entered. The orange
  preview must identify the exact variant that the following confirmation will
  persist; invalid, redundant and conflicting variants never enter the cycle.
- Audit the remaining native parameter/edit dialogs and migrate them to the
  shared internal `Qt.WindowType.SubWindow` properties presentation. Start
  with the three remaining `QInputDialog` workflows: Family Table column name,
  document rename and choosing an already open Drawing source. Native file
  choosers remain system dialogs because they select filesystem resources,
  not editable feature parameters.
- Every internal dialog belongs to exactly one `MainWindow` instance. Its
  parent, bounds, active Apply/OK/Cancel routing, middle-button confirmation
  and keyboard actions must never use application-global state or affect a
  second concurrently open ZIMA-CAD window.
- External Sketch point references stay hidden until the cursor enters their
  snap radius; keep this hover-only behavior for future dense reference types.
- Continue migrating interactive tools to the central `SelectionController`.
  Fillet already uses a declarative one-stable-edge request and switches the
  viewer between topology and ordinary object interaction centrally. Reuse the
  same request/validator path for Chamfer, Assembly mates and Sketch external
  references; do not add further per-command selection flags.
- Keep container Properties on the shared `ConstraintCapability` policy. Every
  container exposes the complete local-frame placement `X/Y/Z + RX/RY/RZ` and
  may consume only its bounded independent position/orientation references.
  Migrate remaining dialog-specific checks to this policy.
- Add polygon input and ellipse geometry.
- Add the DXF import/export workflows specified in **DXF Interchange** and test
  round trips on profiles containing segments, arcs, circles and splines.
- Continue stabilization of centre arcs and spline editing.
- Improve the solver's numerical stability, branch preservation, diagnostics
  and recovery for redundant, conflicting and under-constrained systems.
- Continue solver development as an ongoing Sketcher project: report remaining
  degrees of freedom clearly, identify redundant/conflicting relations, keep
  the selected solution branch during edits and add regression cases for dense
  real-world sketches.
- Make constraint records selectable in both tree and view. Selection must
  highlight every participating entity and dependency; Delete must remove the
  selected relation cleanly.
- Complete constraint symbols and their hover/selection/dependency display for
  every implemented relation, including symmetry and tangent relations.
- Extend the now-stable multi-level Extrusion/Revolve, repeated multi-solid cuts
  and additive joining to wider multi-body ancestry and additional curved
  profile combinations.
- Keep Trim deferred until topology-changing operations have central ID
  remapping and reference recovery.
- After the geometry and interaction rules are settled, evaluate replacing the
  current incremental solver with one unified equation/residual system.

## Critical: Finish OCCT Viewer Removal

- Follow the staged inventory and acceptance criteria in
  `doc/OCCT_VIEWER_CLEANUP.md`.
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
