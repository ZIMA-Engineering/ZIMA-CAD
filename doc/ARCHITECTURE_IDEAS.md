# ZIMA-CAD — nonbinding architecture ideas

> Working notes from an informal discussion. This is neither an approved
> specification nor a binding implementation plan.

Update 2026-09-07: the specific agreed direction for multibody Parts, branch results
and Booleans is documented separately in
[MULTIBODY_AND_BOOLEANS.md](MULTIBODY_AND_BOOLEANS.md). At that date it was still a
pre-implementation proposal. Earlier ideas below do not supersede it; consult that
document for subsequent status.

## Unified container model

- The fundamental ZIMA-CAD abstraction is a container.
- A container has a stable ID, name, parameters, properties, data and child containers.
- Sketches, geometric operations, solids, Parts, Assemblies and Drawings can share
  container mechanisms, differing in data content and permitted operations.
- Containers can nest.
- Result geometry can combine and subtract container outputs.
- Shared mechanisms could later handle copying, history, versioning, Properties
  and references.

## Container coordinate system

Each spatial container can have its own Origin, X/Y/Z axes, XY/YZ/XZ planes and
transform relative to its parent. Child geometry is evaluated in local coordinates.

### 3D Curve container

A separate spatial Sketcher is not proposed. The parent **3D Curve** contains
ordinary Point containers. Each retains its own Origin, X/Y/Z position, stable ID
and standard placement references. Tree order is also curve-point order;
**Insert here** determines where the next point is inserted.

The first version connects evaluated point Origins with a spatial polyline.
Later spline interpolation may be another mode of the same container, not an
incompatible second point representation. Persisted points and order are the
source of truth for viewer and subsequent Sweep. OCCT creates edges/wires from
them only during explicit calculation.

## Interpreting OpenCascade faces

OpenCascade distinguishes:

- `TopoDS_Face`: a current bounded topological face;
- `Geom_Surface`: its underlying mathematical surface;
- `Wire` and `Edge`: face boundaries;
- `TopLoc_Location`: placement;
- `TopAbs_Orientation`: orientation.

An OpenCascade face is not automatically a stable persistent ZIMA-CAD container.
Parameter changes and Booleans can create new topology and different
`TopoDS_Face` instances.

### Reference implications

- Never use order such as `Face1`, `Face2` or `faces[4]` as persistent identity.
- Separate temporary OCC topology from persistent ZIMA-CAD references.
- References should identify the source container, source operation and face meaning.
- Semantic examples: `StartFace`, `EndFace`, `LateralFace`, `GeneratedFromEdge`,
  or box roles `x_min`, `x_max`, `y_min`, `y_max`, `z_min`, `z_max`.
- When a reference face disappears during recalculation, the dependent container
  should retain its last valid transform and mark the reference invalid.

## Future face analyzer

An analytical layer for a current `TopoDS_Face` could determine surface type,
placement/orientation, UV bounds, approximate centre, normal, area and edge count.
Relevant tools include `TopExp_Explorer`, `BRepAdaptor_Surface`,
`BRepTools.UVBounds`, `BRepGProp.SurfaceProperties`, and `GeomAbs_SurfaceType`.
Analysis describes the current result only; it does not itself provide stable naming.

## Surface results and solid trimming

Protrusion and Revolve must separate result type `solid/surface`, Boolean
combination `add/subtract/none`, and cut orientation `flip`. **Add** and
**Subtract** are mutually exclusive; clicking the active button again can turn
both off, producing a standalone surface without Fuse or Cut.

Surface trimming of a solid must explicitly choose the retained/removed half-space.
Properties should expose **FLIP** and the viewer an oriented arrow. Flip reverses
the arrow and persisted side selection, not the sign of length, angle or offset.
The original proposal listed Apply, OK or regeneration as calculation triggers.
Current binding dialog rules supersede Apply: only OK and Cancel are exposed,
with explicit Regenerate remaining a separate calculation action.

## Sketch, Drawing and interchange formats

- Sketch can be a shared 2D geometry layer for modeling and Drawings.
- DXF can import/export 2D Sketch geometry.
- Drawing is a separate `.drwz` document; its initial foundation contains multiple
  sheets, paper formats, a source-model reference and projected views.
- It can later contain borders, title blocks, sections, dimensions, notes, tables
  and Sketches too.
- The goal is one general 2D editor used in several contexts, rather than unrelated
  editors.

## Topics for later decisions

