# Sketch view axes and editing priority

H/V input and markers follow the current perpendicular Sketch view. A quarter
turn swaps the screen assignment of the native horizontal/vertical relations;
front/back views retain the correct assignment. The viewer consumes the exact
published Sketch axes, including the existing placement and occurrence frame.
It does not change the solver, stored constraints, point identities or history.

Oblique navigation keeps the last perpendicular assignment in the viewer. H/V
markers remain visible. The same formatter handles marker painting, marker hit
bounds, transient inference labels and cursor labels. Active Sketch Tree names
and icons update without rebuilding the scene or emitting model-edit signals.
Automatic H/V enablement is interpreted in the same screen frame as manual input.

Ordinary Sketch editing prioritizes native points, editable annotations and
native curves over auxiliary references and axes. The shared viewer fallback
also applies after Escape or a command resets its selection contract. Explicit
command priorities retain precedence; eligibility, tolerances and right-button
candidate cycling are unchanged. Construction wires retain their dash gaps.

The Axis points visibility control also governs permanent point markers on
construction curves in the active Sketch. Hidden markers keep their real native
point identity for picking, snapping and dragging, with hover/selection feedback
when needed. Shared profile vertices retain their ordinary profile presentation.
The localized tooltip explains this scope in all five supported languages.

Feature selection uses the authored wire even when later coplanar merging leaves
only that feature's helper axis available in the final scene. Mirror and Pattern
preview/selection use the complete authored source wire, including nested copies,
instead of downstream Boolean fragments. This is display preparation without
solid calculation or an Undo transaction. The Mirror/Pattern dialog releases its
Tree editing transaction before the final Tree refresh, restoring Insert Here
immediately after OK or Cancel.

## Verification

- 768 native combinations: XY/XZ/YZ, four quarter turns, both front/back sides,
  H/V, native/external point supports, both selection orders, driving/reference
  length dimensions and locked/unlocked states. Successive scalar edits and
  drags check independent native/screen equations and read-only sources.
  Locked drivers leave the document unchanged; unlocked drivers follow existing
  dragging policy. Locked reference dimensions and impossible orthogonal
  relations are rejected. Sketch persistence retains definitions and IDs.
- 32 real GUI combinations: quarter turns, front/back, segment/point-pair input,
  oblique marker persistence, repeated actual endpoint drags, Undo/Redo and
  native save/readback. Camera-only changes preserve document and scene revision.
- Existing endpoint/axis overlap GUI coverage checks hover, clicks, repeated
  axis-contact drags and saved equations. A construction-wire foreground pixel
  sample covers one full dash pattern along the wire, instead of incorrectly
  requiring color inside a transparent gap.

Evidence is kept under `build/loft-audit/sketch-hv-*` and
`build/loft-audit/sketch-hover-*`. Dedicated native Windows verification is used;
Linux execution and the full nested-Assembly GUI interaction matrix have not
been repeated on this host. Existing localized H/V names and icons are reused;
no new product UI messages were introduced.

The combined [2026-10-25 acceptance record](verification/20261025-coordinate-sketch-selection.md)
also covers construction-point visibility, copied authored wires and Insert Here
restoration, including real Windows framebuffer and lifecycle checks.
