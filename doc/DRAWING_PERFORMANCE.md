# Drawing projection performance

## Required behavior

Performance work must preserve geometric accuracy, visibility classification,
stable references, measurements, document properties and Undo/Redo. Projection
tolerances and curve refinement depth are not reduced to obtain speed.

Unchanged view Properties close without committing a document or recalculating
its source. The comparison uses the initial normalized dialog values so that
opening a rounded numeric control does not rewrite higher-precision stored
coordinates. Creating a view always commits, even with unchanged controls.

## Projection reuse

### Interactive display and deferred output (2026-09-23)

Committed ordinary views, geometry-dependent editing and regeneration use
saved viewer polylines and triangles. Display edges carry per-vertex depth;
the interactive path does not refine exact curves for vector hidden-line
classification. An OpenGL depth pass separates visible and hidden strokes at
display resolution, using continuous gray hidden strokes like the 3D viewport.
The saved hidden-edge dash style applies to printing, PDF and vector export.
Drawing View Properties no longer exposes the obsolete hidden-edge style
selector. Canvas display already uses continuous thin hidden edges; editing a
view preserves its stored output style. The display-mode selector still controls
whether hidden edges are visible. This UI cleanup adds no user-visible text or
translation keys; the drawing GUI and five-language catalog checks cover it.
The Windows sheet canvas
also uses an OpenGL paint surface for sheet lines, labels and annotations.
Headless platforms retain their raster canvas; unavailable OpenGL geometry
rendering falls back to the established exact software path.

### Consistent GPU painter profile (2026-09-26)

Offscreen sheet captures inherit the current canvas's OpenGL format, or the
application default when no context is current. They must not force a legacy
OpenGL 2.0 context alongside the application's core-profile canvas. That mixture
caused Qt's image shader compilation to fail after placement/capture: a committed
view retained its model geometry but appeared as a white rectangle or empty frame.

The source-picker regression checks actual model strokes in the native GPU
framebuffer, the captured window and the framebuffer after capture, for both Part
and Assembly sources. It rejects both opaque white rectangles and missing strokes.
The failure was reproduced before the fix; native GPU rendering and copies of two
local saved Part models rendered correctly after matching the painter profiles.
No source geometry, projection tolerance, persistence, placement convention or
localization text changed. Linux native-driver verification remains separate.
The Windows GUI and Drawing harness were rebuilt. Drawing UI, source insertion,
view controls, breaks, details and the five-language translation contracts passed
(six checks). Evidence is retained in ignored `build/drawing-*.log` and the native
framebuffer/window captures.

The Drawing owns an immutable source mesh snapshot for deferred output. Native
Drawing format 21 / payload 13 saves shared source packets inside the `.drwz`,
along with interactive edge depths. Part and Assembly formats are unchanged.
Output does not reload a potentially changed or missing external source. The
printing renderer prepares exact vector strokes on a temporary view copy; PDF
and DXF use that same renderer. Preparing output does not alter Drawing history.

Sections retain their existing calculated intersection and hatch geometry.
Breaks clip and interpolate interactive edge depths. Details share the parent's
source and measuring geometry while retaining their crop and scale. Dimension
selection checks the depth of offered points without computing vector
visibility for the whole model. Exact source references remain authoritative.

### Exact projection sessions

During placement and new-view Properties, ordinary and projected views display
only a frame with the standard arrow cursor. Bounds use an already stored viewer
or measurement packet when available; otherwise the initial frame is provisional
(40 by 30 model units). Source loading and full display geometry are deferred to
OK. Explicit break/crop tools still prepare their real input when invoked.
Direction selection and parent alignment remain active. A
temporary sheet image avoids repainting existing geometry on each placement
mouse move; zoom, pan, resize and device pixel ratio invalidate that image.

Source sessions load a closed root model once into a private workspace and reuse
it for geometry, BOM, annotations and sections. Placement never requests that
bundle. Family choices and title-block parameters read
Assembly metadata without hydrating component geometry. The source chooser
deduplicates the same family. An Assembly with no sections can supply its empty
section list without loading any component geometry. OK still starts a fresh
authoritative source session. Temporary source workspaces omit Drawing histories.

