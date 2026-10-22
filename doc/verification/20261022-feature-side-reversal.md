# Feature direction reversal verification

Status: Windows implementation, focused native/GUI contracts and factory
regeneration pass; release acceptance is being completed. The persisted scope was
explicitly approved on 2026-10-22 in the
[implementation plan](../FEATURE_SIDE_REVERSAL_PLAN.md).

## Definition and calculation

`FeatureParameters::reversed_sides` reverses physical directions. It never moves
the two stored side definitions, targets, value locks or their logical indices.
Side 1 retains End ancestry and Side 2 retains Start ancestry. The optional
neutral value is omitted from serialization, preserving the existing neutral
definition and calculation fingerprints. This is one current implementation;
there is no alternate legacy modeling path.

The direction mapping is shared by native Extrusion/Revolution/Twist requests,
analytical command previews, parameter cues, profile grips and datum Axis
endpoints. Revolution normalization consumes the mapping during both ordinary
calculation and Solid-state preparation. Cap roles follow persisted logical
ancestry separately from profile winding. Thin Extrusion retains each logical
side's authored profile and wall offsets when reversing travel. Shared placement,
Sketch frames and Drawing coordinates are unchanged by the reversal parameter.

## Finite Windows matrix

The native `zima_cpp_feature_side_reversal` matrix passes:

- Fifteen result/wall/axis cases, each calculated in both physical directions:
  Solid, Thin and Surface; all three Thin modes; both Revolution axis orders.
  Check reflected spatial bounds, area/volume, solid centroid and exact logical
  far-cap equations. Surface results must not acquire filled caps. Analytical
  preview extents agree with calculated geometry.
- Twenty single-side/independent/symmetric Extrusion and draft cases, including
  unequal stored settings and retained independent settings across symmetry.
- Eight external captured Plane Up To cases across direction, operation and
  symmetry, with immutable source definitions and native definition readback.
  These synthetic external-plane requests have no real owning Assembly; their
  context-free Part cache is deliberately not used as a persistence oracle.
- Eight Twist direction/handedness/smoothing cases, mixed Extrusion/Revolution,
  a Through All cut with independently checked removed volume and centroid,
  native Point Up To, datum Axis endpoints and full Revolution.
- Three successive direction edits for ordinary solid Extrusion and Revolution,
  native definition/cache readback, fresh calculation and Undo/Redo. Logical
  cap identities and panel locks survive each action.

The actual workspace GUI passes repeated button clicks, fixed first-panel
values/locks/references, a second-panel value edit after reversal, expected
preview bounds, unchanged document/base-scene revision during draft switching,
Cancel, OK, Save, Undo/Redo and native reopening. This synthetic fixture has
an unrotated Part Body; it does not claim every occurrence transform or every
reference-entry mouse interaction.

The five-language translation/dialog contract passes in Czech, English, German,
French and Russian. No new product-facing strings were introduced by reversal.
The existing translated Swap sides action is reused. Ordinary selection and
inspection tests pass without document mutation or extra preview uploads.

Dependent native checks pass for profile commands, Assembly profile commands,
Feature parameters, centerlines, draft, document transactions, work planes and
FORM geometry. The existing curved-limit regression oracle was corrected to
complete deferred physical analysis on a private snapshot and inspect the real
Body archives of a compound. Its exact BRep, cap-on-surface, source-edit and
symmetric-limit checks now pass. The Assembly persistence fixture now copies its
expected definition before Save, avoiding a reference invalidated by session
publication. Neither correction changes product behavior.

Evidence: `build/feature-side-reversal-matrix-final.log` (the direction matrix
passes; the separate initial curved-limit oracle failure is superseded by
`build/feature-side-reversal-rotation-final.log`),
`build/feature-side-reversal-gui-results.log`,
`build/feature-side-reversal-dependent-contracts.log`,
`build/feature-side-reversal-dependent-native-final.log` (four passing cases;
the initial Assembly fixture failure is superseded by
`build/feature-side-reversal-assembly-final.log`).

## Factory and platform boundary

Factory conversion uses explicit native Regenerate, including Relations and
physical analysis, followed by save and exact native definition/cache readback.
Authored history, sides, Body ownership and Sketch data are checked independently.
Bend auxiliary Sketch JSON is compared with canonical whitespace while retaining
its fields and signed floating-point values. User projects and private library
edits are excluded; accepted Git factory definitions are verified separately.

All 1,046 Part-based factory definitions pass explicit regeneration and exact
native readback. A separate comparison of every cached boundary preserves all
reference identities and all available numeric volume/area values; intermediate
deferred physical-analysis state is not mistaken for measured zero. Six start
templates are rewritten and reopened. The actual five-language new-document GUI
passes Part/Skeleton/Assembly creation, active editable contexts and normal
command availability (62.54 s).

The final factory GUI run passes thirteen dependent contracts (196.59 s),
including all four FORM previews, the formed-nose washer, title block, symbol,
frame/template, FORM Sweep Sketch, Sketch entry camera and Sheet/Solid states.
The broader direct Properties/selection oracle passes all 21 library cases
after regeneration (120.11 s). Timings are host observations. A first strict
history comparison found compact versus formatted JSON in two auxiliary Bend
Sketches of `NTW-M08-REFERENCE`; their parsed data are identical. No authored
geometry or side choice was changed to make that comparison pass.

The additional Assembly reversal fixture passes unequal two-sided cuts, exact
opposite centroid equations, stable side definitions/locks, `.asmz` readback,
Undo/Redo and explicit regeneration while the source Part remains unchanged.
It caught the command-patch validator rejecting the optional neutral-by-default
field; the shared Part/Assembly validator now explicitly accepts `reversed_sides`.
The final Part/Assembly command and GUI run passes all three checks, including
unchanged OK on reopened reversed Properties (14.76 s). The broader final native
run passes GPU source-wire, viewer, full Sketcher, rolled-L, intersection and trim
checks; its initial Assembly command rejection is superseded by that focused
passing rerun. Evidence: `build/feature-side-final-native-regressions.log` and
`build/feature-side-final-command-contracts.log`.

Evidence: `build/feature-side-factory-regeneration.json`,
`build/feature-side-factory-geometry-identity-audit.json`,
`build/feature-side-factory-converted-hashes.json`,
`build/feature-side-factory-promoted.json`,
`build/feature-side-regenerated-start-template-gui.log`,
`build/feature-side-final-factory-gui-contracts.log` and
`build/library-preview-oracles-regenerated/report.json`.

Packaged-asset equality and final release results remain publication gates. The earlier read-only
[origin/preview audit](20261022-factory-origin-preview-audit.md) is a prerequisite,
not a substitute for the final factory and release gates. Linux execution remains
unverified and no Linux release is claimed. Unsupported Twist combinations remain
rejected by the existing validation rules.
