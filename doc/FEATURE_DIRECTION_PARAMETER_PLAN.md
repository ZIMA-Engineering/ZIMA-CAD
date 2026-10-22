# Numeric Feature direction

The user approved a numeric +1/-1 direction parameter, negative manual length
and Revolution angle entry, and crossing the starting position with a purple handle. This extends the
already approved independent side-reversal contract; it does not swap panels.

## Implementation

- Reuse `FeatureParameters::reversed_sides`; add no native geometry field.
- Expose `parameter:direction` through the existing dimension identifier registry,
  Family and Relations. It is dimensionless in mm/in and degree/radian documents.
  Existing allocated identifiers remain unchanged; the registry appends the new
  slot using its ordinary synchronization path.
- Assign +1/-1 deterministically. Repeated Regenerate must not toggle the sign.
  Relations retain their existing lifecycle: storing text changes metadata;
  explicit Regenerate evaluates and commits geometry.
- Keep each side's positive magnitude, references and locks in its original panel.
  A negative manual length or Revolution angle reverses the existing state once and stores magnitude.
  Dragging derives direction from the gesture's baseline, never its latest state.
- Reuse the scalar Unit spin box, numeric lock control and parameter annotation.
  Reuse the shared localized Direction label. Translate the one new validation
  error into cs/en/de/fr/ru.
- Respect the existing native positive-length range and precision. The UI retains
  its established magnitude clamps. Do not normalize signed zero globally.

## Verification and factory data

The native command regression covers scalar units, both directions, nine explicit
regenerations, stable panels, locks, invalid values, Undo/Redo, native reopen and
cold geometry. Its first corrected run passes in
`build/feature-direction-native-recheck.log` (0.27 s). The test deliberately
preserves metadata-only Relation storage before explicit regeneration.

The linear Feature GUI regression passes, including actual mouse crossing and
return, scalar and negative View dimension edits, locks, stable panels, repeated
controller updates, no base-scene publication, Cancel, unchanged OK, Undo/Redo
and native reopen (`feature-direction-inline-gui-tests.log`, 5.82 s). Five related
placement/selection/native checks also pass in
`feature-direction-gui-regressions.log`. Localization and unit-entry GUI checks
pass in `feature-direction-localization-units.log`.

The subsequent Revolution extension reuses the gesture baseline. A numeric
angle grip stops at 360 minus the opposite numeric Revolution angle, or 180
for symmetric rotation. It does not push or disable the other panel, including
when that panel is locked. Mixed Extrusion/Revolution retains its full angular
range. An opposite Full rotation blocks this grip. UpTo targets retain their
authored definition and existing exact span validation on calculation; no
approximate collision calculation is added to pointer movement. This is angular
span control inside one Feature, not general collision avoidance between
independent solids. All eight actual mouse cases pass, including negative angle
fields and View dimension editors, both side indices, both axis orientations,
both initial directions, locks, independently checked sector volumes, Undo/Redo
and reopen (`feature-direction-final-factory-gui.log`, 31.01 s).
Signed-angle and drag-limit native checks pass
(`feature-direction-revolution-native-tests.log`, 0.51 s).

The expanded native direction regression also passes 32 exact end contacts and
64 angular gaps (`feature-direction-revolution-contact-precision-tests.log`,
7.61 s). Default 0.001-mm Boolean tolerance is used for 0.1-degree gaps; the
0.001-degree gap fixture requests 1e-7 mm because its radial clearance is smaller
than the default tolerance. Both cached volume and independent adaptive BRep
integration must meet the original 1e-5 volume check. Product precision is
unchanged. The initial sub-tolerance fixture was invalid as an exact-gap oracle;
it is recorded in `feature-direction-revolution-volume-independent.log`.
The expanded test also reacquires a history element after `set_body_history`
replaces the document, correcting a dangling reference in the old datum-axis test.

The isolated factory run completed all 1,046 Part-based assets without a failure,
checking authored definitions, existing dimension identifiers, calculated
volumes/areas and reference identities. Its two Assembly-template inventory rows
are deferred to the separate template refresh. Reports are in
`build/feature-direction-factory/verification.json`; working user files were not
overwritten. Verified outputs are installed, preserving the user's modified
pressed M10 wing-nut and ZE-TEXT working definitions byte-for-byte; their factory
versions are committed separately in `89c5d3d4d`; private working bytes are retained. Six start
templates were rewritten and reopened with the current native implementation.
New-document GUI checks pass (45.29 s), as do the four FORM and nose-washer
preview checks, template editing (12.84 s) and template-based Part/Assembly
creation/activation (2.48 s). Evidence is in `feature-direction-final-factory-gui.log`
and `feature-direction-final-template-selection-recheck.log`.
The first VentilationWindow inspection retry clicked a different overlapping
owner after selection was cleared; the test now repeats the existing RMB
candidate selection and passes (4.98 s), without changing product picking.

Final localization, unit-input and Feature-parameter checks pass
(`feature-direction-final-localization-units.log`, 39.14 s total). The root
`zima-cad.bat` runs this normal build; the user was invited to test it. No new
experimental runtime was created. Linux execution, general solid collision
avoidance and additional Assembly-specific numeric-direction mouse cases are
not claimed by this finite matrix.

Before release, rebuild the normal GUI/CLI and refresh affected committed native
factory assets with that implementation so their registries contain the new
parameter. Verify unchanged existing identifiers, authored geometry, volumes,
side/reference identities and normal insertion. Refresh corresponding start
templates and exercise creation from them in the GUI. Preserve user-authored
working files, including locally modified files in factory library paths; use
isolated committed-source copies where necessary. Do not reuse old conversion
reports or the obsolete reference-next executable as evidence for this change.

The user tested the normal build positively and subsequently accepted automated
verification of the arithmetic correction, authorizing release before further
feature work. Final committed-source portable acceptance remains required.

## Arithmetic input follow-up

The user's normal-build test exposed that the original unit spin box accepted
negative numbers but not arithmetic such as `70-100`. Feature side length and
Revolution-angle fields now consume the existing `UnitExpressionDoubleSpinBox`;
the general unit control and native parser are unchanged. The result follows
the same one-time reversal path as a directly entered negative number.

OK validates all editable side expressions before interpreting any of them.
This also handles middle-button confirmation while a text field retains focus.
Invalid expressions keep their authored text, do not commit and use the existing
five-language Invalid numeric expression message. Qt may append the field's
unit suffix; preserving the expression does not require dropping that suffix.

The follow-up matrix covers both panels, Extrusion/Revolution, implicit and
explicit units, comma decimals, parentheses, positive results, repeated no-op
interpretation, direction locks, invalid division and pending-expression OK.
Its requested combinations are mm/cm/m/in by degrees/radians (32 cases plus
locked cases). Actual workspace mouse/keyboard checks use `70-100` for length
and `70-105` for angle alongside the existing drag, persistence and Undo/Redo
matrix. All four final contracts pass in `feature-negative-expression-final-tests.log`:
translations 12.87 s, unit/expression matrix 30.43 s, selection 4.58 s and actual
Feature mouse/keyboard/Undo/Redo/persistence 29.95 s (77.84 s total). The initial
invalid-expression assertion compared raw text without Qt's retained unit suffix;
the corrected check verifies `cleanText()` while preserving rejection and no
commit. Native data, calculation precision and the general unit control are
unchanged. No new translation keys are needed; the existing validation message
is available in all five catalogs.
