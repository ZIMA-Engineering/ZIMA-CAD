# Geometry side and direction contract

The side from which geometry is referenced or contacted is part of the public
modeling behavior. Internal representations may change, but must preserve this
distinction, including at zero offset. This requirement was reaffirmed on
2026-09-19.

## Current mechanisms

- Placement references carry orientation and side choices, including `flip`,
  FRONT/TOP orientation and `orientation_back`. Consume the shared placement
  contract; do not replace these choices with an unsigned distance.
- Bend endpoint attachment in `cpp/modules/document_core/src/bend.cpp` creates
  opposite signed zero offsets with `std::copysign`. It derives the dimension's
  `solution_side` with `std::signbit`, and reverses the offset when endpoint
  ownership changes. Here `+0.0` and `-0.0` are intentionally distinct states.
- Ordinary projected X/Y dimensions retain a signed coordinate difference.
  Growing a zero distance must use its retained sign to choose the correct
  side. `ExpressionDoubleSpinBox` explicitly preserves authored `-0` during
  focus-out and Enter handling because Qt's default formatter displays it as
  `0` before the properties transaction reads the value.
- Attached Twisted Sheet stores `attachment_material_side` independently of
  its selected endpoint and twist direction. It is derived from the persisted
  joining-face triangles and boundary direction, so the complete starting
  section occupies the parent's thickness face on either side of the edge.
  Preview, calculated loft and unfolded material frame consume the same side.
- Assembly `ComponentPlacementReference` stores a signed `offset` and a
  separate `flip` choice. For plane coincidence, `flip` controls parallel or
  opposite normals, including at zero separation. The offset controls signed
  separation along the target normal. Changing only `+0.0` to `-0.0` does not
  flip a component; the explicit side choice remains authoritative. Native
  persistence must preserve both fields, including the numeric zero sign.
- Extrusion boundary clipping in
  `cpp/modules/kernel_occt/src/occt_kernel.cpp` uses the dot product of the
  outward direction and boundary normal to select the retained half-space.
  Thread end clipping also uses the normal/axis dot product. Reversing these
  directions can change the resulting solid.
- Mirror planes and Pattern axes/directions belong to the operation's own
  placed Origin. Pattern direction and angle signs determine where copies are
  created. A plane normal and its opposite describe the same reflection plane,
  but that symmetry must not be generalized to contact or clipping operations.

## Change and verification requirements

Do not globally canonicalize signed zero, take absolute values of signed
offsets, discard oriented normals, or merge opposite-side states in caches.
Numeric equality alone does not establish equal modeling intent.

Any cache normalization must be limited to fields proven to represent the same
rigid transform, without rewriting stored values. Authored dimensions, side
flags, clipping normals and directed parameters retain their distinctions.

When changing the representation, verify both sides, zero offset, reversed
direction, save/reopen, editing and regeneration. Check the geometric result
as well as the stored side choice. Never describe untested cases as verified.

Regression coverage: `zima_cpp_sketch_dimension_command_tests`,
`zima_cpp_bend_command_tests`, `zima_cpp_assembly_contract_tests`,
`zima_cpp_derived_copy_contract_tests` and
`zima_cpp_sketch_dimension_entry_ui_contract`. The Assembly matrix covers
both Flip states with `+0.0`, `-0.0`, `+2.5` and `-2.5` offsets, native file
round-trips, explicit solving and Undo/Redo. Dimension tests also verify the
physical direction after growing a saved zero dimension.
