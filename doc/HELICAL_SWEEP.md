# H-Sweep (Helical Sweep)

H-Sweep is one Part history container with Add/Subtract. It owns three Sketches;
creation/edits stay pending in one internal window until OK. Finish Sketch returns
to that window. Cancel discards the whole pending container. Editing uses persisted
pre-container input and restores normal history afterward.

Add/Subtract are the bottom Properties button pair shared with Protrusion. Selection
remains pending until OK.

## Inputs

1. Base Sketch lies normal to the winding axis. The first version accepts a circle
   and exact start point on it. A sole circle/standalone point is preselected;
   adding points does not change persisted point identity. Circle center defines the axis.
2. Radial Sketch lies in the axis/start-point plane with anchored Origin. Local X
   is radius change, Y axial height. It contains one open continuous unbranched path
   with tangent joins: segments, arcs, elliptical arcs, or open splines. Height must
   progress in one direction, radius must not reach the axis, and tangents cannot be
   purely radial.
3. Pitch is positive axial travel per revolution. Right/Left changes winding direction;
   default is right-hand.
4. Cross-section Sketch starts at the spatial path start, normal to its tangent.
   It accepts one closed region with holes and may be offset relative to Origin.
   Concentric circles create a hollow section with wall thickness equal to radius
   difference. This command has no separate Thin option.

The radial-curve endpoint ends winding even mid-turn. Turn count is absolute axial
height divided by pitch, not radial-curve arc length.

## Calculation and references

Inputs → means → outputs: three Sketches and pitch → analytic spatial path,
controlled approximation, explicit OCCT Sweep → solid added/subtracted in one history boundary.

For radial path `(x(u), y(u))`, radius is `R + x(u)`, height `y(u)`, and angle
`±2π y(u) / pitch`. The base circle defines center/initial radial direction. Tangents
include winding, axial motion, and radius change. Ends are not rounded to full turns.

Preview uses only ZIMA data: path, end cross-section outlines, and longitudinal
connectors. Section frames transport along the path without extra prescribed twist.
OCCT runs on OK/explicit regeneration, checking body validity and self-intersection.

Start/end faces have distinct `start:from:…` and `end:from:…` identities parented to
the profile region. Pitch, height, and handedness changes do not swap roles. Analytic
planes persist in original-reference geometry for later features. Side faces reference
source profile curves; edges reference their source curves/points. Approximation sample
indexes are not persistent topology identities.

Placement/orientation uses ordinary container controls: three position references,
FRONT/TOP, X/Y/Z, absolute rotations/corrections, orientation flip, and ORIGIN.
View/Tree share picking. Creation arms the first position reference. Short MMB ends
reference entry; double-click confirms the whole window.

Base Sketch uses a container-local plane, default XZ / FRONT; stored Sketches retain
their plane. Properties has no separate “Sketch Plane in Container” row. All three
Sketches adopt the same placement, moving the entire winding together. Derived planes
belong to the feature; there is no independent world base-plane reference. Shared
container placement is unchanged. Placement preview shows Origin without an added
helper construction axis. Entering an owned Sketch aligns camera with its actual
plane as ordinary document Sketches do.

While Properties is open, all three source-Sketch wires remain beside the winding
preview, even with incomplete/invalid paths. Derived planes update from available
inputs; missing inputs retain the last solved Sketch frame. Display uses Sketch data
without OCCT body calculation.

## Verification

`zima_cpp_helical_sweep_contract_tests` checks handedness, partial turns, cylindrical
winding volume, variable radius, radial arcs/splines, hollow sections, invalid paths,
self-intersections, and persistent start/end references after edits/save/load.

Application integration:

```sh
ZIMA_VERIFY_HELICAL_SWEEP_ONLY=1 ./build/cpp-debug/zima-cad-cpp --verify-startup
```

It covers creation, all three Sketch editors, return to Properties, OK, saving,
reopening, owned-Sketch tree, Cancel without mutation, and shared Add/Subtract buttons.

Path calculation uses cubic segments with sampled-deviation checks against document
linear tolerance: half for path, half for OCCT Sweep. Sampling parameterization does
not affect face identities. Current calculation rejects over 1000 turns per container.
Precision comes from File Settings; see [Numerical precision](NUMERICAL_PRECISION.md).
Mesh deviation also controls rendered winding-edge detail.

### Calculated-solid centerline

The solid publishes a dash-dot source-curve centerline (`centerline:from:<source_id>`).
Segments, fillets, splines, and helices retain shape; approximated portions share their
source reference. Only straight portions also offer axis references. Display respects
Axes visibility, including shaded mode. Geometry persists during calculation;
rendering/picking invoke no OCCT. Earlier models gain it through explicit Regenerate.

## Console and base-Sketch offset

`helical.create` adopts three existing standalone Sketches; `helical.get/set` shares
Properties transactions. Arguments/input guards: [SWEEP_COMMANDS.md](SWEEP_COMMANDS.md).

Properties includes base-Sketch offset in mm with a value lock. Adopting a Sketch
preserves its offset. Editing shifts the base-circle plane and whole winding along
its normal without changing container placement/references. The value remains in the
native base Sketch; OK commits, Cancel discards pending changes.
