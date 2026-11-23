# Initial CAM calculation core

Status: calculation foundation only. The CAM Body, native document storage,
properties, NC pane and viewport integration remain in progress. This report
does not certify a controller program or a complete CAM product feature.

## Scope and contracts

Inputs are an immutable generic program, machine/tool definitions and private
native stock, target and fixture geometry in one work frame. Outputs are NC
inspection lines mapped to stable blocks, exact continuous motion, remaining
material and individual collision findings. The means are the shared C++ core
and OCCT 8.0.1; there is no separate Windows geometry implementation.

The tool origin stays at the cutting/shank boundary. Tip and mount offsets are
derived separately. Machine travel checks include circular extrema at the
mounting point, with the declared work offset. Internal lengths are millimetres;
inch output converts coordinates/feed without altering the geometry.

The native sweep uses exact cylindrical/conical primitives for radial profile
segments. Straight motion sweeps monotone boundary strips. Circular motion uses
annular sectors and endpoint solids, clipping an inner cone when it reaches the
path axis. Material removal does not sample tool positions at animation frames.
The selected linear Boolean tolerance is explicit, defaulting to 1e-7 mm.

Simulation copies source topology and geometry before any native validation or
Boolean operation. A fixture serialization check exposed mutation of lazy OCCT
flags despite non-destructive Boolean settings; private copies fixed that
failure. Tool, fixture, target and stock ownership remain separate. Rapid moves
report interference without removing material. Cutting moves report target
gouges; shank and fixture collisions retain the block and obstacle identities.
The result also measures target material absent from the starting stock.

Automatic face milling generates a deterministic raster with cutting-length,
tip-diameter, clearance and axial-allowance checks. Every generated candidate
requires compilation and native simulation before acceptance by the future
workspace integration. No accepted-result publication exists in this core alone.

## Verification

Windows x64 Release, MSVC 19.51, OCCT 8.0.1:

- `zima_cpp_cam_program_tests`: 77 assertions covering linear/rapid motion,
  clockwise/counterclockwise and full-circle arcs, intermediate positions,
  lengths/extrema, block-to-line identity, unchanged authored input, millimetres
  and inches, tool-origin/mount offsets, exact travel boundaries, work offsets,
  unsupported machines, invalid tools, feeds, spindle commands and identities.
- `zima_cpp_cam_sweep_tests`: independent cylinder capsule volumes and convex
  frustum projection integrals, positive/negative/diagonal motion, pointed and
  tapered profiles, shoulders, stationary tools, open/full circular paths in
  both directions, translated centers and tools wider than their path radius.
  Grid classification uses analytical distance to the arc rather than sampled
  tool poses. All generated shapes must pass native validity checks.
- `zima_cpp_cam_simulation_tests`: 37 assertions covering analytical removed
  volumes, two successive stocks, repeated air cutting, rapid collisions,
  separate cutter/shank fixture collisions, stock/shank interference, target
  gouges, insufficient initial stock, fully consumed stock, cancellation,
  immutable source serialization and deterministic face milling with allowance.
  Automatic facing reduces a 2000 mm3 stock to the independently specified
  600 mm3 target; retaining a 2 mm allowance leaves 1000 mm3.

The final three CTest targets passed together in 9.03 seconds on this host
(0.04 / 6.87 / 2.08 seconds). Logs are retained in the local
`build/cam-20261123` directory. These synthetic timings are observations, not
user-model performance promises.

Localization review: this foundation introduces no application controls or
user-visible UI text. Generic NC tokens form a defined interchange syntax;
internal exceptions must be mapped to localized UI diagnostics during command
integration. Existing source/catalog/runtime translation checks passed in all
five languages. No application model or native file format is changed in this
calculation-core change. Linux execution, integrated Undo/Redo, save/reopen,
source invalidation and GUI verification remain pending integration gates.
