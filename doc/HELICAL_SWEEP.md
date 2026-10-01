# H-Sweep (Helical Sweep)

The properties preview shows the selected initial point alongside the base
circle, even before the radial guide is complete. Radial-guide and profile
Sketch editing retain both as passive context. The marker follows the resolved
base plane, container placement, Body placement and displayed occurrence; it
does not add editable geometry or a persisted reference. Preview edges are
submitted before point overlays so refreshing the wire cannot erase the marker.

H-Sweep is one Part history container with Solid, Thin and Surface results.
Solid and Thin support Add/Subtract; Surface supports Add only. It owns three Sketches;
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
   Solid accepts one closed region with holes and may be offset relative to Origin.
   Surface sweeps the contour without caps and accepts an open contour as well.
   Thin accepts a closed or open contour and uses the same thickness and side
   controls as 2D/3D Sweep. Symmetric places half the total thickness on each side;
   an open contour's direction determines its sides. Concentric circles remain
   available for a hollow Solid section.

The radial-curve endpoint ends winding even mid-turn. Turn count is absolute axial
height divided by pitch, not radial-curve arc length.

## Calculation and references

The factory approximation tolerance is 0.1 mm. **Custom precision** in Properties
enables a per-feature value in mm; clearing it restores the default saved when
the feature was created. `SweepPrecision/HelicalSweep` in configuration changes
the default for new features only. The setting affects body construction, not
just its display tessellation. See [the measured comparison](benchmarks/SWEEP_PRECISION_20260926.md).

Solid and Thin placement references offer the two endpoint caps. Surface has
no caps. Its rims and rails retain their source-curve and source-point ancestry,
including the terminal point of an open contour. Curved helical sides use the
existing [general-surface placement](GENERAL_SURFACE_PLACEMENT.md) contract on
persisted original triangles; they are not treated as analytic planes.
Whole-feature selection remains available.

Inputs → means → outputs: three Sketches and pitch → analytic spatial path,
controlled approximation, explicit OCCT Sweep → solid/thin material or an uncapped
surface in one history boundary. Changing result type does not change path precision.

See [result-mode verification and performance](performance/20260930-sweep-result-modes.md)
for analytical checks, localization, native templates and the Solid baseline comparison.

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

Entering any of the three Sketches frames that Sketch's finite geometry rather
than the entire model. A small cross-section therefore remains readable beside
a large preceding body. Empty Sketches fit available scene geometry; a genuinely
empty scene uses a monitor-based working scale. The other source Sketches and available winding
preview remain passive context during drawing and dimension entry; preceding
visible bodies retain the normal history-editing context. Finish Sketch returns
to the pending container and Cancel discards its pending edits.

Double-clicking H-Sweep in the View exposes the pitch as an axial length dimension
for one turn. Its witness points lie on the winding axis and its dimension line
is offset outside the base circle. The existing `pitch` identifier can be inserted
by the Relations picker; explicit Regenerate applies a relation-driven pitch.
The displayed pitch can also be edited directly through the ordinary dimension
editor. This is the positive axial pitch, independent of winding handedness.

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

### Start-marker verification (2026-09-30)

The H-Sweep GUI contract compares framebuffer output with/without the start
marker before a radial guide exists. It checks the selected point's exact
resolved position in radial/profile Sketch context after container translation
and rotation, including while line, circle and dimension tools are active.
The H-Sweep and neighboring 2D Sweep GUI contracts pass in
`build/helical-marker-tests.log`. Existing finish/commit and reopen checks remain
in the same H-Sweep test. No new user-visible text, document fields or shared
placement behavior was introduced; translation coverage also passes.

### Base-plane selection (2026-09-30)

The Properties window places the result type after the three Sketch buttons and
before the base Sketch offset. Thin thickness and side appear directly below the
result type when Thin is selected.

