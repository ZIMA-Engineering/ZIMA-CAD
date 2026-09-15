# Mirror and Pattern in the command layer

The command layer exposes `derived_copy.sources`, `mirror.create/get/set` and
`pattern.create/get/set`. GUI Properties share sources, parameter reading,
preparation and commit with CLI. The catalog reached **205 commands** at this
milestone. The creation/editing sections below record that implementation stage.

```json
{"command":"derived_copy.sources","arguments":{}}
{"command":"derived_copy.sources","arguments":{"object":"EDITED-COPY-ID"}}
{"command":"mirror.get","arguments":{"object":"MIRROR-ID"}}
{"command":"pattern.get","arguments":{"object":"PATTERN-ID","document":"OPEN-DOCUMENT-ID"}}
```

All queries accept optional `document`. They can read another open Part/Assembly
without activation, saving, history changes or recalculation. They copy neither
triangulation nor B-Rep and call no OCCT, placement solver or copy regeneration.
With Properties open, queries return committed document values; pending values
remain in the dialog.

## Available sources

`derived_copy.sources` returns `document`, `object`, `boundary`, `revision` and
`items`. Each item has `id`, `name`, `kind` (`body`, `boolean`, `component`),
`visible` and local `instance_path` for Assembly components. Names are not identity;
two insertions of the same file are independent occurrences.

Without `object`, the Part boundary is immediately after the active Body, or at
the root cursor if no Body is active. Editing uses the boundary before the specified
copy. A Boolean consumes input Bodies and offers its own result. Mirror/Pattern
do not consume sources. Ordering follows history.

Assembly offers only its own immediate components before the copy, or all components
without `object`. Suppressed components are omitted; hidden ones remain valid
sources. Internal subassembly Parts do not become components owned by a higher
Assembly. `boundary` is a position in the saved root list, including suppressed
components, never OCCT object order.

The query describes history/ownership availability. It does not create missing
calculation data or imply that an empty Body has geometry. GUI preview uses existing
calculated results. The unused fallback that replaced legacy Parts lacking Body
history with one fictitious Body was removed.

## Saved properties

`mirror.get` and `pattern.get` return `object`, `name`, `kind`, `source`, `origin`,
`visible`, `placement`, `reference`, `reference_valid`, `value_locks`, `document`
and `revision`. Component copies expose actual `copy_placement`, not normal inserted-
component placement.

`placement` retains the existing contract, locks and references. Axis/plane
`reference` contains `owner`, `key`, `instance_path` and `offset_mm`. Source and
reference identity never derives from face order, tree names or geometric similarity.
Mirror additionally returns last-saved `resolved_plane` with `point` and `normal`.
Reading does not repair an invalid reference.

Pattern returns `pattern`: `mode` (`linear`/`circular`), saved `count`, `full_circle`,
`angle_degrees`, last `resolved_origin`/`resolved_axis` and all three saved `linear`
rows. Each row has `axis` (`x/y/z`, or `null` when unused), `spacing_mm`, `count`,
`reverse_count`, `distribution` (`forward/reverse/both/symmetric`) and
`resolved_direction`. Inactive rows and settings of the other mode are preserved.

`instance_count` is the combination count calculated from numeric parameters alone,
including the original source; it is `null` if unevaluable. It is not geometry
validation. Saved `count` remains the circular count even in linear mode;
`instance_count` gives linear combinations. The tested linear Pattern has 12
positions while retaining circular `count:4`. With `full_circle:true`, actual
circular step is 360° / `count`; saved `angle_degrees` retains the custom-step setting.
The resulting Pattern Body contains new copies only: `instance_count − 1` source
copies. Lengths/points are mm, angles degrees, vectors dimensionless.

Missing/foreign objects return `object_not_found`, wrong feature types
`wrong_feature`, unsupported documents `unsupported_document`. A supplied boundary
must identify an actual Mirror/Pattern in that document. Queries never resolve
ambiguous occurrences by name.

Formats, extensions and start templates are unchanged. Required data already lives
inside `.prtz` or `.asmz`.

## Query verification

