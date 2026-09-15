# Section commands

## Reading saved definitions

`section.list/get/components` read open Parts/Assemblies. Optional `document` permits
other open documents without activation. Queries neither open dependency files nor
change history, invoke OCCT/mate solving, or calculate cuts over body meshes. They
share `sections_for_part`/`sections_for_assembly` with GUI and Drawing sources.

```json
{"command":"section.list","arguments":{"offset":0,"limit":100}}
{"command":"section.get","arguments":{"object":"<section-id>"}}
{"command":"section.components","arguments":{"object":"<section-id>","offset":0,"limit":100}}
```

- `list`: section ID (`object`), name, owned Sketch/Origin IDs, display/reversal flags
  and saved placement-reference validity.
- `get`: additionally complete saved placement/references, sketch frame, `path_mm`,
  section `frames`, `valid`, `error`. Dimensions are mm, angles degrees. Section
  frames derive only from saved ZIMA sketch/frame, without bodies/meshes. `path_mm`
  merges adjacent collinear segments as display does. `sketch.get` with the returned
  sketch ID provides original points/IDs.
- `valid` describes the open cutting path, frame and saved placement-reference flag.
  It neither checks current-body intersection nor freshly verifies every reference.
  Invalid sections remain readable with IDs and errors.
- `components`: `component` key (Part Body ID or exact Assembly occurrence path),
  name, `available`, `stored`, `mode`, `custom_hatch`, effective `hatch`. Modes:
  `cut_hatch`, `cut_only`, `uncut`; patterns: `parallel`, `cross`, `dashed`. Hatch
  parameters: `angle_degrees`, `spacing_mm`, `offset_mm`. Default angle alternation
  is shared with GUI.
- Saved settings for missing components return `available:false`. Repeated sources
  have distinct paths/settings. Assembly lists derive from its saved hierarchy.
- `list`/`components`: `offset >= 0`, `limit` 1–10000, default 2000. Results include
  `items`, `total`, owning document and `revision`.

Errors: `section_not_found` for missing sections, `unsupported_document` without an
appropriate open model, `invalid_arguments` for invalid pagination.

## Implementation and query verification

Shared activation/removal and creation/Properties including hatching are below.
`section.sketch.edit` handles whole-sketch batches. Ordinary Sketcher commands reject
direct section-sketch mutation: the section transaction must validate the complete
open connected path. Shared placement, native formats and templates are unchanged.

Baseline failed because the command was missing (0/1 in 0.10 s). After implementation,
model regressions/catalog passed **2/2 in 0.53 s**: saved IDs, path/direction, custom
hatching, missing bodies, pagination, invalid sections, inactive documents, native
Assembly, repeated nested occurrences and unchanged history/cache.
Logs: `build/section-query-baseline-tests.log`, `build/section-query-first-tests.log`.
Both apps/all tests built. Integration **4/5 in 96.46 s** exposed a process-test input
path converted using Windows system encoding. Shared `path_to_utf8` fixed the test,
which passed **1/1 in 21.32 s**, without subsequent production changes. Startup/
translations passed in 92.58 s. Logs: `build/section-query-integration-tests.log`,
`build/section-query-utf8-tests.log`. Catalog: **221 commands**, 123 tests.

## Activation and deletion

```json
{"command":"section.activate","arguments":{"object":"<section-id>"}}
{"command":"section.activate","arguments":{}}
{"command":"section.delete","arguments":{"object":"<section-id>"}}
```

`section.activate` enables at most one section. Empty/omitted `object` selects permanent
No section without changing section-plane visibility. Activation checks saved placement
validity and open path. Repeating state returns `changed:false` without Undo. No section
also works when a saved section definition is invalid.

`section.delete` removes only the supplied definition. Missing IDs return
`section_not_found` without mutation. Undo restores identical Sketch, Origin, component
settings and activation.

Actions require the displayed active Part/Assembly, like GUI tree menus. Results:
`document`, `object`, `changed`, `revision`. Commands/tree share
`workspace::activate_section/remove_section`. Part retains shared external-reference
summary updates when removing owned sketches. One native-model transaction calculates
no bodies, moves no components and invokes no mate solver. Section display then uses
normal View data.

Baseline failed on missing `section.activate` (0/1 in 0.22 s). Model/catalog then
passed **2/2 in 0.53 s**: activation/No section, no-op, missing IDs, invalid paths,
Undo/Redo, inactive documents and unchanged shared Assembly geometry snapshots.
Logs: `build/section-action-baseline-tests.log`, `build/section-action-first-tests.log`.

