# STEP import interaction investigation and implementation

## Scope and evidence

Baseline source: `1919228b`. Windows local Release executables dated 2026-10-09.
User fixture: `Projects/import/G92K1.stp`, 175,494,212 bytes. Source-text counts
are 6,344 lines containing `ADVANCED_FACE(`, 17,216 containing `EDGE_CURVE(`,
3,324 containing `B_SPLINE_SURFACE`, and 729 containing `PLANE(`. These are
STEP source counts, not counts of final visible faces, occurrences or triangles.
The source fixture was not modified. The findings below describe the initial
investigation; the implemented changes and verification follow at the end.

Inputs are already calculated imported geometry and a pending placement.
Required outputs are visible movable geometry and prompt selection of an exact,
stable face with unchanged side, ownership and persistence semantics. Available
means include persisted viewer packets, existing Body transforms, and the shared
viewer candidate list. OCCT is appropriate for explicit calculation, not hover.

## Missing Body editing preview

`cpp/app/workspace/body_properties.cpp:85` restricts the editing scene to
`graph.available_before(position)`. `cpp/app/workspace/scene.cpp:1743` replaces
the normal display with only those preceding Body outputs. The edited ordinary
Body is consequently absent. The preview callback adds a transformed wire only
when `value.link` is present (`body_properties.cpp:154`). An ordinary Body
containing imported STEP geometry receives no equivalent wire in this path.

This is a source-level explanation for disappearance while editing **Body**
Properties. It is not evidence of deleted persistent geometry, and it does not
establish the cause of disappearance while editing **ImportedStep** Properties.
That separate command already captures persisted original edges before rollback
and submits them as transformed transient preview edges
(`primitive_properties.cpp:161`, `primitive_properties.cpp:1170`). The Body
preview was subsequently verified in the GUI on G92H1-T, as recorded below.

The user confirmed that Body movement must display its complete contents and
approved the discussed shared placement optimizations. The implementation uses
the complete calculated local mesh transformed by the existing pending Body
placement alongside the preceding context. It uses the pending document for
owned sketches, construction objects and origins. No STEP reload or OCCT
calculation is introduced into the preview.

## Work performed on every pointer movement

1. `MeshView::mouseMoveEvent` calls `update_candidates`; an active reference
   command with automatic hover advancement performs a second candidate update
   at the same position after its pointer callback. This second update supports
   commands whose callback changes the mesh; removal requires tracing that
   dependency rather than deleting it globally.
2. `ordered_viewer_candidates` reconstructs persisted identity membership from
   reference triangles and scans scene identities. `ordered_ray_candidates`
   tests every triangle, for both persisted and display geometry. Edge picking
   tests every eligible polyline segment. No spatial acceleration is used in
   these loops.
3. The placement command's candidate filter does substantially more than type
   filtering. For each candidate it prepares a prospective reference, calls
   `resolve_placement`, then computes orientation and point constraint degrees
   of freedom (`reference_selection.cpp:1069-1087`). This is solver work during
   hover, even before the user confirms a face. It is not live OCCT work.
4. For a general surface, `placement_mesh::visit` searches all stored reference
   triangles to locate one face. Surface classification and projection repeat
   this traversal. Analytic planes, cylinders and cones have a separate exact
   persisted surface path; not every face uses the general mesh path.
5. Face highlighting reconstructs boundary adjacency with a `std::map` inside
   painting (`mesh_view.cpp:5085-5172`). It scans reference triangles and rebuilds
   the selected face's boundary and silhouette data for each redraw. Rotation
   changes silhouette visibility, but does not change adjacency.

These are demonstrated code paths, not measured percentages of G92K1 interaction
latency. Exact triangle counts, GUI pointer timings, solver call counts and
paint timings for a successfully imported G92K1 document remain unmeasured.

## Candidate optimizations

- Build identity-to-triangle indices and exact boundary adjacency once per
  immutable viewer packet. Recompute view-dependent silhouette visibility from
  cached normals when the camera changes. Invalidate on actual geometry,
  occurrence or display-packet changes.
- Add spatial acceleration to the common picker while retaining exact leaf
  intersection tests, tolerance, ordering, RMB cycling and visible-fragment
  identity. Preserve the common list used by hover and confirmation.
- Reuse prospective placement validation only while the reference identity,
  baseline placement, reference-row state, source packet and any geometric pick
  seed remain identical. A face-only cache is insufficient if preparation depends
  on the pointer's point on that face. Changes in the shared placement filter,
  solving, preview or state contract require explicit user approval under AGENTS.
- Correct the ordinary Body preview locally, consuming the existing transform.

None of these proposals reduces geometry precision, drops source references or
changes persisted data. No speedup has yet been measured or promised.

## Separate import calculation costs