The placement preview and its Properties dialog share one short-lived
`DrawingProjection` session. On OK, a fresh session reads current source data
from the authoritative workspace or native files. It may reuse the previous
session's camera projections only when all projection inputs compare exactly.
The comparison includes exact splines, analytic surfaces, visibility flags,
adjacent-face directions and complete occurrence identities. Geometry-bearing
floating-point inputs retain signed-zero distinctions.

Within an existing Properties preview, unchanged source/camera/section inputs
reuse edges, triangles, measurements and annotations. Name, position, scale and
display controls do not reproject geometry. Camera keys retain the exact numeric
bits, including signed zero. On OK, measurement geometry, source
annotations, BOM data and sections come from the fresh source. Section
calculations continue to consume current section settings. Nothing is written
to external geometry caches or added to the native format.

## Curve processing

Projection builds a face-to-rim-triangle index once. Exact spline boundaries
inspect only the rim triangles of their adjacent faces. Straight edges and
silhouettes do not allocate a full-model rim-interval array. Adaptive spline
refinement reuses quarter-point evaluations as child midpoints, preserving the
same tolerance, subdivision decisions and visibility calculations.

## Verification and profiling

The Drawing GUI contract covers unchanged OK without an Undo transaction or
coordinate rounding. The view command contract covers valid projection reuse,
source edits with unchanged identity, changed camera and signed coordinates.
Existing projection, section, dimension, export and Drawing UI contracts remain
required for this area.

To profile a real document without saving it, set `ZIMA_DRAWING_PROFILE_INPUT`
to its absolute `.drwz` path and run `zima-cad-drawing-harness --verify-ui` from
the repository root. Use `QT_QPA_PLATFORM=windows` and `QT_STYLE_OVERRIDE=Fusion`
to exercise the native GPU canvas. Offscreen platforms can test software fallback
but must not be reported as GPU performance measurements. The probe checks
the freshly projected stored views against their persisted edge coordinates,
identities and visibility flags, then measures unchanged Properties and insertion.

The 2026-09-23 baseline for `8073895_DGST-16-10-L-PA.drwz` was 29.2 seconds for
unchanged OK, 24.3 seconds before insertion placement and 28.9 seconds for
insertion OK. These are local diagnostic measurements, not universal limits.

The first optimized run, without concurrent builds or tests, measured:

| Operation | Before | After |
| --- | ---: | ---: |
| Unchanged Properties OK | 29.2247 s | 0.04896 s |
| Insert action before placement | 24.2539 s | 20.2635 s |
| Placement and Properties opening | 2.66286 s | 2.11483 s |
| New view OK | 28.8697 s | 5.93107 s |

Total measured insertion work fell from 55.79 s to 28.31 s in that first pass.
All three existing views reproduced their stored
edge coordinates, source references and visibility classifications exactly.
The user Drawing SHA-256 remained
`F8E113C56D84B5137690971B6A47D493BE9B907C2E598AC044B3E86006B611CA`.
Fourteen relevant translation, Drawing, section, dimension, annotation, thread
and GUI contracts passed after correcting outdated test expectations for split
projection axes, title-field ordering and excluded Origin axes.

The subsequent native Windows GPU run on an isolated copy of the same fixture
measured 4.67793 s before placement, 2.51054 s for placement/Properties, and
6.07929 s for new-view OK: 13.27 s combined. Unchanged OK took 0.149409 s.
Preparing isometric GPU geometry from the already loaded packet took 0.122023 s;
loading the closed native source bundle took 4.76005 s.
Four zoom/repaint steps including full-window screenshot readbacks took
0.837254 s. PDF export took 5.45206 s. These are individual local runs; the earlier
baseline used the offscreen canvas, so UI timing comparisons include the change
of rendering backend and are not a controlled CPU microbenchmark.

The deferred-output probe saves and reopens the native Drawing, then compares
its exact output against projection of the captured source packet before saving.
The comparison includes coordinates, source identities, hidden/tangent/silhouette
classification and thread/hatch flags. The exported PDF was rendered separately
for visual inspection. The deliberately central test insertion overlaps existing
views; this is test placement, not a modification of the user's sheet layout.
All fourteen relevant contracts passed with the final native OpenGL 3.3 canvas.
The GPU checks distinguish front/rear strokes, continuous hidden edges and shaded fill.