Both apps/all tests built. Affected integration **6/6 in 129.03 s**: section geometry,
commands, real CLI (20.95 s), catalog, startup/translations (93.20 s), GUI context
actions (14.06 s). GUI also checks no-op activation, actual tree-menu deletion and
full-body restoration. Log: `build/section-action-integration-tests.log`.
Catalog: **223 commands**.

## Hatch Properties precision

Opening/confirming Properties preserves full saved hatch precision and settings for
unavailable components. Editing one field changes only that parameter; batch changes
apply to selected rows. Rotation adds 90° to each row's own exact angle.

The new GUI regression first failed on rounding unchanged values
(`build/section-hatch-baseline-tests.log`, 0/1). After correction, both apps/tests
built and affected Section/Drawing tests including both GUI contracts passed
**4/4 in 19.40 s** (`build/section-hatch-verified-tests.log`). Catalog remained 223.

## Creation and Properties

`section.create` creates from a complete open polyline. `section.set` edits saved
definitions, retaining section, owned Sketch and Origin IDs. Both commands and dialog
OK share `workspace::prepare_section_edit/commit_section`.

```json
{"command":"section.create","arguments":{"path_mm":[[-50,0],[50,0]],"plane":"XY","name":"A–A","show_cut":true}}
{"command":"section.set","arguments":{"object":"<section-id>","reversed":true,"placement":{"y":2}}}
{"command":"section.set","arguments":{"object":"<section-id>","components":[{"component":"<body-id-or-occurrence-path>","mode":"cut_hatch","hatch":{"angle_degrees":12.3456789,"spacing_mm":2.3456789,"offset_mm":0,"pattern":"cross"}}]}}
```

- `path_mm` is required only on creation: 2–10000 finite coordinate pairs, each within
  ±1000000 mm. Adjacent points must be more than 1e-7 mm apart. Native validation
  rejects closed, branched/disconnected paths. Ordinary segments/points get stable IDs.
- Optional: `name`, `plane` (`XY`, `XZ`, `YZ`), `reversed`, `show_plane`, `show_cut`,
  `placement`, `components`, target `document`. Empty/duplicate names are rejected;
  unnamed creation finds free A–A, B–B, etc. Activating a new section disables the
  previous active section in the same transaction.
- `placement` patches numeric `x/y/z`, `rotation_x/y/z`, `reference_offset:N` under
  the existing contract, in mm/degrees. Locked, constrained and unknown fields are
  rejected. These commands initially did not add/replace placement references;
  see [SECTION_REFERENCE_COMMANDS.md](SECTION_REFERENCE_COMMANDS.md) for that API.
- `components` partially updates at most 10000 components, each exact key once.
  Omitted components/fields remain, including saved unavailable Body/occurrence choices.
- Components accept `mode`, `custom_hatch`, partial `hatch`. First enabling custom
  style derives from effective inheritance. Supplying `hatch` enables it and conflicts
  with `custom_hatch:false`. `custom_hatch:false` alone restores inheritance without
  losing saved custom values. Spacing is 0.1–100 mm; numbers retain full precision
  independently of GUI rounding.
- Complete private drafts validate before one history commit using calculated meshes
  and original references. No OCCT, body-geometry change or Assembly mate solving occurs;
  only the defined mesh section is calculated, as Properties OK does.
- Stale dialogs, changed identities, invalid input/unresolved references preserve
  document/cache. Identical final drafts return `changed:false` without history.
  Results contain `section.get` data, `document`, `revision`, `changed`,
  `body_calculated:false`.

Native `.prtz/.asmz` formats/templates are unchanged. Ordinary Sketcher mutations
cannot commit individual section entities; rebuilding the path requires one complete
validated section operation.

Baseline failed on missing `section.create` (0/1 in 0.11 s). Basic models passed
**2/2 in 0.35 s**. Expanded coverage found its own stale pointer after Workspace
insertions; looking up Part again by ID fixed it, **1/1 in 0.19 s**. Checks independently
verify area 300 mm², preserved volume 6000 mm³, hatch precision, invalid batches,
single activation, stale drafts, IDs, Undo/Redo, native files and independent repeated-
nested-occurrence settings.

