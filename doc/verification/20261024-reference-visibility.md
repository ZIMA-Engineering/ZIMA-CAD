# Reference visibility and numeric helper labels — 2026-10-24

> Historical verification record. The later 2026102702 interaction policy
> removes the Axis points toolbar action and hides auxiliary endpoints in the
> ordinary View; explicit commands reveal the same persisted points transiently.
> The 2026-10-24 results below describe the behavior verified for that release.

## Scope

- Complete scenes and live Feature Properties overlays share the same localized
  numeric helper-label formatter. Values, units, parameter IDs and editability
  are unchanged. The transient overlay does not republish or calculate a body.
- Infinite Sketch references from planar body faces render around the viewport's
  projection onto their support, rather than around the original seed midpoint.
  Zoom/pan therefore cannot move the finite display surrogate away from the
  visible part of the infinite line. Bounded edges retain their real extent.
- Reference strokes and external point markers render before authored Sketch
  wires, including when construction and ordinary stroke categories differ.
  Native lines/circles therefore remain in the foreground without changing
  selection, constraints or source data.
- A whole cylindrical body face viewed by a perpendicular Sketch provides its
  complete exact circular support, including angularly and axially trimmed
  cylinders. The existing rational quadratic circle representation is reused;
  the source face identity and existing native document format are unchanged.
  Selecting a particular edge still preserves its real arc. Oblique whole-face
  circular references are rejected; existing finite generators and explicit
  Face × Sketch-plane intersections retain their separate behavior.
- The **Axis points** View action, beside **Axes**, independently hides auxiliary
  axis points. It defaults on, preserves its state when Axes is toggled, does not
  change ordinary points, and retains existing explicit reference/inspection
  visibility. It filters drawing and ordinary picking through the same existing
  helper visibility predicate, without changing geometry or document history.
- New UI strings are supplied in Czech, English, German, French and Russian.

## Verification matrix

The final native Windows build passed five CTest contracts and three dedicated
GUI verification groups. The circular manipulation executable passed 776 cases.
All eight solid/thin planar/generator/partial-cylinder GUI scenarios passed.
Both reference-layer screenshots were visually inspected after pixel checks.
Catalog validation and the five-language GUI controls passed. Evidence names and
SHA-256 hashes are recorded in [the machine-readable result](20261024-reference-visibility.json).
The development launcher still opens the current local build. Verification used
isolated documents/settings and did not open the user's construction project.
Linux execution is not available on this Windows host; both platforms consume
the same C++ source.

### Automated checks

- Infinite-support equations: horizontal, vertical and oblique directions;
  two viewport sizes, three zoom scales, both large along-line displacements
  and both endpoint orders; degenerate seed rejection.
- Real framebuffer: six zoom/pan states for planar face references and a bounded
  edge control; overlapping line/circle foreground pixel controls. Actual face selection for solid/thin sources, persistence,
  unchanged source history, Undo/Redo and native reopening.
- Partial cylindrical source: real face selection for solid/thin sources in a
  perpendicular Sketch outside the source axial extent; exact closed-circle
  identity, source immutability, Undo/Redo and native reopening.
- Circular support equations: 1,025 exact samples after each of three radius
  changes; reversed cylinder axis, displaced Sketch plane, stable IDs, no-op
  refresh, missing/recovered source, and preservation of old stored face data.
- Circular manipulation matrix: native and external circular supports in XY,
  XZ and YZ, radius/diameter, locked/unlocked, fixed/free centers, both support
  comparison orders, repeated value edits and drags, rejected invalid edits,
  driving/reference dimensions and serialization. Existing trim/arc controls
  remain in the same regression executable.
- Axis points: action/default/icon, five-language labels/tooltips, parent-axis
  toggle preference, unchanged base scene revision, actual framebuffer changes
  and ordinary-point control, existing profile/origin/centroid axis selection,
  Properties Cancel/OK and native reopening.
- Numeric helpers: all five catalogs, direction ±1, pitch, pattern count/spacing/
  angle and Up To corrections; unchanged binding/value/unit and idempotence.
  Real Feature Properties direction overlay and existing repeated reversal,
  zero-crossing mouse drags, locks, no-op inspection, Cancel/OK, Undo/Redo/reopen.

### Explicit limits

This does not claim every Sketch constraint/trim combination was exercised on a
cylindrical face reference. The broad circle manipulation matrix runs natively;
new GUI coverage exercises face entry and persistence rather than every native
matrix cell. Linux rendering and mouse interaction remain unverified.
