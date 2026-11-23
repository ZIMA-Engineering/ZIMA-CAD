# Trim boundaries on coincident circular supports

## Report and reproduction

The user's `ZE-CAD/50.prtz` has native circular geometry coincident with external
references. Center references and equal-radius relations preserve those copies.
The connecting arcs own tangent contacts on the external references. Trim did
not transfer those exact contact boundaries to the editable native copies.

The investigated input was copied before inspection. Its SHA-256 is
`d195ca908e622d03a1f515d62622a4f79619cda334ee1376e1019d0223970345`.
The original is not modified or committed. Private evidence is under
`build/trim-contact-20261123`, including the verified input copy.

At baseline, with base axes excluded to isolate contact detection, the upper
circle and lower native arc each had one Trim interval. The corrected topology
has two intervals on each, separated at the persisted contacts. Five selectable
pieces on coincident circular supports were independently trimmed, solved and
saved/reopened. This is a Sketch-definition check; it does not claim that every
unfinished profile in the user document is a valid solid extrusion.

## Change

Trim consumes persisted contacts on a coincident circular support, using the
existing native support-equivalence query. A contact outside the finite native
arc is excluded. It retains the real contact point and the original owners of
its constraints; it does not insert another point, C or reference dependency.

Persisted tangency also takes precedence over numerical intersection refinement
when one circular support is a coincident copy. Otherwise, a mirrored internal
contact can produce an extra microscopic interval next to the real contact and
prevent subsequent radius editing. No global intersection tolerance, display
sampling, placement solver or native serialization contract is changed.

## Windows verification

The new native matrix covers both selection orders, two orientations, mirrored
geometry and all four internal/external contact combinations. Each resulting
trimmed sketch exercises radius/diameter, center distance/X/Y and three-point
angle dimensions, driving/reference and locked/unlocked states, repeated numeric
changes and repeated point drags. Independent incidence, tangency, contact side,
radius and dimension equations are checked after edits. Fully constrained drags
must fail without changing the sketch. External sources remain read-only.
Undo/Redo and native save/reopen preserve the contact identities.

- 32 combinations passed, with 1,920 numeric edits and 2,688 moving drags.
- Eight additional native/external, suppressed/active and inside/outside finite
  arc-domain cases passed.
- The existing Trim fragment matrix passed 96 circular, 144 parallel and 16
  point-H/V variants, including 300 numeric edits and 378 moving drags.
- The original missing-boundary assertion failed before the fix.
- The internal mirrored cases initially exposed microscopic intervals; all eight
  failures were corrected and retained in the passing matrix.

The automated Windows GUI check passed three coincident circular outlines from
the private input and two generated native/reference contact fixtures. It
exercises common hover/click/RMB selection, transient Trim, Cancel, middle-button
double-click confirmation, Undo/Redo and native reopen. A short middle click is
explicitly checked not to commit. Generated fixtures also pass six actual inline
R/D edits and 18 moving point drags after Trim, with independent contact, endpoint
and read-only-source checks. Native tests cover the broader dimensional and
orientation matrix above; GUI execution of every Cartesian combination is not
claimed. The source project retained its original SHA-256 after the checks.

The GUI harness originally left a dragged point selected before trying RMB
candidate cycling, which opened its context menu. Clearing the previous
selection, as in the established shared GUI picker tests, corrected the harness.
This was not a geometry solver stall. The test also explicitly checks that the
Trim definition has changed after confirmation, rather than accepting preview
counts alone as commit evidence.

The initial Trim status incorrectly described a single middle click. It now
describes the existing double-click contract in all five catalogs. Catalog/UI
translation verification passed. Linux execution is pending.
