# Feature selection wire verification, 2026-10-21

The face-fill selection described below was superseded by the user's later
wire-only requirement. Current implementation and verification are recorded in
[Authored preview wire selection](20261022-authored-preview-selection.md),
following the intermediate [result-edge experiment](20261021-solid-wire-selection.md).
The following results remain historical evidence for the preceding stage.

## Implemented behavior

Ordinary hover recolors existing result edges belonging to the feature's explicit
display owner. An adjacent face does not transfer another feature's boundary
ownership. Exact occurrence paths remain separate. Curved surface silhouettes
use their source triangle's persisted face owner in the existing GPU outline pass.

A single feature selection fills only its current result faces, using ranges
within the existing shaded vertex buffer. It does not expose the source/tool
wire. Explicit parameter inspection (including the shared double-click path)
shows the complete calculated source/tool wire at normal width. Clearing
selection or leaving inspection hides it. Source packets and model state remain
read-only. Command-specific geometry editing retains its established behavior.
The translucent face fill is drawn before ordinary result edges in each display
mode; it must not tint the normal wire by compositing over it.

The source wire is one cached GPU batch. Camera changes do not project its
segments through QPainter, recalculate geometry or upload unchanged vertices.
Contiguous result-edge draw ranges are prepared once per selected feature;
construction/dash/annotation/per-edge-color cases retain their established style
path. Input packet changes invalidate derived ranges and source geometry reuse.

## Windows checks

The current contract passes native viewer picking, the 29,000-edge GPU fixture,
ordinary feature selection GUI and Fillet/Chamfer/Shell command-selection GUI.
The real `1000.prtz` 003 fixture also passes. Eight single-selection pan/zoom
frames perform zero extra face-ownership builds, base uploads or source uploads.
Forty source-inspection pan/zoom frames perform zero extra source/base uploads,
edge/face ownership builds, silhouette ownership builds or silhouette uploads.
Clear/reselect and changed-source invalidation pass; serialized input packets
are identical. The original user file is loaded read-only.

Final warm observations: synthetic idle 16.87 ms / inspection 17.83 ms median,
19.23 ms p90; actual 003 idle 16.94 ms / inspection 16.35 ms median, 17.51 ms p90.
These are observations, not universal speedup claims. Logs:
`build/sketch-face-camera-selection-tests.log`,
`build/selection-face-cache-synthetic.log`,
`build/1000-feature003-face-cache.log`. Actual single-selection framebuffer:
`build/1000-feature003-face-cache.png.faces.png`.

The dedicated curved-silhouette GPU regression also passes eight mouse-hover
cases: four camera quarter turns and both viewing sides. A sphere fixture has
two distinct curved face owners and no persisted wire edges. In plain Shaded,
framebuffer pixels must show the requested owner's silhouette and exclude the
other owner's side. This isolates the silhouette pass from topological edges.
Evidence is appended to `build/selection-face-cache-synthetic.log`.

The current interaction-color GUI test also passes after replacing its obsolete
single-click wire-color expectation with explicit result-face fill, unchanged
normal wire, stable fill under hover, source-wire inspection and independent
reference highlights. It verifies that neither confirmation nor inspection
thickens the wire. The test exposed the initial overlay-order defect: compositing
face fill over the already drawn wire tinted it. Drawing that same fill once
before the ordinary edges in each display mode repairs the cause, without an
additional pass or geometry upload. Evidence:
`build/camera-interaction-colors.log`.

The earlier checks below are retained as dated evidence for
the GPU batching baseline; their former single-click source-wire behavior was
superseded by the current face-only selection contract.

## Earlier GPU batching baseline

- Native viewer contract: visible face reference selection retains the offered
  fragment while storage resolves its explicit original ancestry; cut edge
  ownership and repeated occurrence isolation are checked independently.
- Source-wire GPU fixture: 29,000 synthetic STEP-like display edges, two actual
  cut boundaries, one excluded other occurrence and eight source vertices.
  Forty pan/zoom frames cause zero additional source/base uploads and zero
  ownership rebuilds. Clear, reselect, parameter inspection and changed source
  geometry are checked, including invalidation and unchanged input packets.
- Actual `Projects/1000.prtz`, Vytažení 003: 96 calculated edges, 28 current
  result boundary edges and 40 source vertices. Forty camera frames likewise
  require zero additional uploads or ownership rebuilds. The source file is
  loaded read-only; its geometry is not regenerated by this test.
- The interaction-color GUI suite passes with equal ordinary/selected wire
  width. Sheet-state GUI verification passes across Part/repeated Assembly
  occurrences, inspection and restored poses. The solid-state GUI suite passes
  including properties, unchanged OK, restoration and Undo/Redo.

Synthetic warm framebuffer observations before the range-cache correction were
15.51 ms idle / 26.33 ms selected median. After correction: 17.12 / 19.83 ms,
21.55 ms selected p90. These separate runs are noisy host observations, not a
universal speedup promise. Actual 003: 17.13 ms idle / 16.28 ms selected median,
17.80 ms selected p90. Deterministic reuse counts are the regression gates.

Evidence: `build/selection-gpu-source-wire-final-tests.log`,
`build/selection-gpu-source-wire-batches-tests.log`,
`build/selection-interaction-colors-final-tests.log`, and
`build/1000-feature003-gpu-selection-tests.log`. The actual model framebuffer is
`build/1000-feature003-gpu-selection.png`.

Localization review: no product text changed in this rendering correction.
Native serialization and shared placement are unchanged. Linux execution and
every modeling operation's face-ancestry combination remain unverified.
