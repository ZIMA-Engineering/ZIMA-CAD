# Loft parent Origin correction — 2026-10-24

The initial Loft editor allowed Default Origin to fall back to the active Body
because its owned draft was not an independently stored Body-level feature.
New owned profile and guide Sketches now attach to the complete Loft Origin.
The command-local Default Origin action initializes the existing shared reference
rows from the same parent datum, replacing existing references when explicitly
requested. The shared placement solver, reference assignment, global picker and
other feature defaults are unchanged.

Owned Sketch position corrections remain Loft-local. Geometry, wire preview and
Sketch dimension annotations apply the Loft frame before the Body/occurrence
frame. The annotation path previously used raw embedded Sketch dimensions in
Body coordinates; it now consumes the existing parent-frame display adapter for
Loft. Add/Subtract controls are placed below both Sketch lists.

## Finite verification

The focused Windows matrix covers the complete parent-Origin defaults, explicit
replacement of complete Body reference rows, local corrections, translated and
90-degree-rotated Loft/Body frames, independent physical volume/centroid checks,
missing-anchor snapshots and restoration, native save/reopen and Undo/Redo.
Expected transformed centroid values include (89, -11, 13), followed by Y = -12
and -13 after successive local corrections.

Owned placement/profile-offset and native Sketch dimensions are registered even
at zero. Tests cover stable identity, repeated radius edits, placement and offset
assignment, Relations evaluation, Family variant assignment, locked rejection and
save/reopen. GUI checks cover nonzero child placement annotations, native Sketch
annotation placement, ordinary wire-only single selection, double-click parameter
inspection, editable values, rollback, Cancel, no-op OK and middle confirmation.
Output type/Thin details precede Add/Subtract in Loft, 2D Sweep and 3D Sweep.

New and renamed UI text is translated into cs/en/de/fr/ru. The localization
coverage/catalog contract checks identical keys, nonempty values and formatting
placeholders; affected Loft dialogs are opened in all five languages.
[Numeric helper labels and CLI names](../PARAMETER_VALUE_LABELS.md) document the
additional presentation and public spelling changes.

The broad UI base-colour assertion, duplicate-target CLI error-code assertion and
value-lock command query assertion and CLI material/Relations regeneration
assertion also fail on the pre-change/OCCT 8.0.0
baseline. They remain tracked limitations, not passing acceptance evidence.
The finite tests do not establish exhaustive Sketcher dimension/drag coverage,
arbitrary spline/rotation networks, nested Assembly integration or native Linux
execution. The original [Loft verification limits](20261024-loft.md) still apply.
The required native document schema and committed factory assets are unchanged.

Machine-readable results are recorded in [the companion matrix](20261024-loft-parent-origin.json).

Final local results: 11 focused tests pass; the additional Relations/Family
Loft case and GUI inline placement edit pass after their fixture corrections.
General Surface, relation templates, new-document templates, Pattern helper
inspection and feature direction/rotation GUI regressions pass. The broad CLI
process reaches the same material/Relations regeneration failure as baseline.

The additional Helical Sweep GUI run exposed a candidate-only helper-label
placement defect: geometric witness data caused the existing default envelope
layout to replace the value label anchor. Pitch and Pattern values now reuse
the existing zero-witness scalar-label contract. The Pattern native test checks
anchor preservation through that same layout, and the Helical GUI checks both
the visible start marker and the agreed numeric pitch label. No shared layout,
placement solver or geometry algorithm is changed by this correction.

The corrected 2403 local run passes all four repeated native/localization/CLI
checks and the Loft, Pattern and complete Helical Sweep GUI gates. The older
full-turn pitch witness assertion was replaced with the approved label-only
numeric/units/identity contract; the visible-marker assertion remains intact.
