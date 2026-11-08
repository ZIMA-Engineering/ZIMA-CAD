# Fillet selection and automatic profile-frame verification

## Scope

This correction covers three demonstrated defects without changing the native
document format or the placement solver equations:

- a selected Fillet or Chamfer could lose its complete authored azure wire when
  later Boolean topology no longer carried every original route contribution;
- automatic tangent selection could cross a visible material break when the two
  tangent edges had equal opening angles but different supporting face pairs;
- a fresh or replacement first planar support for an automatic profile could
  retain the generic row-zero FRONT role, mapping the profile to local XZ instead
  of the established TOP/local-XY default.

The user explicitly approved the narrow shared-placement change on 2026-11-08.
Existing persisted FRONT/XZ frames are not migrated merely by loading or editing
the document. An explicit replacement pick is a new authoring action and uses
TOP/XY. Manual work planes and complete-Origin entry retain their established
paths.

## Exact user-model result

The read-only reproduction used
`C:/Users/vladi/Prace/ZIMA-Engineering/03-CAD/01-ACTIVE_PROJECTS/ZE-CAD/10.prtz`.
The source file was never overwritten.

- Every Fillet and Chamfer returns a nonempty authored wire. Fillet 05 returns
  one route edge; Fillet 07 resolves five current fragments from three persisted
  routes.
- The demonstrated tangent chain now stops at its three authored members rather
  than offering two same-angle segments on a rotated material wedge.
- The history boundary immediately before Fillet 07 contains 125 vertices,
  136 triangles and 90 edges. Opening its Properties keeps that rollback Body
  visible; Cancel preserves the original revision.
- Actual rounded-surface clicks select the matching Tree feature without face
  fill, double-click shows its radius, and both Fillet 05 and Fillet 07 publish
  a nonempty GPU source-wire batch.

## Regression evidence

The Windows Release build completed and the following focused checks passed:

- tangent-route traversal, including a same-angle wedge whose material sides
  rotate 90 degrees;
- 48 automatic first-face cases across XY/XZ/YZ supports, both sides, quarter
  turns, offsets, calculated volume and native reopen;
- 12 thin rectangle variants with 36 two-edge Fillets, 12 thin Revolutions and
  72 rounded thin-profile cases;
- edge-treatment commands, radii, section distances, volumes, locks, rollback,
  reference ownership and native files;
- treatment fragments, calculation methods, ordinary and symmetric treatment
  lifecycle, direct segment selection and the complete work-plane GUI contract;
- Czech, English, German, French and Russian catalog/source coverage.

No user-visible string changed. Shared C++ remains common to Windows and Linux;
native Linux compilation and execution are a separate host gate.
