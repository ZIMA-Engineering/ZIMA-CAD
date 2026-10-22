# Sketch entry camera, 2026-10-21

This record describes the camera-only stage. The subsequent isolated first-face
prototype also changes new automatic profile placement to real local XY; its
separate scope and verification are recorded in
[First-face local XY mapping](../SKETCH_LOCAL_XY_REFERENCE_PLAN.md).

## Scope and implementation

Ordinary Part/Assembly Sketch entry looks toward the resolved Sketch plane along
its negative normal. The resolved positive X axis points right and positive Y
points up. Only `align_active_sketch_view` changes its initial camera direction;
the existing resolved model frame, Body/occurrence transforms and camera-roll
calculation are consumed unchanged. Drawing templates retain their intentional
right-anchored convention. Symbols retain their established conventional view.

No placement/reference solver, plane frame, geometry-side field, extrusion
direction, native constraint, document format or Undo contract changes. Camera
Flip remains an explicit opposite-side view: X stays right and Y reverses. Four
camera quarter turns return to the entry view; Normal restores that view.

The separate requested H/V label/input adapter for camera quarter turns is not
implemented by this camera-only change.

## Windows verification

`zima_cpp_sketch_entry_view_ui_contract` passes in 41.84 s (43.40 s in the final
render-order integration rerun) using the isolated
`C:/zcb/reference-next` Release build, Qt 6.11.0 and pinned OCCT 8.0.0:

- XY/XZ/YZ entry in an ordinary and a genuinely translated/rotated Body: six
  cases. Body rotation uses authored absolute angles, not disposable resolved
  angle fields. Each checks entry, two flips, four quarter turns and Normal.
- Real calculated start/end cap references of Solid and Thin Extrusion: four
  cases. The ordinary shared Properties reference-entry handler accepts the
  actual calculated face identity, then the actual Own Sketch button enters
  Sketcher. Returning retains the pending placement references, side and
  extrusion direction/lengths. Cancel and native save/reopen leave source
  history, Sketch geometry and Body hierarchy identical.
- Screen-ray intersections independently check positive axis directions. Unit
  axes transformed by the actual camera quaternion check orthogonality and
  absence of unintended roll. This avoids cancellation error from subtracting
  translated float ray endpoints; no product precision was reduced.
- Every camera-only action checks an unchanged document revision. Existing
  driving R dimensions include locked/unlocked states in the six saved cases.

The preceding broader run also passed the owned Extrusion/Revolution profile
frame matrix (115.15 s), Sketch/Holes/Flat/Bend return-frame matrix (157.44 s),
native Sketcher equations, rolled-L edit/drag matrix, native title-block GUI,
symbol GUI, eight Equal-length endpoint GUI cases and four equal corner GUI.
That run initially rejected the translated Body camera because its ray-based
roll oracle accumulated float cancellation; the unit-vector oracle above now
passes that same true rotated Body fixture.

Evidence: `build/sketch-face-camera-selection-tests.log`,
`build/sketch-camera-selection-final.log` and
`build/sketch-camera-rolled-l-regression.log`. Screenshots are under
`Projects/test/sketch-entry-view-*.png`. Source code is shared with Linux; Linux
execution is unverified. The new cap test uses the real shared reference-entry
handler and Sketch button, not a mouse search for each cap. The separate cap
reference mouse-selection tests remain independently scoped.

The user's normal development launcher and executable are not replaced by the
isolated build. `tmp/zima-cad-sketch-camera-test.bat` starts the isolated executable
with the repository working directory for optional manual acceptance. No new
product-facing text is introduced; current-source coverage,
placeholders, catalog validation and GUI translation checks pass for
cs/en/de/fr/ru (`build/camera-localization-coverage.log`). The first sandboxed
run could not create its temporary nested directory; the permitted unsandboxed
run passes. No library asset regeneration is required.
