# Command preview wire selection, 2026-10-22

The user clarified that selection must show the application's actual creation
preview wire, without extra calculated edges. The preceding experiment used
calculated original/reference topology, which is not that preview. Its cache
checks and equal hover/inspection counts did not prove the requested geometry.

The application preview provider now calls the same analytical generators used
by Feature, Extrusion and Revolution Properties. It also reuses the existing
Bend, Flat, Holes, Hole, Twisted Sheet, Thread, 2D Sweep, Helical Sweep and Sheet
Transition preview generators. Input bounds come from the feature's existing
calculated input boundary, without copying or recalculating the body. Body and
occurrence presentation transforms remain applied once. Generic viewer packets,
imports and other operations retain their established presentation paths.

The GPU preview cache still serves hover, click and parameter inspection. An
analytical preview is not combined with result-edge recoloring. Face picking
and current-fragment wire inspection for placement remain separate and unchanged.
No native format, shared placement solving or persisted geometry is changed.

The strengthened Feature selection GUI check opens the actual Properties dialog
after ordinary selection/inspection and compares every preview segment and
endpoint. It requires analytical `preview:` identities and rejects extra or
missing segments. Cancel must leave document state unchanged.

Windows verification passes on `C:/zcb/reference-next`:

- The synthetic Add/Cut GUI fixture and both private L30X30X3 snapshots pass
  the direct Properties-preview segment comparison for every Modeling Feature,
  ordinary View/Tree selection, double-click and unchanged Cancel/document state.
  Hover/click/inspection retain one GPU preview without another upload when
  switching from selection to inspection.
- Face wire inspection, Sheet states in Part/Assembly context and Solid state
  history/inspection contracts pass. The four-contract run completes in 49.90 s.
- No product strings changed; the five existing catalogs were reviewed as
  unaffected. The normal development launcher and native documents are unchanged
  by this display correction. Tests use private fixture files; subsequent user
  saves to the original project are outside those snapshots.

Evidence: `build/command-preview-wire-build.log`,
`build/command-preview-oracle-build.log`,
`build/command-preview-wire-final-build.log`,
`build/command-preview-final-contracts.log`,
`build/command-preview-L006-ui.log`,
`build/command-preview-L-current-ui.log`.

The user accepted this wire behavior on L30X30X3. The subsequent
[factory origin/preview audit](20261022-factory-origin-preview-audit.md) records
the required broader library checks and any failures before release.

Linux and exhaustive command-type coverage remain unverified. The separate user
report about Start/End after symmetric/independent side swapping is scoped in
the [side-reversal plan](../FEATURE_SIDE_REVERSAL_PLAN.md), explicitly approved
on 2026-10-22. Its [implementation verification](20261022-feature-side-reversal.md)
and release gates remain separate from the accepted display experiment and
completed factory prerequisite audit.