Localization review: existing dialog controls remain in use. The new native
edge-depth validation message is translated in all five Qt language catalogs.

### Placement frame and source-loading follow-up (2026-09-23)

Same isolated fixture, Windows GPU backend, sequential runs without concurrent
builds or tests. Logs: `build/drawing-insertion-before.log` and
`build/drawing-insertion-after.log`. Timings exclude human thinking time.

| Operation | Before | After |
| --- | ---: | ---: |
| Source bundle | 4.779 s | 2.252 s |
| Insert action before placement | 3.885 s | 0.869 s |
| Placement and Properties | 2.229 s | 1.612 s |
| New view OK | 5.325 s | 2.449 s |
| Complete ordinary insertion | 11.439 s | 4.930 s |
| Projected action before placement | 0.833 s | 0.905 s |
| Projected placement and Properties | 2.132 s | 1.574 s |
| Projected view OK | 5.278 s | 2.503 s |
| Complete projected insertion | 8.243 s | 4.982 s |

Projected action startup still loads the current source; its small increase in
this single run is not claimed as an improvement. Most gains come from avoiding
repeated root loading and geometry hydration for metadata. Ordinary insertion
takes about 57% less processing time; projected insertion about 40% less.
Four zoom/repaint screenshot steps were 0.833 s versus 0.762 s before; this change
does not establish an improvement to ordinary zoom. The sheet image cache applies
only during placement and is discarded when placement ends.

Sixteen relevant contracts passed, including localization, native GUI placement,
source selection, sections, details, breaks, dimensions, annotations, threads,
PDF and DXF. DXF checks retain dashed output lines; GPU checks require continuous
interactive hidden edges. The probe verified exact deferred output after native
save/reopen and captured `build/dgst-placement-frame.png` for visual inspection.
The follow-up introduces no user-visible text or persistent format changes.

### Interaction analysis after user feedback (2026-09-23)

This is an analysis checkpoint, not a claim that the following bottlenecks are
fixed. Only the diagnostic harness was extended during this investigation.
Run the same probe with `ZIMA_DRAWING_PROFILE_INTERACTION=1` for the additional
measurements; it changes transient controls, cancels, and never saves user files.
The native Windows run completed successfully; log:
`build/drawing-interaction-analysis.log`.

| Diagnostic operation | Total | Mean per operation |
| --- | ---: | ---: |
| 20 title-block layouts | 0.209 s | 10.4 ms |
| 20 native repaints without screenshots | 0.800 s | 40.0 ms |
| 20 alternating zoom events without screenshots | 0.922 s | 46.1 ms |
| Open existing view Properties | 2.262 s | — |
| 10 name edits with event processing | 1.472 s | 147.2 ms |
| 10 position edits with event processing | 1.392 s | 139.2 ms |

These are local loop timings, not hardware GPU timestamps or a frame-rate
guarantee. Layout is measured independently with an empty title context, so its
time must not be subtracted as an exact share of the full-sheet paint time.

Confirmed code paths:

- `drawing_window.cpp`: both insertion actions call `placement_source`, which
  still loads the actual native source mesh before showing the frame. Previous
  measurements were 0.869 s and 0.905 s for ordinary and projected startup.
- `show_view_properties` obtains its section choices through `cache->source`,
  which prepares geometry, BOM, annotations and sections even when no section
  is selected. Metadata selection is still coupled to geometric preparation.
- Every name, position and scale edit invokes the same preview callback, which
  calls `DrawingProjection::project`. Its interactive branch recaptures
  measurement geometry and reprojects edges/triangles without camera reuse.
  It then refreshes annotations and copies measurement geometry again.
- The placement frame is represented by two opposite points in a projected
  edge. After the dialog constructor loads its controls and section choices,
  `canvas_->set_preview(view)` exposes that placeholder to ordinary edge drawing,
  connects the two points, explaining the diagonal after placement. Frame
  bounds must be display metadata, never a drawable model edge.
- `DrawingDepthView::render` allocates its framebuffer and vertex buffer on
  every call, rebuilds/uploads vertices, copies each edge in the draw loop,
  then calls `toImage()`. This causes a GPU-to-CPU readback before composition
  in the OpenGL canvas. The 3D viewer instead owns persistent GPU buffers.