The first model run passed **1/1** (0.49 s),
`build/derived-copy-query-model-build.log`,
`build/derived-copy-query-model-tests.log`. It independently checks source volume
48 mm³, equal Mirror volume and 11 × 48 mm³ for 12 Pattern positions including the
source; both/symmetric directions, inactive rows, locks, original IDs and native save.

It also checks creation/edit boundaries, Boolean sources/consumed inputs, wrong
types/foreign IDs, repeated Part insertions, hidden versus suppressed occurrences
and explicitly stale Assembly-derived geometry. Reads preserve revision, activation,
history graph and addresses of calculated results/shared snapshots. Actual CLI and
GUI Properties checks were included in the subsequent build.

The first related suite passed **8/9** (80.94 s),
`build/derived-copy-query-related-tests.log`. New GUI coverage showed tree editing
passes only ID; the kind parameter applies to creation. The added GUI kind check
was removed, restoring saved-object-driven editing. `mirror.get`/`pattern.get`
still explicitly validate requested kind. Documentation/model tests also distinguish
saved circular count from total linear combinations.

After correction, both programs built and **9/9** affected tests passed (77.04 s),
`build/derived-copy-query-final-build.log`,
`build/derived-copy-query-final-tests.log`. Permanently registered GUI coverage checks
Part/Assembly Mirror/Pattern, reopening the same Properties, rollback/Cancel, grid
directions, source selection and middle-button confirmation. Console also reads
committed values during pending edits without replacing rollback geometry.

## Creation and editing

```json
{"command":"mirror.create","arguments":{"source":"SOURCE-ID","local_plane":"yz","placement":{"x":-2}}}
{"command":"mirror.set","arguments":{"object":"MIRROR-ID","reference":{"owner":"ORIGINAL-OBJECT-ID","key":"FACE-KEY","instance_path":"","offset_mm":1}}}
{"command":"pattern.create","arguments":{"source":"SOURCE-ID","linear":[{"axis":"x","spacing_mm":30,"count":3,"distribution":"symmetric"},{"axis":"y","spacing_mm":20,"count":2,"reverse_count":2,"distribution":"both"}]}}
{"command":"pattern.set","arguments":{"object":"PATTERN-ID","mode":"circular","count":6,"full_circle":true,"local_axis":"z"}}
```

Creation requires `source`; editing requires `object` and at least one changed
parameter. Both support `name`, `source`, `placement` and feature parameters below.
Mutations target the active Part/Assembly after closing dialogs and Sketcher. The
initial stage used a shared guard restricting nested activation; consult current
[command coverage](CAD_COMMAND_COVERAGE.md) for subsequent activation support.
Commands never infer sources from names or current hover.

Mirror needs either local-Origin `local_plane` (`xy/xz/yz`) or exact `reference`,
never both. Explicit reference `offset_mm` moves its plane along the normal. Source
reflection uses actual document coordinates; the plane belongs to Mirror placement.

Pattern defaults to linear on creation. `linear` completely specifies one to three
direction rows, each requiring `axis` (`x/y/z` or `null`). Optional `spacing_mm`,
`count`, `reverse_count`, `distribution` retain existing row values when omitted.
Omitted rows deactivate but keep numeric settings. Axes cannot repeat. Rows use
the Pattern's local Origin axes, not arbitrary vectors.

Spacing range: 0.001–1,000,000 mm; directional count: 2–1000. `forward`, `reverse`
and `symmetric` counts include the source; symmetric counts must be odd. `both`
uses `count` forward including source plus `reverse_count` (1–999) additional
backward positions. Total combinations cannot exceed 1000.

`mode:"circular"` uses `count` (2–1000), `full_circle` and `angle_degrees`
(−359.999 to 359.999°). A custom step must distinguish occurrences within one turn.
`local_axis` selects local `x/y/z`; alternatively `reference` selects an original
axis or straight edge. Zero axis-reference offset is allowed; nonzero reference
offset is reserved for Mirror planes. Default circular reference is local Z.

Circular arguments apply only in circular mode, `linear` only in linear mode;
`mode` may change in the same command. Switching Pattern modes preserves inactive
directions and circular settings. Editing cannot convert Mirror ↔ Pattern.

