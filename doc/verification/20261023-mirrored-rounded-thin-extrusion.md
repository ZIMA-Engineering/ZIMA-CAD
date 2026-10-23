# Mirrored rounded Thin extrusion

## Report and cause

The last `Vytažení003` in the supplied Toyota skeleton failed with
`OCCT profile face is invalid`. The read-only snapshot used for reproduction is
`build/skeleton-latest-review.prtz`, SHA-256
`71c9eed6d8f8026d87e41c1ab8cdda0ccaec984739d8bf85b4bd6bf846a0259c`.
It contains two disconnected rounded rectangular profiles, radius 6 mm,
one-sided thickness 3 mm and two original-plane extrusion limits.

Removing the Thin wall calculated both profiles and their plane limits.
Keeping Thin while replacing the limits with blind extrusion still failed.
Testing each profile separately isolated the mirrored contour: its offset
wire traversal did not agree with the automatically selected outer-face plane.
Blindly reversing the inner wire therefore failed to define a hole.

## Change and material-side follow-up

The existing shared planar profile-face builder now orients each hole's finite
boundary on the actual outer face's supporting plane, then reverses that boundary
before adding the hole. This changes transient OCCT boundary orientation only.
It preserves authored profile coordinates, thickness sides, source identities,
limit references, document serialization and the protected placement contract.
After the initial face-validity fix, the user observed that the mirrored contour
grew outward while the other contour grew inward. The cyan authored preview
already handled this correctly. Its closed-profile rule normalizes traversal to
the finite interior independently of authored winding. The kernel now follows
that same convention: the OCCT offset support bounds the finite interior,
polygon miter offsets account for closed winding, and closed spline offsets
retain the corresponding curve orientation. Open profiles retain their
directional side behavior. Circle offsets already used the finite-interior rule.
This preserves the existing meanings of One side, Other side and Symmetric;
it corrects the calculated material to match the existing preview.

Surface-result wires retain their existing path. No new user-visible text is
introduced by this kernel fix; the existing localized error is reused.

## Windows verification

- Twenty-four analytical rounded/straight rectangle cases cover mirrored/unmirrored traversal,
  both extrusion directions and one-side/other-side/symmetric thickness. The
  expected volumes are calculated independently from rounded-rectangle areas.
  Actual material bounds are checked independently as well, so a correct volume
  on an incorrect thickness side cannot pass.
- The full extrusion-limit suite passes, including inclined and original-face
  limits, cuts, circular Thin walls, zero-corner Surface/Thin/Solid cases,
  source identities, previews and cache invalidation.
- The supplied snapshot regenerates without errors. Saving a temporary test
  copy, reopening and cold regeneration preserve final volume, face identities
  and authored history. The source snapshot is not overwritten.
- The revolution-limit suite and five-language localization suite pass.

Evidence: `build/skeleton-extrusion-regression.log`,
`build/skeleton-extrusion-native-roundtrip.log` and
`build/thin-profile-dependent-regression.log`. The material-side follow-up is
covered by `build/thin-consistent-matrix.log` and
`build/thin-consistent-skeleton.log`; the supplied model volume after that
correction is approximately 3.43492e6 mm3.

The same 24 winding/direction/thickness cases also pass as Thin Sweeps, with
independent analytical volume checks (`build/thin-consistent-dependent-native.log`).
The complete Sweep command suite passes (`build/sweep-dependent-complete.log`).
Its fixture now requests physical properties on a local result copy before
reading volume, respecting the existing lazy-analysis contract; the initial raw
zero was an unmeasured cache value, not a failed geometric calculation. Initial
localization/revolution runs also encountered sandbox temporary-directory write
restrictions; both passed when rerun with a writable isolated test directory.
Native Linux execution and GUI interaction with the supplied extrusion remain
unverified at this point.