- Container dependency recalculation and graph.
- Stable references between containers.
- Container history, Undo/Redo and versioning.
- General topology naming beyond simple parametric shapes.
- Exact boundaries between container, feature and result body/solid.

## Assembly prototype at the time of discussion

- Assembly uses a separate `.asmz` document but shares metadata, units, accuracy
  and user parameters with Part.
- An inserted Part is an occurrence referencing a source `.prtz` by relative path.
  Its transform belongs to the Assembly and must not modify the source Part.
- The occurrence tree shows the source Part tree. Activating a Part in Assembly
  context makes its children behave as in Part; active/passive differences mainly
  concern the View and modeling-tool availability.
- Placement uses up to three reference pairs: offset plane mates, datum/generated
  axis coaxiality, angle mates and Flip. Type choices follow geometry and remaining
  freedoms; the stable solver selects the position nearest the current transform.
- Mates appear as clickable 3D dimensions. Value mates are directly editable;
  zero/coaxial mates remain placement-state indicators.
- Central `TopologyRegistry` provides stable `FaceRef`, `EdgeRef`, `VertexRef` for
  Box/Wedge, Extrusion and Revolve. External sketches and supported history
  operations use these instead of temporary indices. Supported propagation through
  addition/subtraction preserves ancestry and distinguishes missing/ambiguous
  results; general Booleans and further operation types still required expansion
  at this stage.
- Assembly Protrusion/Revolve subtract only. They can target all or selected
  occurrences but must not modify source `.prtz` files.

## Drawing foundation at the time of discussion

- One `.drwz` contains multiple sheets, each with its own A4–A0 format.
- A4 is portrait; other supported formats are landscape.
- Sheet coordinates are real millimetres, with Origin at bottom right, positive X
  leftward and positive Y upward, preserving title-block placement on format change.
- The workspace is black without paper fill; a white rectangle marks the sheet.
- Drawing stores relative source Part/Assembly path and ID. View geometry derives
  at runtime from actual native-renderer topology; obsolete saved 2D projected-line
  caches were intentionally unsupported by this prototype.
- Views can be selected, moved and deleted. Derived views retain parent linkage
  and can be created in eight 45-degree directions under European/American projection.
- Each view has its own display mode. Line modes share edge/silhouette classification
  with the 3D model; shaded modes use model colours, smoothed normals and a software
  Z-buffer.
- A view can show an independently movable name/scale label.
- The first associative linear Drawing dimension was implemented; full ISO dimensions,
  tolerances, item numbers, labels and technical symbols remained to be completed.
- `.frmz` and `.tblz` are editable as Sketch documents. The `.tblz` renderer reads
  persisted `[Sketch]` directly, without inferring insertion from geometry bounds
  or applying hidden offsets. Sketch `(0, 0)` equals Drawing `(0, 0)`.
- Title-block text preserves anchor, both alignments, rotation, flipping, font,
  colour and height. CAD height means font cap height, not a particular string's
  ink bounds.
- Parametric fields are semantic text entities with tokens; the renderer does not
  draw a hard-coded table over the Sketch.
- Title-block BOM Repeat Region, including Item Number and Quantity, worked;
  sections, details and further production Drawing features remained.

## Parameters and relations

- A user parameter is always a materialized value usable without knowing its origin
  in features, Assemblies, family tables, title blocks and Drawings.
- Stable default keys are English identifiers. Localized parameter names map both
  key and value language, for example literal UI mappings `Název -> name + cs`
  and `Name -> name + en`; a shared value replaces a language-specific branch.
- Relations belong only to Part/Assembly model documents. They store
  `target + expression`, writing evaluated results to normal
  `user_parameters[target]` and the parameter's language-shared value.
- In the Python prototype, expressions were parsed through Python AST and evaluated
  by a custom allow-list interpreter. Model files could not import modules, access
  files or call application Python. This describes the prototype, not a requirement
  to reintroduce Python into the native implementation.
- Initial system context exposed `model.volume`, `model.area`, `model.mass` and
  `material.density`. Volume/area came from the result OCCT shape; density was
  normalized to `kg/mm^3` and mass stored in kilograms.
- Drawing owns no relations. Its Parameters dialog edits the source Part/Assembly
  and refreshes Drawing geometry and title block after committing.
- New Parts clone configured `START_PART.prtz`, assigning a new document ID and
  target name. Default relations thus remain in normal model files, not hard-coded
  application logic.
