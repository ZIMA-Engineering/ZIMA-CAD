# FEM result inspection and deformation animation

This change adds transient presentation of the existing linear-static result.
It does not introduce a dynamic or thermal solver or modify native documents.
The default magnification remains one. Results offers play/pause, magnification
and speed settings, an optional transparent original, extrema and click probing.
The shared internal properties window owns magnification/speed editing and its
OK/Cancel and middle-button confirmation behavior.

The calculated mesh remains immutable. A separate displacement attribute is
uploaded once; each animation frame changes a shader uniform. The same triangles
provide the colored surface, mesh wire and neutral transparent comparison skin.
Legend values and colors retain the full calculated load case throughout the
cycle. The displayed magnification is explicitly labeled. Min/max and selected
result markers follow the same deformation factor.
Perspective clipping includes the current deformation and optional original;
Fit All includes the displayed result envelope without changing native bounds.

Result picking intersects the currently displayed triangles, including the
current animation factor. A displacement probe identifies the nearest corner
node of the hit surface triangle and reports its calculated displacement
magnitude. A stress probe reports the owning tetrahedron's volume-averaged von
Mises stress. The transparent original is not pickable. Ordinary CAD candidates
are suspended only while explicit result probing is active. Outside probing,
stationary load/restraint annotations retain their selection behavior.
Mesh/source/material/condition validity remains
owned by the existing analysis cache.

No result controls are persisted in native documents. They do not create Undo
transactions, regenerate CAD, remesh or solve. Source changes invalidate the
existing solution as before. All added UI text is supplied in cs/en/de/fr/ru.

## Verification

Windows verification passed:

- Independent translated-triangle picking in orthographic and perspective
  views, both windings, and factors 0, 0.5, 1 and 2. The hit is checked against
  the actual triangle equation. Actual mouse input retains node identity;
  previous positions and the transparent original do not become probe hits.
  Scene replacement clears analysis picking and markers.
- Full FEM GUI coverage, including magnification Cancel, automatic sizing,
  speed, short/double middle-click confirmation, actual node probing,
  owning-element stress probing, extrema, play/pause and hiding results.
  Animation retained the calculated arrays, document revision, CAD calculation
  count and geometry/vector/color upload counts.
- Settings and actions in all five languages and both themes, including actual
  dialog labels, the common Cancel action and screenshots. The GUI checks use
  the production loader for both INI and supplemental JSON catalogs.
  Translation catalog/UI validation passed.
- Existing FEM pressure, region, material-failure cleanup, cache invalidation,
  Undo/Redo and native save/reopen checks passed. The native pressure/stress
  regression matrix passed for linear and quadratic elements.
- Existing imported-Body GUI checks passed: ordinary face picking, authored
  preview, repeated offsets/rotation, Cancel, unchanged/changed OK, Undo/Redo
  and native reopen. This exercises the ordinary renderer with analysis off.
- A read-only copy of the saved twisted-profile case meshed, solved and rendered
  the enlarged result with its original and extrema. Maximum displacement was
  0.167992 mm and maximum element-average stress approximately 10.0824 MPa.
  Forty frames at factors from zero to the chosen 325.951 magnification took
  a median 5.560 ms and maximum 7.970 ms, including framebuffer readback.
  Geometry/vector uploads and document/CAD calculation counts stayed unchanged.
  The complete real-document GUI run, including meshing, solving and captures,
  took 58.866 s. This is not a controlled end-to-end comparison with older runs.

The source-copy SHA-256 remained unchanged. No factory/template conversion is
required. Linux execution has not been verified on this Windows host.

## Solver cost

Profiling the saved twisted-profile case separated volume meshing (about 2.2 s)
from the dominant iterative solution (13,122 iterations on the recorded mesh).
The matrix-free element product now specializes its fixed T4/T10 cardinalities,
gathers each input node once per element, and reuses the same shear-stress tensor
for the element's test functions. Integration, accumulation order, precision,
preconditioning and convergence criteria are unchanged.

On the identical recorded quadratic mesh (33,920 nodes; 16,709 elements), the
original implementation took 55.291 s with a progress observer and 53.722 s
without one. The revised implementation took 43.068 s and 43.349 s respectively.
Every displacement component, reaction component and element stress was exactly
equal in the JSON round-trip comparison. Both implementations used 13,122
iterations. This is one Windows case, not a universal speedup claim.

The diagnostic harness replays recorded node coordinates only after confirming
identical element order and connectivity. This isolates solver comparison from
small run-to-run coordinate differences in Gmsh high-order optimization. These
JSON files are disposable test evidence and are never native document storage.
