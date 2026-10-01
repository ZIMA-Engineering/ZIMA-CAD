# Shading adjacency lookup — 2026-10-01

## Inputs, output and measured work

Inputs are the calculated viewer mesh, persisted face/occurrence references and
existing crease rules. The required output is the identical float vertex/normal
buffer sent to OpenGL, including normal summation order and sharp CAD boundaries.
The work is performed when a changed scene requires GPU mesh preparation.

Temporary instrumentation on the user's `Projects/02.prtz` spring measured about
26–32 ms for triangle preparation plus upload, 0.04–0.16 ms for line preparation
plus upload, and 30–33 ms for silhouette preparation. The direct shading probe
then measured the CPU normal/vertex preparation separately. Instrumentation was
removed from the production renderer after diagnosis.

## Change

`shaded_triangle_vertices()` used an ordered map to find triangles at each rounded
position, but never iterated that map. It now uses a reserved hash table with the
same three-integer key and equality. Each adjacency vector is still populated in
original triangle order. Coordinate rounding, crease threshold, face/occurrence
checks, mirrored-normal handling, float operations and output traversal are
unchanged. Consequently the optimization changes lookup cost without changing
floating-point summation order.

The table is local to one preparation call. Every changed mesh still constructs
its own current adjacency; there is no cache across geometry revisions. GPU
buffer lifecycle, silhouette generation, depth testing, common picking, OCCT,
persisted geometry and Undo/Redo are unchanged.

## Measurements

Baseline product source: `edd67ef6`. The same extended native benchmark was run
serially before and after on Windows/Fusion at 1200 × 800. For each fixture it
measures four shading calls and reports the mean of trials 1–3 below. Hashing is
outside the timed region. These timings measure CPU shading preparation, not
regeneration, whole-dialog latency or normal warm-frame rendering.

| Fixture | Before | After | Reduction |
| --- | ---: | ---: | ---: |
| 64 cylindrical occurrences | 6.238 ms | 3.341 ms | 46.4% |
| 256 cylindrical occurrences | 28.930 ms | 14.352 ms | 50.4% |
| 64 spherical occurrences | 39.703 ms | 28.663 ms | 27.8% |
| 256 spherical occurrences | 160.641 ms | 95.914 ms | 40.3% |
| User spring `02.prtz` | 28.143 ms | 16.003 ms | 43.1% |

The native Part is calculated once for benchmark preparation and never saved.
Its calculation is excluded from measured shading work. There is no claim of a
corresponding percentage improvement in total application time or warm repaint
speed. Silhouette preparation remains a separate candidate for further work.

## Equivalence and verification

All 20 SHA-256 hashes of the complete float position/normal buffers match between
versions. All 200 corresponding RGBA framebuffer hashes also match. The existing
benchmark covers neutral display, actual common-picker hover and confirmation,
five display modes, opaque/translucent bodies, standard-view changes, pan, zoom,
tiny rotation, perspective, changed vertices with unchanged array sizes, empty
mesh and restored mesh. Repeated frames must stay stable and highlight changes
must not change base geometry revision or selected occurrence identity.

The viewer contract checks planar CAD creases, face/occurrence separation,
duplicate seam vertices, unselectable curved result meshes and reversed triangle
winding. No new user-visible text is introduced; localization review and the
five-language catalog contract apply. Documentation and test diagnostics are
English. The root Windows launcher is unchanged. Linux verification and portable
packaging are outside this change.

Evidence: [before/after benchmark and test logs](20261001-shading-adjacency.txt).
Run `zima_cpp_silhouette_view_benchmark` with optional native Part paths to reproduce
both direct shading and complete-frame checks. It requires desktop OpenGL.

The local Windows GUI and benchmark were rebuilt successfully. All six selected
contracts passed in 42.77 s: Helical Sweep GUI, translations, viewer contracts,
refresh scope, selection filters and surface-profile GUI.
