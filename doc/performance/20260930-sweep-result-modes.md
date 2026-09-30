# Sweep result modes: verification and performance

2D Sweep and 3D Sweep now add Surface to their existing Solid/Thin choices.
Helical Sweep now offers all three choices. Surface accepts a continuous open
contour or a closed contour, has no end caps, adds no volume, and cannot subtract
material. Existing 2D/Helical closed-section holes produce separate swept walls.
Thin uses the existing offset implementation, side choices and total thickness;
Helical thickness also participates in value locks and Family parameters.

Inputs → means → outputs: authored path/profile identities and result parameters →
the existing explicit sweep calculation, optionally offsetting the section or
omitting solid closure → solid material or original-reference surfaces in one
history transaction. Preview remains independent of OCCT. Neither placement nor
its signed-side contract was changed.

## Verification

The final Windows Release regression selection passed **13/13** in 136.31 s:
2D/3D/Helical model contracts, sweep commands, existing Surface and Thin profiles,
Boundary Surface, Family tables, value locks, translations, both sweep GUI
contracts and the Family-label/template GUI contract.
After completing calculation-message localization, the translation and both sweep
GUI contracts passed again (**3/3**, 44.58 s), including the failed-OK message.

Analytical checks cover rectangular open surfaces and cylindrical closed surfaces,
coexistence with a preceding solid, inner contours, zero surface volume, open
endpoint ancestry, all three Helical thickness sides and both winding directions.
Helical Thin volume is compared with cross-section area times spatial path length.
The fine analytical fixtures explicitly select 0.001 mm construction tolerance;
they do not change the application's 0.1 mm default. Thickness must be meaningful
relative to the selected calculation tolerance, as with existing Thin sweeps.

Surface-area checks exposed fixed-quadrature error on rational surfaces: a
radius-2, length-20 cylindrical wall reported 252.002 mm² instead of 80π mm².
Rational shapes containing surface-result faces now use adaptive area integration.
Solid-only property calculation retains its existing path.

Command tests cover result changes, complete rejection of Surface subtraction,
native persistence, locks and Undo/Redo. The five-language translation test opens
all three actual property dialogs, switches result type, checks visibility and
localized labels, and confirms Surface through OK. Existing sweep GUI tests cover
placement, Sketch editing, confirmation and Cancel. New controls reuse translated
messages already present in every catalog. Sweep calculation diagnostics were
also completed in all five catalogs, and failed confirmation translates the
backend message while keeping the edit open.

`zima_cpp_construction_command_tests --refresh-start-templates` regenerated Part,
Skeleton and Assembly start templates using the changed serializer. Their bytes
were unchanged: these empty templates contain no Helical feature parameters.
The Family-label GUI contract subsequently created Part and Assembly documents
from the templates and confirmed their editable contexts and normal commands.
Helical native feature data now requires `result_type`, `thin_mode`, and
`thickness`; no legacy-field fallback was added.

The older Helical test expected curved sides to reject placement. That failure
was reproduced in a separate build of baseline commit `86d9414f`. It contradicted
the already-approved 2026-09-29 [general-surface placement](../GENERAL_SURFACE_PLACEMENT.md)
contract. Only the obsolete test expectation and documentation were corrected;
the shared placement implementation was not edited.

## Existing Solid performance

Windows Release, same machine and native dependencies, separate baseline/current
builds, no overlapping calculation or compilation. The existing
`zima_cpp_sweep_precision_benchmark OUTPUT 0.1 0.1` measured two warm-ups followed
by six fresh-kernel samples per case. Both arguments are deliberately identical;
this comparison measures unchanged Solid behavior, not precision differences.
Times include calculation, meshing and original-reference preparation.

| Solid fixture | Baseline mean | New mean | Difference |
| --- | ---: | ---: | ---: |
| Planar circular path | 237.12 ms | 236.63 ms | −0.21% |
| Rounded spatial path | 571.82 ms | 572.76 ms | +0.16% |
| Eight-turn helix | 5439.31 ms | 5573.88 ms | +2.47% |

Volumes, areas and triangle counts matched exactly in the reports for each
fixture. Helical sample ranges overlap (baseline 5311.91–5528.26 ms, new
5293.56–5768.16 ms); this small experiment does not establish a statistically
significant regression or a universal performance guarantee. Thin and Surface
costs depend on section complexity; Thin calculates offset walls when selected.
The ordinary Solid path does not perform those additional constructions.

Raw measurements: [baseline](20260930-sweep-results-before.json),
[new](20260930-sweep-results-after.json).