Importing a Part first inspects STEP, captures component body packets and frozen
B-Rep, then evaluates document history from those frozen shapes
(`step_model.cpp:75-96`). Assembly import deduplicates component definitions,
but also evaluates each generated Part. Investigation must distinguish these
explicit import calculations from already calculated interactive use.

Edge packet preparation projects sampled edge points and inside probes onto
surfaces (`occt_kernel.cpp:5411`, `occt_kernel.cpp:5440`). Complex spline surfaces
make global projection potentially expensive. Existing FORM preparation uses
validated edge p-curves and wire orientation first, with fallback to projection
(`occt_kernel.cpp:5538`, `occt_kernel.cpp:5992`). A similar STEP path is a candidate,
not a verified equivalent implementation. Validate SameParameter, seams, trimming,
orientation, tolerances and material/contact sides; compare complete packets.
See the earlier [import projection investigation](20261002-step-iges-projection.md):
simple projector reuse did not establish a speed improvement.

## Actual G92K1 diagnostic result

Command:

```text
build/cpp-windows-release/zima_cpp_step_model_contract_tests.exe --capture-file Projects/import/G92K1.stp 0
```

Inspection selected leaf request 0, definition `0:1:1:1`. Component capture did
not finish. A monitored sandbox run exited after 122.8 seconds with
`0xC0000374` (heap corruption). The same read-only command outside the sandbox
also failed, after 129.0 seconds, with the same exit code. The first unmonitored
attempt also failed; it provides no reliable elapsed-time evidence. Runs were
sequential. An unrelated synthetic benchmark overlapped part of the monitored
sandbox run, so these durations are crash observations, not controlled timing
comparisons. No other benchmark overlapped the outside-sandbox run.

Windows Application events establish an access violation `0xC0000005` in
`TKTopAlgo.dll` 8.0.0 at RVA `0xED532`, followed by heap-corruption reporting in
`ntdll.dll`, for both monitored runs. The local export table places that address
after exported `BRepGProp_Gauss::Compute` functions. This suggests investigation
of geometric-property integration, but is not a symbolized call stack and does
not prove the original corrupting instruction or responsibility for the defect.
Do not attribute this failure to STEP validity, memory exhaustion, meshing,
topology capture or the viewer without further isolation.

Diagnostic logs are disposable workspace outputs under `build/g92-capture*`;
relevant event XML is `build/g92-crash-events.xml`. No diagnostic process was
left running. The capture mode inspects the file and then transfers the document
again before calculating one selected component; its duration is not equivalent
to importing the complete file through the GUI.

The existing small STEP contract suite passed outside the sandbox with TEMP/TMP
confined to `build/diagnostic-temp`. It checks hierarchy, repeated definitions,
Bodies, placements, units and frozen persistence. Its initial sandbox invocation
stopped at a denied native-file rename, not a geometry assertion. Passing the
small suite does not establish large-fixture or GUI correctness.

## Implemented optimizations

- The ordinary Body preview consumes the complete local branch packet, including
  imported geometry, rather than displaying only predecessor Bodies. Pending
  transforms also apply to document-owned auxiliary geometry. A discarded cursor
  mesh is no longer assembled during Body previews without legacy Hole overlays.
- Display and persisted-reference meshes receive separate immutable AABB trees
  when their source packets are submitted. Broad-phase culling retains the exact
  existing triangle/edge hit tests, tolerance, candidate order and equal-depth
  tie behavior. Infinite edges remain eligible. Scene replacement rebuilds the
  index; camera motion does not rebuild it. Face membership uses precomputed
  distinct identities instead of all triangles on every pointer movement.
- Face highlighting indexes triangles by their persisted face identity and
  retains bounded adjacency packets. Camera-dependent silhouette visibility
  still uses the current camera; scene replacement clears the cache.
- One armed placement field retains up to 64 measurements and prospective
  validation results. Source reference, entry origin and scene revision control
  measurement reuse. Current row preparation still runs, and its full reference
  descriptor controls solver-result reuse, including explicit signed-zero checks.
  Re-arming the field or publishing changed geometry invalidates reuse.
- Ordinary command hover avoids a second picker pass when the pointer callback
  changes neither model nor selectable overlays. Mesh/overlay changes and cleared
  candidate lists require the second pass. Active Sketcher commands retain their
  original second-pass behavior.
- STEP edge-side capture consumes the existing verified SameParameter pcurve
  path already used by FORM, with the established global projection fallback.
  Geometry accuracy and persisted side identities are unchanged. Optional
  `ZIMA_CPP_IMPORT_PROFILE` output separates import packet preparation phases.

These changes use shared C++ code. OCCT remains pinned at 8.0.0 and the native
document format is unchanged. The development launcher remains `zima-cad.bat`.

