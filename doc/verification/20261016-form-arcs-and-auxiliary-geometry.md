# FORM arc constraints and auxiliary geometry

Windows C++ development verification, 2026-10-16, OCCT 8.0.0.

## Scope and evidence

The investigation uses a disposable copy of the user's updated FORM-KOULE
document, containing two tangent native arcs in its XZ Sketch, an independent
vertical construction segment, and a point reference to the preceding XY
Sketch. The reference packet is persisted as broken with a cached point at
(15,0). The original file is not rewritten by these checks.
The snapshot and unchanged original have SHA-256
8DD3E77D43E8625D156014363E5C5CFB2D069B76547EE41D304B2B6ED2829693.

The initial native reproduction rejected the construction endpoint to the
axis-bound arc start in one selection order, rejected equal radii in both
orders, and rejected the feasible first-arc/X-axis tangent before solving.
The unconnected construction segment and the built-in axes have separate
identities; a curve in another Sketch cannot be merged as a native point of
this Sketch. An explicitly projected external point remains read-only.

## Changes

- Auxiliary Sketch wires use blue-violet (#8066C2). Construction axes retain
  brown (#AD6E2E), and hover/selection retain their existing azure feedback.
  Auxiliary geometry has no axis overhang, including when consumed by Drawing
  annotations. Exact reference samples and endpoints are unchanged.
- The endpoint-merge seed also recognizes native arc endpoints. It preserves
  the existing native point identity rule and places a free segment endpoint
  at its selected curve endpoint before verifying the complete constraints.
- Equal-radius creation falls back to the established simultaneous circular
  fit when sequential solving fails. A temporary radius equation preserves
  the selected reference radius; it is not persisted.
- Simultaneous fitting reads cached external point coordinates under the same
  rule as the ordinary PointReference equations. It never clears the broken
  flag, moves the external source or rewrites the reference packet.
- A native arc endpoint already on a base axis is reused as the tangent
  contact. If its axis incidence is missing, creation persists that C with T.
  The existing circular fitter handles the coupled native equations.
- Center dragging follows the anchored parallel coordinate of a circular
  base-axis tangent contact. This extends the existing coordinate-dependency
  walk rather than weakening constraints or rounding the mouse coordinates.
  It allows the free normal movement even when the cursor has a transverse
  component; fixed radius/contact cases still undergo the full solve.
  An anchored circular contact uses deformation fitting instead of attempting
  to translate the fixed endpoint along with the center.
- Trim topology records whether base axes participated. Newly cut native
  Segment/Arc endpoints on these axes retain explicit C. Trims explicitly
  excluding base axes do not acquire incidental axis constraints. This flag is
  transient operation data; the native document format is unchanged.

No new user-visible text or translation keys are introduced. Localization
review covers all five supported languages; labels, symbols and commands are
reused. Shared container placement, geometry side identities, document format
and dependency versions are unchanged.

## Mathematical rejection

The second arc in the supplied snapshot has both its center and its start
point constrained to the vertical base axis. A positive-radius circular arc
with that center cannot be tangent to that same axis. Those constraints must
remain authoritative; rejection is not repaired by removing or ignoring them.
Tangencies outside the retained arc domain and contacts contradicting the
read-only external point are likewise not claimed as valid operations.

## Verification matrix

The new native checks cover both orders of the actual endpoint C, feasible
first-arc/X-axis T and equal-radius relation; independent circle, tangent,
axis and shared-incidence equations; successive radius edits and drag
attempts; read-only external packets; and serialized reopen. A separate
eight-case orientation/fixed/free audit compares connected construction-axis
movement with the prior contact-first merge path and checks immutable base
axis origins through three successive drags.
An additional 64-case tangent-center matrix covers both base axes, both
contact sides, both selection orders, R/D, driving/reference and lock states.
It checks transverse cursor projection, repeated accepted free-center motion,
atomic locked-radius rejection, successive value edits, contact-side identity,
fixed native contacts and serialized reopen. Invalid locked-reference
dimensions are rejected before testing the supported reference state.

Sixteen ordinary circle axis-trim cases cover four quarters,
radius/diameter dimensions and locked/unlocked manipulation. Thirty-two
auxiliary/construction cases confirm their existing exclusion from trimming.
Dimensions are created on the retained arc after trimming. This is not a
claim that pre-trim dimension restoration or construction-role restoration
is implemented by this change.

All ten targeted native CTest suites passed: viewer,
serialization, Sketcher, keypoint contact, reference command, dimension command,
relation command, mirror/tangent, curve command and Sketch command contracts.
The actual FORM native C/T/equality checks pass both orders, three successive
radius value changes and three accepted center drags per case, independent
geometric equations, immutable external packets and serialized reopen. The
unconnected construction segment remains unchanged; after explicit endpoint C,
its start stays at Origin and its endpoint stays on the vertical base axis.

Six actual-document GUI cases pass native candidate hover/selection, C/T/equal
creation in both input orders, Undo/Redo, feature OK and native save/reopen.
Each case also performs three actual center drags, with Undo/Redo after each
drag, followed by independent saved arc/tangent/equality checks. Relation tools
are ended with Escape before testing ordinary point dragging.
An additional GUI circle mouse-trim check confirms both new endpoint axis C
equations and native save/reopen. The existing standalone trim GUI preview,
Cancel and commit regression also passes.

The dedicated auxiliary-color GUI check passed on light and dark backgrounds,
including exact finite extent and unchanged brown construction-axis color.
The five-language translation/catalog and boundary-text GUI checks passed
(Czech, English, German, French and Russian).
The broader interaction-color run stopped at its ordinary wire hover/selection
thickness assertion. The profile-axis display checks executed successfully,
but the full profile-centerline run later stopped at its prerequisite-state
restoration assertion. These broader runs are not counted as passes.

Linux verification is not performed on this Windows host. New GUI coverage
does not claim every dimension kind, contact side, external support variant
or trim gesture for every matrix combination. Previously recorded C-only
reference-dimension drag and K-only edit limitations remain separate from
the successfully verified operations.