- Saved views without `output_source` still traverse their exact split strokes
  in QPainter on every paint. Merely owning a QOpenGLWidget does not remove
  that CPU work. All sheet views are traversed, including offscreen geometry.
- Every sheet paint rebuilds the title-block layout, including symbol mesh
  expansion and definition parsing, even when only the viewport changed.
- Selected views recolor model strokes cyan in both GPU and vector paths.
  The latest user requirement is cyan selection bounds only, retaining the
  normal geometry colors; topology/dimension reference highlights remain
  independent and must not be removed.
- Source sessions copy the complete Workspace. DrawingState copies current,
  Undo and Redo documents, although source preparation does not need Drawing
  histories. Its contribution is not separately measured yet and may grow
  during longer editing sessions.

Required next correction order: separate frame metadata and selection overlays;
remove source geometry loading from placement startup; separate lightweight
property changes from geometry-dependent previews; separate section/family
metadata queries from complete source bundles; retain render buffers/layouts
across paints with explicit invalidation; remove interactive GPU readbacks and
batch unchanged geometry. Preserve exact output, source freshness at OK,
sections, breaks, details, dimensions, picking and Undo/Redo. Expensive geometry
may be deferred until OK for ordinary new-view creation as requested; explicit
geometry-dependent tools still need their real input when invoked.

Operation sequence verified in code:

1. **Insert View**: resolve selected source/path; create a projection session;
   construct an empty isometric view; call `placement_source` (Workspace copy,
   native root loading/hydration if closed, authoritative mesh construction);
   project mesh vertices into two bounds points; begin transient placement.
2. **Projected View**: copy the selected parent; create a new projection session;
   load its source through `placement_source` again; construct a frame; retain
   parent scale/style. On each mouse move, calculate the direction sector/ray,
   update bounds only if direction changes, then update placement coordinates.
   The existing sheet image is reused while zoom/origin/size/DPR remain stable.
3. **Placement click / Properties opening**: copy the pending view; end placement;
   collect deduplicated source family choices; construct controls; load sections
   through the full source bundle; expose the pending view as the canvas preview
   and show the dialog. Control edits then invoke geometric preview preparation.
4. **Property edit**: read all controls into a copy of the initial view; refresh
   section marker/settings values; call the same full preview path regardless of
   whether the changed field affects geometry; replace preview and repaint.
5. **Wheel zoom**: calculate zoom and pan around the cursor; request repaint.
   It does not itself load native sources or run exact visibility subdivision.
   The costly work is in the following full sheet/render pass described above.
6. **View selection**: resolve the existing common candidate, synchronize selection,
   request repaint; both model stroke colors and the bounds currently change.
   User requests bounds-only feedback; reference-specific highlights are separate.
7. **Changed OK / new-view OK**: copy source Workspace, prepare a family variant
   if applicable, construct a fresh projection session, copy Drawing, validate
   and project the accepted view, update dependent projections/details and
   affected dimensions/balloons; publish permitted source edits; refresh sheet UI,
   variants and title context; commit Drawing history. Unchanged existing OK
   bypasses this path and just closes. The fresh source session currently makes
   an additional Workspace copy, including unrelated Drawing histories.

No product behavior was changed in that analysis phase. The implementation and
verification below supersede its pending correction list.

## Responsive interaction implementation (2026-09-23)

Placement now uses separate rectangular bounds, never a placeholder model edge.
Ordinary and projected placement reuse available source bounds without opening
the native source. A first view with no available bounds uses a provisional
40 by 30 model-unit frame. New-view properties retain this frame until OK;
explicit geometry-dependent tools still prepare their real input when invoked.
Selection changes only the cyan view rectangle, leaving geometry colors intact.

Existing-view previews reuse projection and measurement data when only name,
position, scale or display settings change. Source, camera and section changes
invalidate geometric reuse. Section-free Assembly metadata no longer prepares
the full source bundle. Commit still obtains current source data, and source
sessions omit unrelated Drawing histories from their temporary Workspace.

Rendering retains bounded GPU geometry buffers and framebuffers, batches edge
draw calls, caches unchanged view images, batches legacy vector strokes and
caches title-block layout. Placement backgrounds are prepared with an offscreen
OpenGL painter and reused until viewport changes. Changed GPU view images still
require readback for sheet composition; this is not a fully direct GPU compositor.
Printing retains its exact geometry and dashed hidden-edge path.

