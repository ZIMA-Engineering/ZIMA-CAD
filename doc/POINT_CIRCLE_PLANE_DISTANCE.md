# Point on a circle: parallel plane distance

For a Point Feature whose first positional reference is an exact full circle,
a subsequent plane parallel to the circle plane cannot locate the point along
the circle. Its displayed distance is therefore derived from the circle centre
and the plane's oriented normal. The reference remains available for orientation.

The existing persisted `offset` remains the authored value. Solving uses a
temporary copy containing the derived distance; no new file-format field or
external state is needed. Tilting the plane restores the authored constraint.
An impossible restored constraint remains invalid and disables dialog OK.
An already selected intersection branch is also retained, but temporarily
excluded from solving while the parallel planes leave the circle position free.

The policy is explicitly enabled only for Point Features. Ordinary placement,
other Feature types, ellipses, partial arcs, splines and cylinders retain their
existing contracts. Circle recognition reuses the exact rational-conic validation
used by solution branches, with additional exact validation of native body circles represented by three
rational 120-degree arcs. Parallelism uses the geometric solver tolerance,
not a broad visual angle threshold. Signed zero, reference identities, authored
locks and orientation-side flags are retained.

The shared reference field displays a read-only derived value and a translated
explanation. Reference replacement, inspection and removal keep their existing
semantics. Inline dimension edits and placement commands use the same Point-only
policy so they cannot modify the derived distance indirectly.

Regression coverage is in `curve_placement_contract_tests.cpp`,
`feature_ui_verification.inc` and `translation_contract.cpp`. The optional
`ZIMA_VERIFY_INVALID_PLACEMENT_SOURCE` test accepts the original conflicting
POKUS fixture and checks commit, Undo/Redo and native save/reopen without writing
to that source file.
