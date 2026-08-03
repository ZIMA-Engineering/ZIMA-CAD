# Next Work

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
- Implement ISO technical fonts and 5 mm paper-space text conventions.
- Finish configurable sheet frames, zones, title blocks/stamps and
  parameter-driven format fields.
- Add sections and details after selection and annotation ownership are stable.
- Keep paper-space geometry in millimetres with the bottom-right sheet origin;
  A4 remains portrait and A3 through A0 remain landscape.
- Keep runtime drawing geometry derived from renderer-owned model topology;
  persisted legacy 2D projection caches are intentionally unsupported.

## Container Orientation and Reference Geometry

- Exercise the new six-DOF properties workflow for Point, Axis, Plane, Sketch,
  Protrusion and Revolve on real feature chains.
- Continue extending semantic topology references from the now-supported
  container planar-face/orientation constraints to edge, vertex and curved-face
  container constraints.
- Keep positional references independent from explicit rotational references;
  report the remaining X/Y/Z/RX/RY/RZ degrees of freedom clearly.
- Stabilize the two-row mapping of container planes to Front, Back, Top,
  Bottom, Left and Right, including automatic prevention of parallel mappings.
- Verify generated locked axes for circular protrusions, cylinders, cones and
  spheres after edit, regeneration, save and reload.

## Assembly Stabilization

- Exercise the new `.asmz` workflow on real two- and three-component examples.
- Continue refining the three paired mate rows now that plane/offset,
  concentric-axis and angular mates, nearest-pose solving and editable in-view
  dimensions are available:
  - clear feedback for incompatible, redundant and unresolved mate sets;
  - explicit reporting of the remaining X/Y/Z/RX/RY/RZ freedoms;
  - robust recovery after a referenced entity disappears;
  - wider coverage of axis-to-plane angular combinations and non-origin datum
    references.
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
  mate frames and highlighting; add regeneration/recovery integration coverage
  equivalent to planar mates next.
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

- Add intelligent entity input: inference, snapping, continuation and clear
  previews while creating connected geometry.
- Add polygon input and ellipse geometry.
- Continue stabilization of centre arcs and spline editing.
- Improve the solver's numerical stability, branch preservation, diagnostics
  and recovery for redundant, conflicting and under-constrained systems.
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
- Add editable container position:
  - X
  - Y
  - Z
- Add editable container rotation:
  - RX
  - RY
  - RZ
- Initial RX/RY/RZ support is implemented in container properties, `.prtz` storage,
  solid rebuild and selected-container local coordinate-system display.
- Container rotation must rotate its local coordinate system:
  - point
  - axes
  - planes
- Child solids and sketches now follow the complete parent container transform.
- Continue extending local-coordinate evaluation to future geometry types.
- Initial plane-on-face attachment is implemented for Container XY/YZ/XZ planes
  and semantic Box faces (`x_min` through `z_max`). Attachments project the
  global Origin onto the target plane, use two perpendicular global reference
  axes with a 45-degree switch, persist in `.prtz`, and preserve the last valid
  transform when the target disappears.
- Extend attachment references beyond Box faces and add dependency-cycle
  detection before supporting general feature chains.
- Wedge is available as a parameterized solid (`length`, `width`, `height`,
  `top_offset`) for testing sloped planar faces. Its semantic face roles include
  `slope`, and plane-on-face attachment supports all six Wedge faces.
