# Connected equal-radius component verification

Date: 2026-10-07. Shared C++ implementation, Windows native and Qt GUI tests.

## Defect and correction

Four rounded rectangle corners linked by three EqualRadius constraints failed
when a radius dimension drove a corner beyond the directly connected pair.
Pair-local reference selection repeatedly overwrote a propagated value without
recognizing the driver elsewhere in the connected component. The new native
regression reproduced `Corner radius conflicts with its equal-radius relation`
before the solver change.

The solver now builds the active EqualRadius adjacency graph once per solve.
Driving radius/diameter dimensions and visible corner parameters propagate to
the complete connected component, regardless of pair direction or chain length.
Read-only external circular supports remain authoritative; dimension drivers
take precedence over a dragged radial point. Components without an explicit
driver retain their established pair behavior. Conflicting drivers remain
conflicts; the change never removes equality constraints or relaxes them.

Existing corner radius/diameter dimensions also need their original identities
after derived arcs are materialized. Intermediate materialization now temporarily
holds those dimensions aside and restores them when all named arcs exist.
Explicit corner dimension edits validate the derived profile before committing,
so impossible radii are rejected without changing the Sketch. The document
schema, source geometry identities and Undo transaction boundaries are unchanged.
No user-visible text was introduced; localization coverage remains required.

## Regression matrix

The new native matrix has 192 combinations: star/chain connections, both pair
directions, each of four driving corners, free/fixed rectangle points, a visible
corner radius parameter or ordinary Radius/Diameter dimension, and locked/unlocked
driving dimensions. The lock variant is repeated for the corner parameter, which
has no separate dimension lock flag. Every combination includes:

- Three successive edits, 3 → 5 → 2 → 4 mm, with all four radii checked.
- Reference Radius and Diameter dimensions on other corners, which measure the
  result without driving it.
- Original point and constraint identity checks after every edit.
- Serialization/reopen/solve and materialization of all four arcs after each
  edit, checking endpoint-to-center distances independently.
- Rejection of a 30 mm radius without changing the serialized document.
- A source point drag: actual movement for the free rectangle; unchanged
  serialized geometry for the fixed rectangle. Equality must remain intact.

The full native Sketcher suite, including existing external supports, trimming
and other dimension/drag contracts, passed in 8.66 s. Logs are disposable files
under `build/form-diagnostic/`; `equal-four-baseline-tests.log` records the
reproduction and `equality-native-matrix.log` records the passing native matrix.
An additional matrix links 3, 8 and 16 circles, in both pair directions, with a
Radius or Diameter driver at the end of the chain and three successive edits.
The implementation has no four-member or chain-length limit.

The new `zima_cpp_sketch_four_equal_corners_ui_contract` uses actual common-picker
mouse clicks to create all three equalities, successive inline radius edits,
Sketch confirmation, native save/reopen and Undo/Redo. The existing corner-chain,
dimension-entry, grips and external-constraints GUI suites check dependent paths.
The successive inline edits each retain their ordinary Undo transaction: Undo
must restore all four radii to 2, then 5, then 3 mm; three Redo actions restore
the final Sketch. The first GUI assertion incorrectly expected one Undo to
revert all three edits; the saved native document showed the correct 2 mm state.

The final native suite (including the long-chain matrix) passed in 7.82 s and
the four-corner mouse/inline-edit/save/Undo/Redo GUI test passed in 10.85 s.
Surface placement, FORM hover/confirmation inspection and localization also
passed in the final five-test run (85.69 s total), recorded in
`build/form-diagnostic/equality-confirmed-tests.log`. The dependent external,
grips, dimension-entry and ordinary corner-chain GUI tests passed in the
preceding run; its four-corner failure was the corrected Undo assertion above.

## Limits

The new GUI case covers an XY rectangle with a visible corner radius parameter.
Other orientations, each possible GUI driving corner, GUI Diameter/reference
dimension entry, GUI drags of this exact four-corner fixture and mixed native/
external components are not separately covered by this new GUI case. Existing
native and dependent GUI coverage is not a Cartesian product of those variants.
Linux execution remains unverified.
