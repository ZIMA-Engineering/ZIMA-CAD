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
  The full suite still stops at the separate `tilted-cone-with-bends.prtz` file,
  which declares unsupported format 43. Do not report the full suite as passing.
- Five-language translation coverage and catalog validation pass. This change
  adds only English test diagnostics and documentation; no product UI text changes.

Evidence: `build/box-thickness-regressions.log`,
`build/box-thickness-final-tests.log` and `build/box-thickness-build.log`.
No test assertion was removed, and no geometric tolerance was loosened.

The cone and profile-side-twist documents remain unchanged. A previous temporary
schema-only cone probe reached `Bend path requires one circular arc` and an
abnormal exit in its specialized test: its authored historical Bend path also
needs recreation with the current arc-only model before a fixture update can be
accepted. The twist suite still reaches its format-43 fixture after its generated
chain cases. Neither suite is claimed as passing. This repair is limited to the
box, and does not imply support for loading legacy user documents.