Measurements use the same immutable DGST fixture and native Windows harness as
the preceding baseline. Times include event processing and are single local runs,
not guaranteed frame rates. No source or user Drawing file was modified.

| Operation | Before | After |
| --- | ---: | ---: |
| Ordinary insertion action | 0.869 s | 0.126 s |
| Ordinary placement click / properties | 1.612 s | 0.149 s |
| Ordinary new-view OK | 2.449 s | 2.697 s |
| Projected insertion action | 0.905 s | 0.083 s |
| Projected placement click / properties | 1.574 s | 0.051 s |
| Projected new-view OK | 2.503 s | 2.175 s |
| Existing properties, interaction probe | 2.262 s | 0.102 s |
| Ten name changes with repaint | 1.472 s | 0.517 s |
| Ten position changes with repaint | 1.392 s | 0.521 s |
| Twenty native repaints, original three views | 0.800 s | 0.666 s |
| Twenty wheel events, original three views | 0.922 s | 0.615 s |

Ordinary insertion through OK totals 2.971 s instead of 4.930 s; projected
insertion totals 2.309 s instead of 4.982 s. Ordinary OK itself did not improve:
geometry preparation is intentionally deferred to confirmation.

The four-wheel-events-plus-window-screenshots probe regressed from 0.833 s to
1.900 s. It includes window capture and must not be described as pure zoom
latency. A separate native probe with an additional prepared GPU view, without
screenshots, measured 20 repaints in 0.663 s and 20 wheel events in 0.793 s.
This verifies responsive interaction in that scene but does not establish a
before/after improvement for the added GPU view; capture/readback remains a
known optimization opportunity.

All 16 selected drawing, command, source, output, UI, thread and translation
contracts passed (37.21 s). GUI checks cover preview drag and Cancel, frame
placement, family variants and geometry-dependent tools. GPU cache checks cover
geometry/depth invalidation, restored occlusion, continuous hidden edges and
shaded fill. The full insertion probe also verified save/reopen exact geometry
and generated PDF output. Localization review found no added user-visible text.

Evidence: `build/drawing-responsive-verified-tests.log`,
`build/drawing-responsive-final-interaction.log`,
`build/drawing-responsive-final-insertion.log` and
`build/drawing-responsive-gpu-interaction.log`. The normal development launcher
remains `zima-cad.bat`; the current native executable was rebuilt successfully.

## Direct projected placement and redundant painting follow-up

A projected view now inherits source, scale, display mode, hidden/tangent edge
style, thread-leadin visibility, caption visibility and dimension-guide settings
from its parent. Direction remains determined by placement and the sheet's
projection convention. Clicking commits directly through the same transaction
used by Properties OK, including fresh source preparation and failure isolation.
Escape before placement leaves the document unchanged. Later Properties editing
remains available. Projection does not copy plane-specific crop, break or section
definitions into a different camera plane.

Depth-image cache hits now return before making the offscreen context current.
Actual changed-image rendering still switches context, with restoration of the
caller context. Opt-in `ZIMA_DRAWING_PROFILE_DEPTH=1` reports cumulative elapsed
milliseconds for geometry checks, context activation, draw submission and image
readback. Baseline DGST sampling put changed-view rendering near 2.3–2.5 ms,
including approximately 0.3 ms between draw submission and readback completion;
readback alone was not the dominant full-sheet cost. These are wall-clock stages,
not hardware GPU timestamp queries.

The canvas parent no longer queues another child repaint from its own paint
event: the OpenGL child owns painting and receives updates directly.

`ZIMA_DRAWING_PROFILE_SHEET=1` isolates template, geometry and complete sheet
painting. It identified the expensive capture path: `QWidget::grab()` redirects
the OpenGL widget's QPainter to a raster target. Replaying the existing model
strokes there took approximately 440 ms, compared with approximately 20 ms
through the geometry stage on the native GPU painter. The redirected capture
now renders the same sheet through the existing offscreen GPU painter and copies
its final image once. If offscreen graphics initialization fails, the original
painter remains the fallback. PDF and print behavior is unchanged.

Final verification on the same immutable DGST fixture:

