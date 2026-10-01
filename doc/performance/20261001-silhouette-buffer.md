# Curved-surface silhouette buffer reuse — 2026-10-01

## Inputs, output and cause

Inputs are the current calculated viewer mesh and the exact view-direction vector
already used by the silhouette classifier. The required output is the identical
line list, depth behavior, colors, selection identity and frame. The available
means are the existing per-View OpenGL buffer and mesh-upload lifecycle.

Previously every line-display repaint traversed all internal triangulation-edge
candidates, allocated a temporary vertex array and uploaded its visible segments.
Hover, confirmation, pan and zoom repeated this work even without changes to the
classifier inputs. Plain Shaded mode does not draw these lines.

The View now retains the completed GPU line buffer and its vertex count, keyed by
the exact three float components of the calculated direction. It does not quantize
camera rotation or change the existing silhouette threshold. Mesh upload and GL
buffer creation invalidate this state. An empty classified result is cached too,
and a later rotation or mesh replacement still recalculates it. Display colors,
depth testing and highlight processing continue on every frame as before.

This changes presentation work only. No OCCT calculation, topology/reference
identity, geometric precision, model persistence or Undo transaction is added or
removed. It introduces no product text or native-file format change.

## Measurement method

`zima_cpp_silhouette_view_benchmark` runs on the Windows desktop at 1200 × 800.
It builds 64/256-occurrence circular Extrusion and spherical Revolution scenes,
and accepts native Part paths for additional representative geometry. The checked
`tilted-cone-with-bends.prtz` reproduces a user's inclined conical sheet with bends;
its authored history is calculated once for benchmark preparation. Source files
are never saved. Geometry preparation is excluded from repaint timing.

For each scene, the benchmark compares neutral, common-picker hover and LMB
confirmation across all five display modes, with alpha 255 and 120. It records
the median and maximum of seven completed framebuffer reads after warming the
state. These include GPU rendering and readback, not just CPU classification;
they are not timings for regeneration or a general application speedup.

It also records frames after Top/Front/Isometric transitions, pan, zoom, a
0.00001-degree rotation, perspective projection, changed vertices with unchanged
array sizes, an empty scene and restoration of the original geometry. Before/after
RGBA SHA-256 values test identical output, while repeated warm frames and unchanged
base-mesh revision check stable presentation. Selection assertions retain the
common picker's exact candidate including its occurrence path.

Baseline product source: `52624fd4`. Both runs use the same benchmark source and
fixture, with the production renderer changed only for the optimized run.

## Scope and limitations

This cache does not accelerate silhouette preparation while the view direction
continuously changes. It also does not address other scene rebuilding, picking,
transparent sorting or repeated draw calls. No precision or display-quality
reduction is used. Linux validation remains deferred to the Linux host.

Localization review: no user-visible strings changed. Benchmark diagnostics and
this documentation are English. The local launcher remains `zima-cad.bat`.


## Direct preparation probe

Whole-frame readings showed variability even in the unchanged plain Shaded
control. A second paired run therefore temporarily timed the actual renderer
block from immediately before view-direction calculation through the silhouette
buffer bind/upload, ending before `bind_attributes`. The probe used
`std::chrono::steady_clock`, converted the elapsed value to microseconds, then
wrote `silhouette_profile candidates=<N> us=<value>` to the redirected log. Logging
occurred after the elapsed value was captured. This includes driver upload calls
when present but excludes draw calls and framebuffer readback.

The same probe was applied to baseline and optimized source. For each warm
mode/alpha/selection group, the first framebuffer grab was discarded and the
remaining eight preparation samples were retained (seven timed frame grabs plus
the stability-check grab). Cold preparation and transition frames are excluded.
The probe is absent from committed production code and from the final local build.
Raw probe logs are `build/silhouette-profile-before.log` and
`build/silhouette-profile-after.log`; the summarized samples and complete
uninstrumented frame logs are retained in the accompanying measurement record.


## Results and validation

Each row aggregates 192 warm preparation samples across the three interaction
states, four line-display modes and two alpha values. Times below are medians.

| Scene | Candidates | Before preparation ms | After preparation ms | Saved ms |
| --- | ---: | ---: | ---: | ---: |
| 64 circular Extrusions | 6,272 | 0.03090 | 0.00070 | 0.03020 |
| 256 circular Extrusions | 25,088 | 0.16860 | 0.00080 | 0.16780 |
| 64 spherical Revolutions | 49,216 | 0.37975 | 0.00060 | 0.37915 |
| 256 spherical Revolutions | 196,864 | 1.47320 | 0.00080 | 1.47240 |
| Native inclined cone with bends | 714 | 0.00650 | 0.00040 | 0.00610 |

The independent scale check agrees with the cause: the old cost grows with the
candidate count, while warm reuse stays approximately constant. The largest
controlled fixture saves about 1.47 ms of preparation per unchanged repaint. The
small native user-derived fixture saves only about 0.006 ms; it is not evidence
for a large speedup of ordinary small models.

Completed-frame timings remain mixed. For confirmed opaque Shaded-with-edges,
the 256-sphere scene measured 34.405 → 34.966 ms and the native cone
5.702 → 5.780 ms. The unchanged plain Shaded control also varied
(32.893 → 34.595 ms and 5.682 → 7.643 ms respectively). These runs do not establish
a reliable whole-frame speedup. The accepted result is the directly measured
removal of repeated preparation, with identical frame output.

All **200** uninstrumented before/after RGBA hashes match exactly. This includes
empty/nonempty silhouette transitions, small rotations and changed geometry with
the same array sizes. The viewer, selection-filter GUI and five-language catalog
contracts pass **3/3 in 9.34 s** (`build/silhouette-contracts.log`). GUI, CLI,
benchmark and viewer test targets build successfully; temporary profiling code
was removed before the final build (`build/silhouette-final-build.log`).

[Measurement record, including all frame hashes](20261001-silhouette-buffer.txt).
This step produces a local Windows build and source commit, not a new portable
release archive.