`placement` is a numeric patch of the same Properties placement: `x/y/z`,
`rotation_x/y/z`, `reference_offset:N`, in mm/degrees. Locked/reference-driven field
rules are unchanged. Generic addition of placement references was a separate stage.

## Commit, locks and errors

Shared `prepare_derived_copy_edit` retains revision, history boundary, original
parameters and viewer references without OCCT. OK/commands use `commit_derived_copy`,
the existing placement solver and derived Body/component calculation. New geometry
is explicitly calculated at commit. Cancel saves no pending values. A changed
document rejects stale preparation with `document_changed`.

Changes create one Undo step. Identical settings are a no-op without revision or
calculation. Sources must exist before the operation; self, missing, foreign and
later sources are rejected before mutation. Part result/source errors prevent
commit. A separate downstream-feature error can remain in history as before;
commands explicitly return `calculation_errors` and `changed:true` in that case.

Copy locks use `value_lock.list/set`: local placement in `placement:*`, and Pattern
`pattern:angle`, `pattern:spacing:0/1/2`. Components use `copy_placement`, not normal
component placement. Angle locks also prevent indirect angle changes through
full-circle count. Spacing locks belong to exact Properties rows, including inactive
ones. Dialog unlocking commits with values; console rejects mutations during pending
Properties.

Editing a component copy preserves its visibility and colour/appearance overrides.
Geometry, material and source properties still derive from the existing source
calculation. Creation retains source-component property inheritance. Source Parts
are not modified.

## Creation/editing verification

After extracting commit, existing query and actual GUI tests passed **2/2**
(8.01 s), `build/derived-copy-shared-edit-build.log`,
`build/derived-copy-shared-edit-tests.log`.

New model tests passed **1/1** (0.81 s),
`build/derived-copy-command-model-build.log`,
`build/derived-copy-command-model-tests.log`. Independent volumes: 48 mm³ Mirror,
528 mm³ for 12 positions, 912 mm³ for 20, and 2000 mm³ for an Assembly Pattern of
three positions from a 1000 mm³ source. Checks include manually calculated reflection/
grid bounds, original plane, copy IDs across count changes, inactive modes, locks
and indirect angle changes, atomic failures/count overflow, sources beyond the
boundary, stale preparation, Undo/Redo, cold native calculation and retained
component appearance.

The first full expanded suite passed **105/105** (496.11 s),
`build/derived-copy-command-all-build.log`,
`build/derived-copy-command-full-tests.log`. Further checks then examined current
unsaved sources in pure CLI without GUI scene refresh.

That scenario exposed stale CLI source data: enlarging source volume from 1000 to
2000 mm³ still produced a newly committed 1000 mm³ copy
(`build/derived-copy-unsaved-repro-tests.log`, 0/1). GUI refreshes in `refresh_scene`;
Assembly-copy preparation now obtains current sources through existing
`Workspace::refresh_source_geometry`. It shares calculated data without solving
mates or recalculating old copies/cuts. Queries do not call this preparation.
The source Part is neither saved nor modified; source refresh adds no separate Undo.

After the fix, model tests passed **1/1** (0.75 s),
`build/derived-copy-unsaved-fix-build.log`,
`build/derived-copy-unsaved-fix-tests.log`. Final coverage added Undo to the old copy
result while retaining current shared source data, Redo, source edits in a real CLI
process and locked spacing in GUI Properties. Both programs built; **14/14** affected
tests passed (85.98 s), `build/derived-copy-final-build.log`,
`build/derived-copy-final-tests.log`. The full 105-test run preceded this final source-
sharing fix. Formats/templates are unchanged.

## Original references of nested copies

Mirror/Pattern analytical faces respect nested placements and are readable through
exact virtual Pattern paths. Further copies, contextual projection and native
reopening share these rules. Activation targets the original source; virtual nodes
do not own source-document placement. Explicit copy recalculation repairs older
calculated data, never a read. Geometry proof/regressions:
[NESTED_COPY_REFERENCES.md](NESTED_COPY_REFERENCES.md).
