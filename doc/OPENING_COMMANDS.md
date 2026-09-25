# Opening commands

`opening.create/get/set` manages current **Opening**: plain/threaded holes, pilot bore,
entry chamfer, and drill tip. Native kind is `FeatureKind::Thread`. Threads use
technological surfaces, not helical B-Rep cuts, matching GUI.

`get` reads without calculation. Creation/editing shares dialog OK: validate, solve
existing placement, calculate bodies, and create one Undo step. Errors commit nothing;
numerically unchanged features do not recalculate.

## Example

```text
new part opening_example
```

Create a centered 40 x 40 mm Sketch and extrude 20 mm on each side. Replace
`SKETCH-ID` with the ID returned by `sketch.create`:

```json
{"command":"sketch.create","arguments":{"name":"Block profile","plane":"XY"}}
{"command":"sketch.rectangle.create","arguments":{"sketch":"SKETCH-ID","first":[-20,-20],"second":[20,20]}}
{"command":"extrusion.create","arguments":{"sketch":"SKETCH-ID","extent":"symmetric","length_forward_mm":20}}
{"command":"opening.create","arguments":{"type":"metric","designation":"M10","bore_length_mm":20,"thread_length_mm":10,"chamfer_enabled":false,"drill_point_enabled":false,"placement":{"z":-20}}}
```

Use returned stable `container`:

```json
{"command":"opening.get","arguments":{"container":"OPENING-ID"}}
{"command":"opening.set","arguments":{"container":"OPENING-ID","bore_length_mm":25}}
```

## Parameters

`create` inserts into the active Body; `set` requires `container`. Both accept optional
target guard `document` and the same properties. JSON numerical values are numbers;
lengths are mm, angles degrees.

| Parameter | Meaning |
| --- | --- |
| `name` | Nonempty feature name |
| `type` | `plain`, `metric`, `whitworth`, `pipe` |
| `designation` | Exact `thread.catalog` size, such as `M10` or `G 1/2` |
| `nominal_diameter_mm` | Plain-hole diameter; catalog controls it for threads |
| `custom_bore_diameter` | Enable custom pilot diameter |
| `bore_diameter_mm` | Custom pilot diameter, requiring the preceding flag |
| `bore_length_mm` | Cylindrical length excluding tip |
| `thread_length_mm` | Thread cylinder length from Origin excluding runout |
| `bore_end` | `length`, `through_all`, or `up_to` with a stored target |
| `thread_end` | `length` or `up_to` with a stored target |
| `direction` | `forward` or `reverse` |
| `chamfer_enabled`, `chamfer_depth_mm`, `chamfer_angle_degrees` | Entry chamfer; included angle |
| `drill_point_enabled`, `drill_point_angle_degrees` | Blind-hole tip; included angle |
| `runout_pitch_factor` | Runout length as pitch multiple, 0–100 |
| `left_handed` | Left-hand thread |
| `placement` | Numerical existing-placement patch: `x/y/z`, `rotation_x/y/z`, available reference offsets |

Lengths range 0.001–1000000 mm; angles 1–179°. Locks prevent direct and catalog-induced
changes. Pilot diameter must be smaller than nominal thread diameter. Thread plus
runout must fit blind depth. Through/Up To disables tip as GUI does; explicitly
enabling it in those combinations is rejected.

Catalog selection adopts nominal diameter, pitch, and automatic pilot diameter while
preserving custom pilot values. Fixed-depth mode may increase default depth to fit
thread/runout, but explicit `bore_length_mm` determines final depth and must validate.
Changing only thread length does not automatically deepen the hole. Changing standard
without designation retains an available size or chooses the closest nominal diameter
as GUI does. Responses always report actual results.

`get` also returns `feature`, `body`, `revision`, locks, and stored
`bore_targets/thread_targets`. `bore_diameter_mm` is the stored threaded pilot diameter;
plain-hole actual diameter comes from `nominal_diameter_mm`.

## Stage boundaries

Opening (`FeatureKind::Thread`) supports targets below. Native Hole has separate
`hole.create/get/set`; its Up To support was a separate subsequent CLI task, now
covered in [NATIVE_HOLE_COMMANDS.md](NATIVE_HOLE_COMMANDS.md). External Thread and
DrillPoint have their own implemented commands. Formats/start templates are unchanged.

## Verification

