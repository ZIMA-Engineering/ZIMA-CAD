# Default profile for an attached sheet Flat

Selecting a supported sheet-end boundary while creating a new Flat prepares
a rectangular profile. Its two bottom corners consume the existing external
endpoint references published by ordinary sheet attachment. Its width follows
the source edge; a driving vertical dimension starts at 50 mm. The two vertical
sides and horizontal top retain a rectangle without a redundant horizontal
constraint between the already referenced bottom endpoints.

The initializer operates only on an empty, resolved draft. It does not replace
an existing authored profile or run again after the user deletes the prepared
geometry during the same creation session. The pending sketch remains transient
until OK. Cancel, ordinary placement solving, source-side identity and native
serialization retain their existing paths. No OCCT calculation is introduced
by initialization or preview and no document format changes are required.

`workspace::initialize_attached_flat_profile` consumes resolved endpoint data;
it does not modify the shared placement solver or sheet-attachment implementation.
The UI publishes its next ordinary preview with the prepared sketch instead of
publishing an intermediate empty profile.

The native regression matrix covers both endpoint orders, negative/zero/positive
local origins, repeated height edits, locked/unlocked drivers, reference height,
underconstrained dragging, length and vertical dimensions in both point orders,
serialization, real Bend attachments at 35/90/180 degrees, both material
orientations, two thicknesses and reversed source-edge parameterization. The
document-history test additionally exercises source-width changes, Undo/Redo
and native save/reopen. GUI checks use the actual offered boundary, reference
confirmation, pending sketch, Sketcher entry/return and OK.

The Bend dialog title is now "Bend Properties" for creation and editing, with
the same translation key provided in cs/en/de/fr/ru. The profile initializer
introduces no new UI strings. Verification results and remaining gaps must be
recorded before release; the matrix description alone is not a passing result.

## Windows verification, 2026-10-19

- `build/flat-default-profile-regression-contacts.log`: complete native Flat
  suite passed, including independent endpoint-contact equations, the new
  six local-origin/endpoint-order cases and 24 real Bend orientation/thickness/
  parameter-direction combinations. Repeated driver edits, reference-height
  dragging, underconstrained dragging and both length/vertical point orders
  passed. A locked reference dimension is intentionally rejected by the existing
  model contract; the test verifies that rejection rather than treating it as
  another valid solver state.
- `build/flat-default-profile-gui-complete.log`: actual boundary hover/cycling/
  confirmation prepared the rectangle; three actual double-click height edits,
  Undo/Redo, Sketcher entry/return, OK, native persistence, cancellation of a
  second seeded draft and editing the existing 12 mm profile passed.
- `build/flat-bend-title-localization.log`: translation/catalog contract passed
  for the five supported languages. The native GUI test checked the active
  Czech Bend title; the title was not independently inspected in every language.

An older volume assertion read an intentionally pending analysis slot as zero
after changing a profile to YZ. Independent fresh calculation returned the
correct 4586.371700 mm3. The test now explicitly calls `ensure_body_properties`
on a copied result when asserting a scalar, including save/reopen and Family
variants. This preserves the production demand-driven analysis contract and
does not introduce a geometry recalculation or modify the document.

GUI point dragging, profile trimming and arbitrary changes to the prepared
rectangle into another contour have not received a dedicated new GUI matrix.
The initializer does not change those existing Sketcher implementations.
Linux execution remains unverified on this Windows host. These finite checks
must not be reported as exhaustive Sketcher coverage.
