# Sheet material regression documents

- `box-cross-branch.prtz` exercises a branched Sheet Profile box and shared
  references during Unbend/Bend Back.
- `tilted-cone-with-bends.prtz` reproduces the reported `04.prtz` case: a Flat,
  two Sheet Profiles, Sheet Cut, an attached Revolved Sheet with an inclined
  axis. Tests append Unbend All and Bend Back. Calculated body caches were removed; native authored
  history, ancestry and references were preserved. The cone previously unfolded
  alone but failed when unrelated bend partition planes split its material.
  Tests read this fixture or work on temporary copies, never save into it.

The cone and profile-side-twist fixtures explicitly mark original point
references with `body_edge=false` (2026-09-23). This updates the authored test
data to the current reference schema; no legacy loader fallback is required.

All three fixtures now explicitly store the current `origin_point_visible` and
`origin_text_visible` fields as false (2026-09-26). The authored geometry,
references and signed numeric values are unchanged. This repairs stale regression
inputs; it does not introduce a legacy document reader or a new format version.

## Box fixture schema repair — 2026-10-01

`box-cross-branch.prtz` uses native INI format 46, with explicit
`suppressed=false`, `link=null` and `scale=null` fields for its ordinary Body.
It also stores all thirteen user parameters in the current
`UserParameterValues/Data` JSON object. In particular, its authored
`SHEETMETAL_THICKNESS` remains the string `6` under the default language key.

The first repair updated only the format declaration and Body fields. That was
incomplete: the current reader no longer consumes the old individual parameter
lines. It silently read no thickness and calculation used the 1 mm fallback.
The resulting geometry joined the nominally free box walls and failed the
surface-tolerance gate during unfolding. These failures were caused by incomplete
test-data conversion, not demonstrated faults in the current geometry kernel.
Restoring the authored 6 mm parameter in the current representation removes both
failures without changing model geometry algorithms or tolerances.

All original parameter names, language keys and string values were compared with
the previous fixture. Every other INI section remained unchanged by the parameter
repair. The earlier Body-field repair compared all retained numeric values by
exact floating-point bits. No Sketch, reference, constraint, authored dimension,
side choice or signed zero offset was edited. The existing text relation already
has a valid newline and remains unchanged. No production loader, compatibility
branch or format definition changed.

Both fixture consumers now explicitly require 6 mm immediately after loading or
regenerating the box. This catches a lost default directly rather than allowing
misleading downstream geometric failures.

### Validation and remaining fixture work

- The complete `zima_cpp_bend_command_tests` suite passes, including angle edits,
  isolated reference failure/recovery, save/reopen, Undo/Redo, independent walls,
  and source-wall height changes from 150 to 175 mm.
- The box portion of `zima_cpp_sheet_state_command_tests` passes regeneration,
  Unbend, Bend Back, volume preservation and unchanged source feature definitions.
  The full suite also passes after the cone reconstruction described below.
- Five-language translation coverage and catalog validation pass. This change
  adds only English test diagnostics and documentation; no product UI text changes.

Evidence: `build/box-thickness-regressions.log`,
`build/box-thickness-final-tests.log` and `build/box-thickness-build.log`.
No test assertion was removed, and no geometric tolerance was loosened.

## Inclined Revolved Sheet reconstruction — 2026-10-01

The cone-shaped sheet in `tilted-cone-with-bends.prtz` is created by the ordinary
Revolution feature with sheet-metal settings and an inclined axis. It is not a
separate Cone primitive or a newly added application command.

The original fixture used a Bend trajectory containing a circular arc followed
by a 39.812259674072266 mm straight segment. Current Bends own only the circular
arc; the straight wall is now a separate attached Flat immediately after that
Bend. The fixture was rebuilt with the existing native calculation and
`flat.create` command, selecting the first Bend's persisted outer end-rim edge.
The other Bend, Sheet Cut, attached Revolution, and their existing owners and
source references remain in the model. Original arc IDs were retained; the
removed straight segment and its point, tangent constraint and length dimension
are replaced by the new Flat's ordinary owned rectangular Sketch.

The model now stores INI format 46, current Body flags and all thirteen user
parameters in `UserParameterValues/Data`, including the authored 4 mm thickness.
It was regenerated and saved through the current native writer. The committed
fixture contains authored data only; calculated caches were removed so tests
must reconstruct the geometry from real current inputs.

An independent size check confirms the replacement wall's material:
90.6476974487328 mm width × 39.812259674072266 mm length × 4 mm thickness
= 14435.558678742753 mm³. Adding it increases calculated volume from
57434.562983827505 to 71870.12166257024 mm³, matching that analytic increment.
This preserves the old straight continuation's material rather than simply
removing unsupported geometry from the test.

The dedicated native test guards the 4 mm thickness, two Bends, two Flats, the
continuation attachment and its original length. Setup exceptions now return a
normal failed test diagnostic instead of terminating with an unhandled exception.
No production reader or geometry algorithm changed, and no tolerance was raised.

Validation includes the complete native model and each curved region separately,
Unbend/Bend Back volume and rotation-axis identity, and the generated matrix of
five axis slopes, both axis directions and both Revolution directions. The ±10
slope cases also cover cuts authored in the developed state and repeated state
changes. The complete sheet-state command suite covers cold regeneration,
state changes, source definitions, native persistence and Undo/Redo. Both suites
and five-language translation/catalog validation pass.

Evidence: `build/cone-fixture-rebuild/native-regression.log`,
`build/cone-fixture-tests.log`, `build/cone-fixture-final-tests.log` and
`build/cone-fixture-final-build.log`. Changes add no product UI strings; new test
diagnostics and this documentation are English. This fixture reconstruction does
not add legacy-file compatibility or require a new product executable.

`profile-side-twist.prtz` remains a separate outdated fixture; its suite is not
claimed as passing by either repair above.