- Four wheel changes including complete window captures: **0.390 s**, compared
  with **1.900 s** in the preceding deployed build. This is a capture-inclusive
  benchmark, not a claim of fivefold faster ordinary wheel interaction.
- Projected-view action: **0.077 s**; placement click through completed insertion:
  **2.087 s**, with no Properties dialog or second confirmation.
- Ordinary insertion action: **0.116 s**; placement/properties **0.154 s**;
  OK **2.184 s**. Unchanged existing Properties OK: **0.117 s**.
- Cache-hit profiling reports only `cached`, without context activation or draw
  stages. Native image/depth invalidation checks still pass.
- All **16 of 16** selected contracts passed in **32.84 s**, including direct
  projection, parent display settings, Esc, later Properties editing, Undo/Redo,
  sections, breaks, details, source selection, PDF/DXF and localization coverage.
  The full probe verified exact output after save/reopen and produced a PDF;
  the GPU-captured sheet was visually inspected.

Evidence: `build/drawing-gpu-capture-build.log`,
`build/drawing-gpu-capture-tests.log`,
`build/drawing-gpu-capture-insertion.log`,
`build/drawing-gpu-capture-interaction.log` and diagnostic
`build/drawing-sheet-profile.log`. No new UI text was introduced. The main
executable was rebuilt for the unchanged `zima-cad.bat` entry point.

## Changed View Properties confirmation (2026-09-23)

The same immutable DGST fixture was measured through the native GUI, with three
iterations of opening existing View Properties, changing orientation twice and
confirming OK. Timing includes processing the resulting GUI events. Baseline
OK times were **2.531, 2.471 and 2.661 s** (median **2.531 s**); almost all time
was in `edit_drawing_view`, before dialog closure or final repaint.

Confirmation previously created a fresh source bundle even when the preview had
already loaded the same native source. Interactive camera results were not part
of the reusable precise-output camera cache. The explicit projection transaction
now reuses the prepared source bundle and its interactive camera geometry after
checking live source identities/generations and the exact native dependency
bytes. The loaded bundle is shared instead of copied. Annotation state, marker
refresh, child-view propagation, measurements and the existing commit transaction
still run with their original ownership and validation rules.

`NativeReadCapture` records native Part/Assembly inputs only while an explicit
projection read scope is active. Nested dependencies and missing source files
are included. Confirmation compares contents, not only timestamps or geometry
IDs. A modified, replaced, newly available or removed dependency invalidates
reuse; unsaved edits and closing/reopening live sources also invalidate it.
Outside that scope the native loaders perform no additional file reads. The
capture is temporary in-memory data, never a required sidecar or native format
change. Existing exact source-packet checks remain available after invalidation.

Final OK times with the same orientation sequence were **0.349, 0.344 and
0.339 s** (median **0.344 s**): approximately **7.4 times faster**, or **86% less
waiting**. Sending actual mouse-wheel events over the orientation combo produced
**0.360, 0.370 and 0.385 s**. Both probes verify changed confirmation and camera
restoration through Undo/Redo. First source preparation after opening the dialog
still costs about **2.3–2.4 s** on this fixture; this change removes its duplication
on OK, not that initial cost. Further orientation previews measured **0.006–0.162 s**.

Verification covers source geometry/identity, unsaved edits with the same runtime
identity, signed zero, exact deferred output after native save/reopen, a nested
Part changed while retaining its timestamp, newly available files, projection
hierarchies, sections, breaks, details, dimensions, balloons and Assembly/import
contracts. All 15 selected contracts pass across the regression run and focused
retest. The Show/Erase retest also verifies that removing a painted axis dot
retains the analytic center required by annotation geometry. No user-visible
strings were added; localization coverage passed.

Evidence: `build/drawing-commit-baseline.log`,
`build/drawing-commit-final-profile.log`, `build/drawing-commit-wheel-profile.log`,
`build/drawing-commit-regression-tests.log` and
`build/drawing-commit-verified-tests.log`. The current native application was
rebuilt successfully for `zima-cad.bat`. Profiling is opt-in through
`ZIMA_DRAWING_PROFILE_COMMIT=1`; the real-file GUI probe additionally accepts
`ZIMA_DRAWING_PROFILE_WHEEL=1` for mouse-wheel input.
