# Coordinate dimensions on native incidence points — 2026-10-19

## Cause and bounded correction

The wing-nut edit matrix exposed a repeatable coordinate-dimension failure.
An auxiliary point had an ordinary C relation to a native support. Changing
its X/Y ordinate moved that point alone; the next incidence projection returned
it to the unchanged support. Valid edits could therefore cycle and be rejected.

The existing coordinate-dimension correction now carries its H/V coordinate
group and movable native line/circular support. It reuses the existing point
translation closures, as the point–line dimension path already does. The
ordinary solver still verifies all equations and rejects impossible edits.
Immutable supports are not carried; external source geometry remains read-only.
The change does not alter ordinary C-point dragging into a new support-editing
gesture: such points retain their existing sliding/angular interaction.

This is shared C++ code. There is no native-format, placement-contract,
dependency-pin or factory-asset change, and no new application-visible string.
The native test-process helper optionally uses the existing CLI `--keep-going`
mode to inspect state after an expected rejection; authoring keeps its original
fail-fast default.

## Finite Windows matrix

| Gate | Coverage and result |
| --- | --- |
| Native command matrix | 96 cases: line, circle and long arc; X/Y ordinates; positive/negative orientation; driving/reference dimensions; locked/unlocked supporting dimensions; fully/underconstrained geometry. All passed. |
| Successive actions | Each driving case performs three value edits with Undo/Redo. Reference cases perform three non-driving value attempts. Every case performs three support-handle drags and native save/reopen. |
| Independent equations | Actual stored point–line cross product, circular radius, H/V contact, coordinate values, point identity and expected DOF are checked after actions. Free handles must move; locked fully constrained handles must remain unchanged. |
| External controls | 48 native line/circle/arc cases: both coordinate axes, signs, source directions and lock states. Three successive ordinate edits and three drags, independent equations, exact read-only source data and serialized reopen passed in the Sketcher contract. |
| GUI | Three native fixtures: line, circle and long arc. Actual ordered picking and dimension double-clicks, three inline value edits, three native support-handle mouse drags, Undo/Redo and native save/reopen passed. |
| Existing behavior | Eight native/localization contracts passed: Sketcher, Sketch serialization, dimension commands, reference commands, Relations, text geometry, text commands and translations. Existing tangent/trim matrices run inside the Sketcher contract. |

The command receipt is `build/sketch-coordinate-incidence-v6/results.json`;
its executable SHA-256 is recorded beside it. GUI receipts/screenshots are in
`build/cpp-windows-release/coordinate-incidence-ui/` and
`build/sketch-coordinate-incidence-gui.log`. The eight-contract log is
`build/sketch-coordinate-native-regression.log`.

A historical development CLI (`2026101603`, commit
`312b83ba3920edc3182c6d307559bfa515d475a4`, modified source) passed 48/96 of the
same cases and rejected the 48 driving-incidence cases. This is a historical
comparison, not an exact-parent binary ablation. Separately, the current-parent
wing matrix rejected both thickness drivers before this correction; all four
studies now pass three successive edits of both thicknesses.

Single-point/axis ordinate factories have one point and one typed axis argument;
there is no second participant order to reverse. The external matrix reverses
the actual support direction instead. This matrix does not certify every
two-point, symmetric, oblique point–line or tangential dimension combination.
Trimming an auxiliary incidence point itself and exhaustive GUI permutations
remain unverified; the existing dependent-trim contracts are supporting evidence,
not a substitute for those variants.

## Remaining wing-chain failure

The casting study's coupled crown arc and two tangent arms still rejects span,
overall height and crown-radius edits. Hub top diameter/height changes also fail
when Relations updates that coupled outline. These are retained failures, not
successful edits or reasons to remove tangency. The studies are not eligible
for factory publication; see [Wing-nut studies](../WING_NUT_LIBRARY.md).

Linux has not been built or exercised for this correction. The published
Windows release `ZIMA-CAD-2026101906` has not been replaced or retagged.

## Reproduction

```text
python tools/verify-sketch-coordinate-incidence.py --output build/coordinate-incidence-check
ctest --test-dir build/cpp-windows-release -R "^zima_cpp_sketch_coordinate_incidence_ui_contract$" --output-on-failure
ctest --test-dir build/cpp-windows-release -R "^zima_cpp_sketcher_contract_tests$" --output-on-failure
```

Use a new output directory. Build the corresponding current native targets
first; the command matrix also accepts an explicit `--cli` for comparisons.
