# Cylinder limits, 3D Section properties and retained Body measurement

Windows verification on 2026-11-20, following release `2026112001`.
These are local development changes; the published release archive is unchanged.
The native file format and factory assets are unchanged by these repairs.

## Changes

- A single curved Extrusion Up To target uses the existing command-local exact
  draft and persisted authored operand wire. Opening unchanged Properties,
  ordinary inspection and unchanged OK do not calculate a new solid. Rapid
  correction edits remain debounced. A corrected single-cylinder limit now
  bounds its auxiliary prism at the first contact, preventing a disconnected
  tail beyond the opposite cylinder wall.
- 3D Section properties use the shared properties scroll container directly.
  The redundant nested scroll widget prevented the shared Origin actions from
  finding their row. The Section's own Origin remains displayed after reference
  entry ends, in both Part and Assembly.
- Body measurement can inspect the actual retained geometry when history has
  failed features. Values carry an explicit warning, including saved records.
  Such a record is not a valid downstream centroid placement reference.
  Completing missing physical integrals updates derived caches only; opening
  or closing the inspector does not dirty the document or create an Undo step.

## Executed checks

| Check | Verified scope | Windows result |
| --- | --- | --- |
| `zima_cpp_extrusion_limit_contract_tests` | 36 cylinder combinations: one-sided, symmetric and unequal two-sided stock; two radii; both contact sides; corrections 0, -2, 1.25. Independent surface equations, analytic volume, source identity and repeated calculation. | Pass, 8.14 s |
| `zima_cpp_ordered_up_to_kernel_tests` | Existing ordered-limit calculation contracts. | Pass, 9.69 s |
| `zima_cpp_feature_up_to_parameters_tests` | Persisted Up To parameter contracts. | Pass, 0.16 s |
| `zima_cpp_feature_cylindrical_up_to_ui_contract` | Both contact sides; exact wire equations; rapid edits produce one draft calculation; unchanged opening/OK; Cancel; changed OK; native save/reopen; authored inspection; Undo/Redo. | Pass, 6.50 s |
| Actual `26.prtz` copy through ordered GUI harness | Existing saved target, corrections 0, -2, 1.25, 0; one rollback scene and no scene republishing for correction changes. Subsequent synthetic ordered targets, real View picking, failure acknowledgement, repair, persistence and Undo/Redo. | Pass |
| `zima_cpp_body_properties_tests` | Analytical solid/surface integrals, sheet integrals, placed Bodies, centroid dependencies, deferred physical analysis, Assembly and historical boundaries; retained geometry before/after a later Cut; missing anchor rejection; warning persistence and Undo/Redo. | Pass, 92.88 s |
| `zima_cpp_measurement_inspector_ui_contract` | Existing Part/Assembly inspector interactions plus failed-history warning/values; no mutation on opening, OK or Cancel; Save creates one record; native reopen; Undo/Redo; invalid centroid source excluded. | Pass, 6.45 s |
| `zima_cpp_section_ui_contract` | Part/Assembly placement layout, whole-Origin entry, own Origin visibility, resizing, live Light/Dark/Light transitions; repeated occurrences; Section Sketch editing and Undo/Redo; existing linked Drawing/hatch/export cases. | Pass, 18.69 s |
| Translation contract | New retained-geometry warning in cs/en/de/fr/ru, rendered with measurement values. Existing catalog and UI coverage. | Pass, 14.61 s |

The Section harness also exercises its existing endpoint drag and dimension
cases, both selection orders for contact with a perpendicular thread axis, and
native persistence. This repair does not change Sketcher equations or tools;
the run is not an exhaustive Sketcher dimension/manipulation matrix.

Test fixtures were corrected to follow established lifecycle contracts: use the
history-cursor command before inspecting a reopened historical boundary; clear
automatic naming when assigning a custom Feature name; respect one-shot Origin
selection; find an axis through the common picker instead of assuming it lies
at the resized viewport's midpoint; wait for an asynchronous exact draft; and
acknowledge a failed definition with the already established second OK.

Private logs and user-model copies are under `build/rip25-20261120/`. The user's
original models were not overwritten. Section layout captures were visually
inspected in both themes; controls occupy their shared row below the title.
No Linux execution was performed. Shared C++ and OCCT dependency pins are unchanged.
