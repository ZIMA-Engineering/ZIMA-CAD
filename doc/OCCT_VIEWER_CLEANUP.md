# OCCT Viewer Cleanup

## Target architecture

OpenCascade (OCCT) remains the geometry kernel. It may build, inspect and
triangulate `TopoDS_Shape` values, but it must not own a graphical context,
camera, input handling, selection highlighting or screen overlays.

`ZimaOpenGLViewer` owns the viewport and `DocumentViewerScene` translates the
document into renderer-owned meshes and topology mappings.

## Completed cleanup

The native path is already active:

- `MainWindow` inserts `ZimaOpenGLViewer` into the view layout;
- `_ensure_viewer_initialized()` deliberately does not initialize AIS/V3d;
- `rebuild_view()` calls `_rebuild_native_view()` and returns;
- native picking maps mesh topology indices back to OCCT subshapes through
  `viewer_mesh.py`.

The application UI no longer imports `OCC.Display`, AIS or V3d. The unused
`ZimaViewer`, `LegacyViewerStub`, hidden `MainWindow.viewer`, unreachable AIS
rebuild path, presentation maps, OCCT mouse detection and AIS edit overlays
have been removed.

View-normal commands now set the native camera directly. Native selection and
context menus remain connected to `ZimaOpenGLViewer`.

In-view editable dimensions are implemented in the native overlay layer and
track camera navigation and viewport resizing. Symmetric Protrusion dimensions
share one stored value regardless of which side is edited. Standard and reset
views use native camera animations.

Protrusion Properties no longer constructs or triangulates an OCCT sketch or
standalone body merely to show its cyan wire. Profile points, segments and
sampled curves are transformed directly into `ViewerMesh`; extrusion wires,
including planar, spherical, cylindrical, conical and general-surface Up-to
plus Through-all, are derived from persisted viewer and reference data.
Analytic targets use the shared ray/surface solver. A general target uses its
persisted face triangles for preview and resolves the exact face only at the
explicit calculation boundary. Existing calculated wires are read from
persisted `BodyResult` packets. OCCT is entered only at OK or explicit model
regeneration.

Thin previews likewise remain viewer-data-only. One supported open chain or
closed loop may contain segments, circular/elliptic arcs, ellipses, splines and
evaluated sketch-corner radii. Both offset boundaries, corner-longitudinal
edges and open end caps are emitted as cyan `EdgePolyline` data. Switching
wall side or symmetric mode replaces this overlay after the button event
without rebuilding the body or substituting a cached shaded mesh.

## Removal sequence

### 1. Detach the hidden legacy widget — complete

- Move any still-required no-op state from `LegacyViewerStub` to explicit
  `MainWindow` fields.
- Route context menus, sizes and coordinate conversion through
  `native_viewer`.
- Remove `MainWindow.viewer`, `LegacyViewerStub` and `ZimaViewer`.

Acceptance:

- application startup does not import `OCC.Display`;
- the visible viewport, context menu and selection-enable toggle still work.

### 2. Remove unreachable AIS rebuild code — complete

- Delete the legacy block after the return in `rebuild_view()`.
- Delete presentation-only helpers that then have no callers.
- Remove AIS presentation maps and highlight state from `MainWindow.__init__`.

Acceptance:

- model, source-history, sketch and coordinate-system geometry still render;
- wire, shaded and shaded-with-edges modes still work;
- there are no `DisplayShape`, `AIS_Shape` or
  `AIS_InteractiveContext` references in the application UI.

### 3. Finish native interaction replacements — active

- Replace remaining OCCT ray/projection helpers with native camera rays and
  renderer picking.
- Preserve candidate cycling for faces, edges, points, axes and planes.
- Preserve point/axis/plane constraint previews and confirmations.

Acceptance:

- all selection filters and overlapping-candidate cycling work without
  `MoveTo()` or OCCT selection activation;
- creating and editing a datum axis cannot enter
  `StdPrs_WFShape::Add()` or `StdSelect_BRepOwner::HilightWithColor()`.

### 4. Replace edit overlays — complete

- Render dimension lines and endpoint handles in the native overlay layer.
- Project editable value widgets with the native camera.
- Remove `_edit_dimension_ais` and all presentation cleanup code.

Acceptance:

- in-view solid and placement parameter editing remains available;
- overlays track rotate, pan, zoom and resize.

### 5. Tighten the dependency boundary

- Keep OCCT imports used by geometry and topology evaluation.
- Move renderer conversion code to `viewer_mesh.py` or `viewer_scene.py`.
- Add a static check preventing `OCC.Display`, `AIS_*` and `V3d_*` imports in
  the application/viewer UI.

## Verification

For each removal step:

1. for the frozen reference only, run
   `python3 -m compileall -q archive/python/zima_cad`;
2. start the bundled Linux build;
3. open an existing `.prtz` file and create each primitive;
4. exercise display modes, standard views, fit, rotate, pan and zoom;
5. exercise container and topology selection plus reference filters;
6. create and edit Point, Axis, Plane and Sketch constraints;
7. enter and leave in-view Edit mode repeatedly.