Both apps/all tests built. Integration **9/9 in 190.58 s**: Section models, new/existing
commands, catalog, real CLI (28.22 s), GUI console (47.29 s), startup/translations
(98.20 s), Section Properties GUI (15.08 s), Drawing contract. GUI creates via console,
opens Properties and renames without losing exact hatch values.
Logs: `build/section-properties-baseline-tests.log`,
`build/section-properties-first-tests.log`, `build/section-properties-expanded-tests.log`,
`build/section-properties-pointer-tests.log`, `build/section-properties-full-build.log`,
`build/section-properties-integration-tests.log`.
Catalog: **225 commands**, CTest 125 tests; this stage ran the nine affected integration tests.

## Owned-sketch batch editing

`section.sketch.edit` takes `object`, `operations`, optional `document`. All 1–1000
operations use one private owned-sketch copy. The final section commits through the
same transaction as Properties OK.

```json
{"command":"section.sketch.edit","arguments":{"object":"<section-id>","operations":[{"command":"sketch.point.move","arguments":{"point":"<first-point-id>","position":[-20,2]}},{"command":"sketch.point.move","arguments":{"point":"<second-point-id>","position":[20,2]}}]}}
```

Inner operations share `sketch.*` mutation names, arguments and native implementation,
but **omit `sketch` and `document`**: the outer command fixes ownership. Local geometry,
constraint, dimension, trim, offset, text, spline and external-reference edits are
available. New standalone documents/sketches, reads, file import, save and other model
operations are unavailable in batches. Normal `sketch.*` mutation of saved section
sketches remains rejected to protect final validation.

Each step uses original typed argument declarations, Sketcher geometry, derived-curve
refresh and sketch validation. Final section validation occurs after the entire batch,
allowing, for example, deleting one segment and replacing it with two even though
intermediate state is not a cutting path. Final geometry must be an open connected
polyline of ordinary segments, as in GUI. Circles/arcs/import blocks cannot create
unsupported section profiles. Construction geometry remains in the owned sketch.

- `sketch.entities`/`sketch.entity.get` for the sketch ID from `section.get` provide
  existing point/curve/constraint IDs.
- Position changes retain IDs; explicit deletion/recreation follows normal Sketcher
  identity/dependency rules.
- References use original owner/semantic key, plus exact Assembly `instance_path`.
  Identical edges in separate occurrences stay distinct. Creation, explicit refresh
  and detachment use shared Sketcher operations without body recalculation.
- Step failures return their original code and zero-based `operation_index`. Invalid
  final paths reject the entire batch. No intermediate results are published.
- Success returns ordered `results`, including new IDs, plus section data, `changed`,
  `revision`, `body_calculated:false`. Result IDs are not placeholder variables for
  later steps of the same batch; requests supply actual IDs/coordinates.
- The complete change has one Undo/Redo step; identical final drafts add no history.
  Data lives only in native Part/Assembly files.

Baseline confirmed missing command (0/1 in 0.13 s). Expanded model test passed
**1/1 in 0.17 s**: ID-preserving moves, no-op, late batch errors, invalid final paths,
whole-path replacement, original references/refresh/detachment, repeated Assembly
occurrences, source sharing, Undo/Redo and native save. Initial cache checking wrongly
compared a new history wrapper's address; corrected checks verify calculated-geometry
preservation and identical shared Assembly source snapshots.
Logs: `build/section-sketch-baseline-tests.log`, `build/section-sketch-first-tests.log`,
`build/section-sketch-expanded-tests.log`.

After both apps/all tests built, full suite passed **125/126 in 534.86 s**. New GUI
coverage exposed Section Properties cleanup leaving tree reference-selection mode
active, causing later console mutations to report ongoing editing. Local dialog cleanup
now clears it on OK and Cancel; general placement solving is unchanged.

After rebuilding apps/tests, affected suites passed **6/6 in 58.71 s**, including all
Section models, GUI console (45.47 s), full Section GUI (12.65 s), subsequent batches
and Undo after closing Properties. Other 125 tests passed in the preceding full run;
the full suite was not repeated after this local fix. Real CLI and translations were
included in the full run.
Logs: `build/section-sketch-verified-build.log`, `build/section-sketch-full-tests.log`,
`build/section-sketch-gui-diagnosis-tests.log`, `build/section-sketch-gui-fixed-build.log`,
`build/section-sketch-gui-fixed-tests.log`.
Catalog: **226 commands**, total 126 tests.