## Representative measurements and equivalence

`Projects/import/G92H1-T.step` is 220,957,785 bytes. The tested leaf request 0
selects definition `0:1:1:2`, yielding 223,666 vertices, 308,352 display triangles,
308,337 reference triangles and 57,528 reference edges. This is one component,
not a benchmark of every leaf in the complete STEP hierarchy.

Two baseline captures took 72,000.3 and 71,513 ms. The final capture took
72,544.6 ms: no import-time improvement is established. All three have identical
complete packet SHA-256:
`b8aa06b9b0da6d600a90b4fba081d68c9bf0f9b7b265618972106bd311d1fc41`.
The comparison covers stored geometry, references, sides, BRep and physical
properties. A recursive JSON comparison also found zero differing leaves.

On that captured packet, 75 rays on a 5 by 5 grid along three axes returned
408 candidates. Complete ordered candidate lists were equal for every ray.
The first comparison measured 35.8512 ms per exhaustive query versus 3.20063 ms
indexed, approximately 11 times faster, with a 273.422 ms index build. A later
run measured 42.1417 versus 3.7049 ms, with a 361.316 ms build; compilation
overlapped that later run, so its absolute timings are not isolated. Exact
triangle tests fell from 46,251,675 to 1,774. These timings measure the common
picker, not complete dialog opening, command filtering or rotation FPS.

The final import profile attributes 14.34 s to aggregate surface properties and
15.16 s to face packets, versus 0.534 s to edge packets. STEP transfer/inspection
also occupies time outside these result-packet phases. Reducing mesh detail or
omitting physical properties was not used to improve these measurements.

## Verification and remaining limits

- New picker regression: 144 complete candidate-list comparisons, positive and
  negative/non-unit rays, multiple occurrence paths and tolerances, coincident
  faces, invalid triangles, infinite edges, all face/container selection options
  and a changed-source index rebuild passed.
- Existing native placement-reference assignment and mesh-surface placement
  tests passed, including finite boundaries, solution alternatives, sides and
  persistence. General-surface container placement remains supported.
- Existing STEP and import-command tests passed, including hierarchy, shared
  definitions, units, mesh-setting independence of exact volume, frozen native
  persistence after deleting the STEP, atomicity, Undo and stale workers. The
  STEP suite also passed with the pcurve/global-projection comparison enabled.
- G92H1-T native GUI test passed complete geometry checks after successive
  offsets 3, 7, -2 and 0 and rotation 15 degrees, unchanged camera, Cancel,
  unchanged OK without a new Undo transaction, changed OK, Undo/Redo and reopen.
  This diagnostic includes repeated 355 MB native saves/loads; its total duration
  is not a preview latency measurement.
- The final small import GUI regression passed the same history checks and
  actual imported-face selection: stable warm candidates, one ordinary hover
  pass, identical cold/warm highlight frame and exact clicked reference identity.
  This regression is registered as `zima_cpp_import_interaction_ui_contract`.
- The large captured G92H1-T packet also passed actual GUI face placement,
  warm candidate identity, one hover pass, identical cold/warm highlight frames
  after camera animation settled, and exact clicked reference identity. Twenty
  warm queries including placement filtering averaged 21.7794 ms. This is not
  an end-to-end dialog latency measurement.
- A native Body with two disconnected solids passed full own-geometry translation
  equations, preceding Body context and Cancel restoration. Its targeted GUI
  regression is registered as `zima_cpp_body_preview_ui_contract`. The broader
  existing Body UI diagnostic stopped earlier at the active-Body Tree/View menu
  policy check; a passing complete Body UI suite is not established.
- Existing face-fill image checks and all five translation/catalog checks passed.
  No new or changed product-visible text was introduced. The full UI executable
  passed its initial placement/DOF and interaction checks but then failed while
  parsing an empty JSON input in the existing default-Origin test; it is not
  reported as a passing full-suite run.

After the final Release rebuild, both registered targeted GUI regressions passed
through CTest (3.75 s for import interaction and 3.87 s for native Body preview).
The final test log is `build/g92-final-gui-ctest.log`. These small fixtures
complement the separately executed large captured-packet diagnostics above.

The G92K1 kernel crash remains unresolved. A full-tensor GK experiment at the
same precision was stopped after several minutes in its property phase and
removed from the implementation because it was substantially slower. It provides
no completed result or proof that the crash is fixed. Physical-property
integration remains on the established path.

Linux execution, complete multi-component STEP import, every native feature
dialog, every general-surface GUI placement variant, assembly nesting and
quantified rotation FPS remain unverified in this change. Existing user changes
in configuration and unrelated untracked files are excluded from committed
source and release assets. The translation diagnostic refreshes its existing
working-directory `unsaved-document-cs.png` screenshot; this is not a product asset.
