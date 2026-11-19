# Sheet contact edges and Profile controls — 2026-11-19

Status: local native and hidden GUI acceptance passed; portable acceptance pending.

## Cause and bounded changes

The sheet attachment check treated the whole joining face as if it had to lie
on one side of the infinite attachment-edge line. Boolean operations can join
coplanar thickness faces belonging to several branches. Remote portions of that
valid face can cross the line without changing the material side at the selected
edge. The check now uses the persisted planar face triangles incident to the
interior of that exact edge. It does not query OCCT, change reference identity,
disable Boolean union or change the shared placement solver.

Flat had a separate explicit Side A restriction. Interactive attachment now
accepts either skin at a valid sheet end face. Its existing local material-side
calculation places the thickness inward. Automatic Sheet from Body authoring
retains its previous outer-skin choice; this change does not silently choose a
different reference for converted walls.

Ordinary Sheet Metal selection gives converted Flats and Bends priority over
their original Solid/Fillet aliases. The original features remain in the same
candidate list for right-click cycling. Modeling keeps its existing ordering.
Selection uses cached viewer data and the common picker. A workspace change
preserves an already confirmed View/Tree selection.
Source alias publication also copies its input face before appending aliases,
so vector reallocation cannot invalidate that face while several aliases are
being appended.

Profile places its work-plane row below placement degrees of freedom. When
attached, XY/XZ/YZ and View remain visible but disabled; Sketch stays enabled in
the same row. Hiding the free-width controls also hides their scroll viewport
and schedules the existing dialog size adjustment. No native format changes.

## Saved user cases

Read-only originals were copied into the ignored verification directory; no
user document or protected Toyota project was overwritten.

- Model 18: the previous check accepted 10 of 24 straight boundary edges. The
  local-face check accepts all 24, including both skins of the merged branches.
- Model 17: three cold calculation/save/reopen cycles retain valid references
  and volume 205431.61542201793 mm³.
- Model 18: three cold calculation/save/reopen cycles retain valid references;
  volume 30258.011961087552 mm³ versus saved 30258.011961140612 mm³.

## Verification boundaries

The implementation is shared C++; native execution here is Windows with
OCCT 8.0.1 and Qt 6.11. Linux execution is not claimed. Tests use hidden Qt
windows and native events, without capturing or controlling the user's desktop.
An active Part inside an Assembly consumes the same ordinary selection priority;
its occurrence-specific GUI scenario has not yet been repeated for this change.

The new Flat validation text is supplied in cs/en/de/fr/ru. Other changed controls
reuse existing translation keys. Catalog validation is recorded below.

## Native acceptance

- Flat command suite passed: 72 cut attachment cases (two fragments on each
  skin, first/last/midpoint anchors, Flat/Bend/Profile, two Body placements),
  including identity, regeneration, save/reopen and Undo/Redo checks.
- A merged branch face fixture passed both skins for all three attachment
  kinds. Opposite-skin Flat continuations have the same volume. Successive
  Bend endpoint changes of +3, -2 and 0 preserve the dependent Flat, including
  regeneration and Undo/Redo.
- Existing rectangular Flat continuations, angle/flip/thickness/parameter
  reversal cases and midpoint dimension edits/drags passed. Outer-only fixture
  selection is explicit where a test specifically checks Side A behavior.
- Bend, Sheet Profile, Sheet exchange (including Sheet from Body) and Model
  calculation suites passed against the changed native libraries.
- Translation coverage and affected UI catalog checks passed in all five
  supported languages. No untranslated new validation text remains.

## Hidden GUI acceptance

- Converted Flat and Bend: three successive hover, right-click source cycling,
  left-click selection/Tree synchronization and Modeling/Sheet Metal mode cycles
  passed. Document state and base scene revision stayed unchanged. The test
  waits for camera animation and inspects the narrow Bend at close-up scale;
  whole-model sampling can legitimately hit nearby authored Sketch wires.
- Profile: work-plane row follows the degrees-of-freedom row; attached plane
  presets and View remain visible and disabled, with Sketch enabled in the same
  row. The dialog shrinks without moving when width controls disappear. Existing
  attachment, dimension, Tree, Explode, Undo/Redo and Sketch-opening checks pass.
- Saved model 17: 13 valid attached Tree rows on opening, five Properties/Cancel
  checks, explicit regeneration and save/reopen pass.
- Cut GUI: all 12 fragment/feature/endpoint-or-midpoint scenarios pass creation,
  Properties/Sketch editing, Cancel and cold regeneration.

Initial test fixture assumptions that only Side A was eligible were corrected
to explicitly select Side A where intended, and to cover both skins elsewhere.
No failing geometry scenario was removed from the matrix. GUI sample discovery
was corrected to wait for animated camera changes and use close-up face picking;
the actual hover/click assertions were retained.