The base Sketch plane dropdown selects the container's own XY, XZ or YZ plane.
The signed base offset is measured along that plane's normal. The helix axis,
radial guide and section follow the selected plane; container placement stays
unchanged. Existing base Sketch plane and offset values are retained on opening.

Verification: the five-language translation contract checks all plane choices,
radial-guide alignment, retained negative offset and control order. The H-Sweep
GUI contract creates and saves a YZ-based feature at -3 mm, reopens its owned
Sketches and Properties, and verifies Cancel preserves the committed plane.
The 2D Sweep GUI contract checks own-plane choices, unchanged placement/camera,
owned Sketch entry, save/reopen and Cancel. No native format change is involved.

### Rotation axis and visible Sketch context (2026-10-01)

H-Sweep displays its rotation axis and two endpoint markers from the start of
creation, including while editing the three embedded Sketches. Before a guide
height exists, the transient axis uses one pitch as its provisional length.
Before the base circle exists, it passes through the base Sketch origin. Once
the circle and guide are defined, it passes through the circle centre and ends
at the signed guide height. Base plane, plane offset and container placement
are included. Winding handedness does not change the axis.

Explicit calculation publishes the rotation axis and its endpoint references
for Solid, Surface and Thin results. Their identities derive from the owning
container and base circle; they do not depend on OCCT enumeration or winding
approximation. Existing viewer reference serialization stores them inside the
native document. No document schema or shared placement contract changes.
The existing swept-spine centerline and attachment endpoints remain available.

Previous Sketch curves are passive 3D context wires. They must enter the normal
wire rendering pass after their Sketch keys are namespaced; retaining their
active-Sketch overlay flag would exclude them from both renderers. The GUI
contract checks a rotated guide-Sketch framebuffer with and without the base
circle, as well as context during line, circle and dimension entry. Sketch fit
continues to use the active Sketch, not the distant background context.

The Add/Subtract buttons in 2D, 3D and H-Sweep reuse Extrusion's vector signs,
44-pixel height, rounded border and selected/hover appearance. Selection rules
and Surface subtraction restrictions are unchanged. Existing labels are reused
in Czech, English, German, French and Russian; no new UI text is introduced.

Verification on Windows/Fusion: GUI and CLI builds passed. The complete Helical
model contract passed (171.25 s), including Solid/Surface/Thin geometry,
references, signed height, native reopen and regeneration. The H-Sweep GUI
contract passed (23.02 s), neighboring 2D Sweep passed (14.42 s), Extrusion
prototype passed (50.83 s), and five-language coverage passed (5.21 s).
Logs: `build/helical-axis-build.log`, `build/helical-axis-model-tests.log`,
`build/helical-axis-sweep-ui-tests.log`, and `build/helical-axis-ui-tests.log`.
The last log also records an initial runner mistake: adding `CONSOLE_ONLY`
dispatched the two Sweep entries to the general console contract, where its
GUI deletion assertion failed. Those entries were rerun with the correct
flags; the general console deletion failure was not addressed by this change.
The rotated context screenshot is `Projects/test/helical-rotated-context.png`.
No Linux verification or portable release packaging was performed in this change.

All property-dialog Sketch-entry buttons using the shared Sketch style now use
the Feature button's 6-pixel corner radius. This includes 2D/3D/H-Sweep, sheet
transition, section and Sketch properties. This is a shape-only adjustment:
existing sizes, labels, icons and editing callbacks are unchanged. No localized
text changes are required.

The shape-only follow-up was rebuilt on Windows. Five-language coverage passed
(4.94 s), and the 2D/H-Sweep GUI contracts passed (15.33/25.63 s). Logs are
`build/sweep-sketch-shape-build.log`, `build/sweep-sketch-shape-tests.log` and
`build/sweep-sketch-shape-ui-tests.log`. The broad dialog-layout entry did not
reach its layout checks: its preliminary interaction-colour fixture failed
with "Selection colour fixture has no hover candidate". Full dialog-layout
validation therefore remains unconfirmed; no layout dimensions were changed.
