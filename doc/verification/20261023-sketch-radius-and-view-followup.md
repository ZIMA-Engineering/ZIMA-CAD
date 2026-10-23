# Sketch radius and viewer follow-up

This development follow-up is for the next release. Signed Windows 2026102301
and its tag remain unchanged. No document-format, dependency or container
placement contract changes are required.

## Equal corner radius editing

An explicit corner-radius gesture now supplies its edited native radius as
the transient driver of the connected EqualRadius component. Equality remains
symmetric and retains its persisted constraint and corner identities. Intrinsic
corner annotations follow the shared editable parameter; independent driving
R/D constraints and read-only circular supports remain enforced. Requests that
cannot retain the requested radius are rejected transactionally.

Zero follows the same solve and profile-validation path as positive radii.
Native corner treatments remain as zero-radius definitions, so all related
corners become sharp and can subsequently be restored without losing equality.
An ordinary native circle cannot become a zero-radius corner treatment.

The native matrix has 640 table entries: star/chain equality graphs, both
insertion directions, all four edited corners, free/fixed authored vertices,
no intrinsic annotation, an annotation on the edited or another corner, native
R/D dimensions, and their locked/unlocked states. Annotation-only rows repeat
the lock flag without introducing a distinct lock behavior. Every row exercises
R 5, 0, 2, 0, 4; save/serialized reopen, stable supports and constraints,
independent circle equations, reference R/D readings, an impossible R 30 edit,
and a subsequent authored-point drag or atomic fixed-point rejection.

Additional cases cover both equality insertion orders with a free native circle
or read-only external circle, zero-circle rejection, and rejection against
another member's independent driving R/D dimension. The existing full Sketcher
suite also runs its trim, external-support, dimension and repeated-drag matrices.
The zero-group extension is specific to native corner treatments; it does not
make zero-radius external or ordinary circular geometry valid.

GUI acceptance covers the shared picker, each of the four tangent handles,
successive radius changes, dependent-corner collapse, reactivation through the
two-support sharp-corner gesture, native save/reopen and Undo/Redo. The separate
existing four-corner inline-dimension scenario remains part of acceptance.
Mouse target values allow 1e-3 mm for the floating-point screen/ray projection;
the connected radii and independently checked circle equations retain 1e-7 mm
checks against the actual native radius. The collapse gesture crosses the sharp
vertex rather than relying on a cursor landing at mathematical zero.

## Surface alpha and edge visibility

The supplied skeleton uses `#4D000000` surface color. Previously, every
nonopaque surface was omitted from the depth-only pass, exposing rear edges
in HiddenEdges and NoHiddenEdges. ShadedWithEdges also lacked a separate edge
depth pass after blending its transparent fill.

Depth-only rendering now includes surfaces independently of fill alpha.
ShadedWithEdges blends its fill first and establishes the edge depth afterward.
Shaded fill transparency, including zero-alpha fill, retains its existing
back-to-front blending behavior. Ordinary selection still uses its established
wire-through-occlusion feedback.

Framebuffer tests cover opaque, alpha-77 and alpha-zero surfaces in Wire,
HiddenEdges, NoHiddenEdges and ShadedWithEdges. They require identical edge-only
images across alpha values, a hidden rear edge and a retained foreground edge,
and unchanged visible-edge counts over transparent shaded fill. Existing tests
also verify fill ordering and zero-alpha fill over opaque geometry. The supplied
Part is loaded read-only through the native reader with its cached triangles;
NoHiddenEdges is identical for transparent/opaque fill and differs from Wire.

The last Shell in that Part independently reports
`Shell requires exactly one connected solid`. Its 5 mm thickness and four
opening references do not bypass the current one-connected-solid restriction.
This was the initial diagnosis. The subsequent multi-solid Shell correction is
recorded in [the Shell/reference follow-up](20261023-shell-reference-and-view-followup.md);
the protected original user model is not overwritten by verification.

## TOP and dimension text

TOP previously retained a 180-degree Z rotation. It now views from positive Z
with X right and Y up. Tests intersect actual camera rays with the XY plane in
orthographic and perspective projection and verify the axis directions and
absence of cross-axis movement. Other standard-view definitions are unchanged.

Exactly vertical dimension text already used bottom-to-top reading. A nearly
vertical projected direction could reverse it on one side of the numerical
tie. The shared presentation now treats horizontal direction components up to
1e-4 as vertical for reading direction only. Linear, radius and diameter tests
cover both measurement orders and projected drift -1e-5, 0 and +1e-5. This does
not rotate geometry, change dimension values or alter document coordinates.

## Evidence and limits

All listed acceptance checks passed on Windows. The mouse scenario took
9.370 seconds and the inline-dimension scenario 10.690 seconds. Both retained
the protected user/configuration hashes. The three focused suites passed in
34.17 seconds, the additional full native support matrix passed, and the
five-language catalog check passed in 15.69 seconds. The normal development
EXE was linked successfully after the user closed the running application.

- `build/sketch-view-regression.log`: appearance, full Sketcher and dimension
  presentation/camera suites.
- `build/equal-corner-final-native.log`: additional circular support and driving
  dimension rejection cases.
- `build/equal-corner-followup-gui.log`: mouse and inline-dimension scenarios.
- `build/sketch-view-localization.log`: all five supported language catalogs.
- `build/cpp-windows-release/supplied-part-visible-edges.png`: native supplied
  Part with corrected NoHiddenEdges rendering.

No product UI text is added or changed; existing translation keys are reused.
These are finite Windows checks. Native Linux execution and a complete nested
Assembly/alternate-plane GUI matrix remain unverified on this host.
