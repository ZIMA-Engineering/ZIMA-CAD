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

`box-cross-branch.prtz` now declares native INI format 46 and explicitly stores
`suppressed=false`, `link=null` and `scale=null` for its ordinary Body. These
fields were introduced after this fixture's previous format 43 declaration.
Only the format declaration and Body-history JSON line changed. Existing Body
values were compared recursively, including exact floating-point bits; authored
Sketches, references, constraints, dimensions and signed zero offsets were not
edited. The existing text relation already has a valid newline and is unchanged.
No production loader, compatibility branch or format definition changed.

A temporary copy passed CLI open, explicit Regenerate, Save, close, reopen,
Regenerate and Save. Sketch point/segment identities, constraints, dimension
side choices and both zero signs survived the round trip. Evidence:
`build/sheet-fixture-check/roundtrip.log`. Five-language translation coverage and
catalog validation passed; this data-only change adds no UI text.

### Remaining failures exposed by loading the fixture

- `zima_cpp_bend_command_tests` now passes loading, the first Bend's 180-degree
  edit with isolated downstream failure, native save of that diagnostic,
  recovery at 90 degrees, Undo/Redo, and the second Bend's edit/Undo. It then
  fails `Free box walls acquired a shared joined edge` in `check_profile(150)`.
  The owners are `01a0af5d46a97ac7bdafefbc7c826ea4` and
  `01a0af5d46a97ac7bdafefbc7c826ef7`. This is an unresolved geometry/adjacency
  contract failure, not a passing full Bend suite.
- `zima_cpp_sheet_state_command_tests` loads and regenerates the box, then
  `unbend.create` reports that region `01a0af5d46a97ac7bdafefbc7c826ecb` exceeds
  the configured surface tolerance. Do not increase the tolerance to hide this.

Logs: `build/sheet-fixture-bend-fixed.log` and
`build/sheet-fixture-dependent-tests.log`. No assertions were disabled.

The other two documents remain unchanged. A temporary schema-only probe of the
cone reached `Bend path requires one circular arc` and an abnormal exit in its
specialized test: its authored historical Bend path also needs to be recreated
with the current arc-only model before a fixture update can be accepted.
The twist suite still reaches its format-43 fixture after its generated chain
cases. Neither suite is claimed as passing, and neither old file was relabeled
as a fully current model. This repair is limited to the box.