Model regression compares independent volumes of plain, metric, Whitworth, and pipe
openings. It covers three chamfer angles, tip, direction, through drilling, catalog/
custom pilot diameters, invalid inputs, locks, exact Undo, profile identities, native
saving, and fresh calculation. Actual CLI creates/edits openings. GUI moves from CLI
creation to Properties and checks pending edits, OK/Cancel, Undo, and another catalog size.

Full build first exposed a missing Part-harness Workspace dependency. Pure catalog
selection moved to `document_core`, already used by the harness. Both programs then
built and related tests passed **11/11** (66.97 s):
`build/opening-command-full-build.log`, `build/opening-command-related-tests.log`.
After adding Opening-name translation, creation Undo, indirect catalog locks, and
thread semantics, **3/3** passed (16.96 s), `build/opening-command-final-tests.log`.

Final transaction review moved existing FRONT normalization from GUI into shared
commit, without changing its algorithm or placement solving. A model test additionally
checks Origin-plane-referenced openings and exact placement restoration by Undo.

After this move both programs rebuilt and **5/5** passed (57.25 s):
`build/opening-command-placement-build.log`, `build/opening-command-placement-tests.log`:
Opening, placement, profiles, actual CLI, and GUI confirmation. Catalog: **177 commands**.

## Up To targets: bore and thread

`opening.create/set` accepts independent `bore_targets` and `thread_targets`, activated
by `bore_end:"up_to"` or `thread_end:"up_to"`. Each active target requires exactly one
original reference. Example ending both at the main XY plane:

```json
{
  "command": "opening.set",
  "arguments": {
    "container": "<opening-id>",
    "bore_end": "up_to",
    "bore_targets": [{"owner": "<part-id>:origin", "key": "origin:plane:xy"}],
    "thread_end": "up_to",
    "thread_targets": [{"owner": "<part-id>:origin", "key": "origin:plane:xy"}]
  }
}
```

References require textual `owner`/`key`, with optional `label`, `kind` (`face`/`plane`),
and empty `instance_path`. Sources must be preceding objects or available Origins in
the same Part, possibly in an earlier Body with different placement. Other occurrences,
own/later features, and caller-supplied substitute coordinates are rejected. Persisted
original references determine actual target kind/geometry. Bore accepts original faces
or planes; thread ends must be planar.

GUI/CLI share `prepare_opening_end_target` and calculation. Identical reassignment
returns `changed:false` without Undo; invalid creation/editing publishes no partial change.

Explicit calculation refreshes construction planes from current document data. Planar
solid faces remain original-face references rather than independent construction planes.
The kernel validates original identity for bore/thread and uses current placement.
Internal opening-cut dependencies participate in incremental cross-Body calculation,
including fresh calculation after native reopening.

Lost/suppressed targets mark Opening invalid. Original IDs and last reference data
remain for repair without pretending validity. Suppressing a source may commit with
`calculation_errors` because the dependent opening cannot calculate. Restoring the
source or Undo allows valid calculation again. Properties reads, hover, and tab changes
invoke no OCCT. All persistent data remains in native Part without new formats/sidecars.

### Repair verification

The user explicitly approved the previously deferred target repair on 2026-09-13.
After CLI input integration, baseline confirmed that a suppressed target plane left
Opening valid (`build/opening-target-baseline-tests.log`, 0/1 in 0.32 s).

After repair, full native model contract and new-target tests passed **2/2 in 7.16 s**
(`build/opening-target-second-tests.log`). Coverage includes analytic volumes,
thread-surface ends, two independent planes, stale helper data, moved original-face
recalculation, missing bore/thread targets, no-ops, errors, Undo/Redo, and native saving.
Existing Opening commands and external Threads passed too.

Two-placed-Body extension passed **1/1 in 0.85 s** (`build/opening-target-bodies-tests.log`):
depths 55/56 mm, source changes, Undo/Redo, and fresh incremental diameter changes.
Older geometry tests now create actual construction planes; nonexistent datums with
arbitrary substitute coordinates are invalid. Catalog remains **223 commands**;
suite has 124 tests.

Both applications/all tests built. Full Windows Release passed **124/124 in 527.41 s**,
without failures (`build/opening-target-full-build.log`, `build/opening-target-full-tests.log`).
It includes actual CLI, CLI targets → Opening Properties (56.80 s), startup/translations
(92.26 s), both approved repairs, Assemblies, Sketches, imports, sections, and drawings.
These results belong to this stage; overall CLI coverage was not yet complete.

Queries and optional-component removal: [OPENING_COMPONENT_COMMANDS.md](OPENING_COMPONENT_COMMANDS.md).
