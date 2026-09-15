# Extrusion and Revolution in console and CLI

`extrusion.create/get/set` and `revolution.create/get/set` share profile commit with
Properties OK. In Part, creation converts a standalone sketch into a profile feature,
preserving container ID, Origin, placement, owning Body and history position. The
operation gets a new feature ID; its sketch stays in the same container without copying.

## Input

First create a sketch with `sketch.create` and populate a closed profile through
Sketcher commands; Thin can also use open profiles. IDs below come from command
results, not tree names or edge indices.

```json
{"command":"extrusion.create","arguments":{"sketch":"<sketch-ID>","length_forward_mm":20,"name":"Extrusion"}}
{"command":"extrusion.get","arguments":{"container":"<container-ID>"}}
{"command":"extrusion.set","arguments":{"container":"<container-ID>","extent":"two_sides","length_forward_mm":30,"length_reverse_mm":5}}
{"command":"revolution.create","arguments":{"sketch":"<sketch-ID>","axis":"<centerline-ID>","angle_degrees":90}}
{"command":"revolution.get","arguments":{"container":"<container-ID>"}}
{"command":"revolution.set","arguments":{"container":"<container-ID>","angle_degrees":180}}
```

Revolution uses a green construction axis in its own sketch. Convert a segment with
`sketch.segment.centerline`. Without `axis`, use the sole available axis; multiple axes
require exact ID. An invalid explicit ID is never replaced by another axis.

Numeric arguments are JSON numbers: lengths mm, angles degrees, independently of
display units. Shared parameters: `name`, `combine` (`add`, `subtract`), `extent`
(`one_side`, `two_sides`, `symmetric`), `direction` (`forward`, `reverse`),
`profile_offset_mm`. `placement` accepts the shared `placement.set` numeric object,
including existing locks/restrictions. Current work-plane selection is documented in
[WORK_PLANES.md](WORK_PLANES.md).

Extrusion uses `length_forward_mm`/`length_reverse_mm`; Revolution
`angle_degrees`/`angle_reverse_degrees`. Length range: 0.001–1000000 mm; each angle:
0.001–360°. Actual total revolution must satisfy model-kernel limits. Symmetric mode
sets both sides to the forward value; conflicting explicit reverse values are errors.

`end_forward`/`end_reverse`: `length`, `through_all`, `up_to`. Through-all applies
only to subtraction. One-sided through cuts begin at the profile plane in the chosen
direction; cutting both ways requires `two_sides` and both ends `through_all`.

`get` returns document/container/feature/sketch/Body IDs, parameters, locks, placement
validity and saved end targets. Optional `document` can read another open Part, including
during Properties, without calculation, reference solving, history or dependency loading.

## Thin results

`result_type`: `solid` or `thin`. Thin uses `thin_thickness_mm` and `thin_mode`
(`one_side`, `other_side`, `symmetric`). Type/side changes calculate actual bodies and
enter calculation fingerprints. The former bug saving full volume despite Thin preview
was removed from model calculation.

```json
{"command":"extrusion.set","arguments":{"container":"<container-ID>","result_type":"thin","thin_thickness_mm":1,"thin_mode":"symmetric"}}
```

Closed contours form walls between offset outlines. Open contours close at their two
original endpoints. End identities derive from those points, side identities from
original curves. Inner/outer sides retain identity across thickness/side changes.
Preview shares calculation profile order/start point without OCCT.

Circles remain analytical. A standalone spline's mathematical offset becomes one
B-spline with controlled deviation at most 1e-7 mm; unacceptable approximations are
rejected. Edges retain original identity and save curve data in calculated documents,
not merely display polylines.

Thin uses contours without internal holes. Excessive thickness, collapsed outlines,
disconnected paths or unsupported offset topology changes return errors. Other connected-
curve offset limits match shared Sweep Thin calculation. Commands never omit invalid
geometry or replace it with a solid result.

## Transactions and initial boundaries

Part mutations require the active owning Body. GUI editing and derived-Body edits are
rejected. Failed calculation/invalid arguments preserve sketches, history, revision and
calculated bodies. Successful changes form one Undo/Redo transaction. OK and `.set`
explicitly calculate even with identical numeric parameters because the sketch may
have changed. Dependent Assemblies are not automatically regenerated.

The initial stage retained separate Assembly-cut execution and deferred nested activation/
command-driven Assembly cuts. Later Assembly support is described below.

This stage changed no native formats/templates. Required data stays in `.prtz`, `.asmz`,
`.drwz`.

## Initial verification

Model tests compare independent volumes for rectangular extrusion, revolved annuli,
one-/two-sided through subtraction. They check changed sketches with unchanged feature
parameters, container identity, locks, errors, Undo/Redo and saved calculated bodies.
Separate CLI processes run without an available Qt platform plugin. GUI console creates
both types, edits through actual Properties and compares saved volume.

Full Windows Release **90/90** (420.90 s), `build/profile-full-tests.log`. Later fixes
safely retain the first reference before removing an old orientation item and prevent
taking another feature's sketch; both have regressions. Final model/GUI paths **9/9**
(112.75 s), `build/profile-final-tests.log`. This initial stage still rejected Thin;
the next verified real walls. Both normal applications built in
`build/profile-final-build.log`.

Thin tests compare cylindrical/rectangular walls, open segments, arcs, splines and
Revolution against independent volumes, using numerical integration of underlying cubic
length for splines. They cover thickness/side/direction changes, preview/body agreement,
native save, cache fingerprints and reference ancestry. Shared geometry/3D Sweep
regressions **3/3** (17.70 s), `build/thin-profile-spline-tests.log`.

Full Windows Release after GUI/CLI Thin support: **90/91** (427.39 s),
`build/thin-profile-full-tests.log`; both apps/all tests built in
`build/thin-profile-full-build.log`. The sole failure was a 10-second automatic DXF
file-selection timeout during profile editing. The same build passed independently
**1/1** (9.07 s), `build/thin-profile-dxf-recheck.log`. After adding timeout diagnostics,
**three repeats** passed (27.75 s), `build/thin-profile-dxf-repeat-tests.log`. The
isolated timeout cause was not reproduced and is not claimed as an import-algorithm
fix; later full suites must retain coverage.

Stricter spline/reversed-direction checks **1/1** (0.52 s),
`build/thin-profile-precision-tests.log`: volume deviation from independent integration
**1.990028e-9 mm³**, allowed 1e-4 mm³. Offset edges contain saved exact B-splines;
direction changes preserve face parents. Final build: `build/thin-profile-final-build.log`.

## Two target limits: kernel stage

Kernel now has independent forward/reverse extrusion limits. Both can constrain profiles
with oblique planes; reverse also supports original exact faces. Through cuts can combine
with opposite-side targets. Limit changes preserve profile-derived cap/side identities.

Oblique-plane checks use exact whole-profile bounds in plane coordinates, also rejecting
planes intersecting circles away from seam points. Original planar faces supply their
current plane during explicit calculation; stale numeric target snapshots cannot override
it. Kernel verifies exact original owners, including compound profile features.

This stage prepared the kernel; native adapters, second preview end, reference refresh
and CLI input followed below. It added neither commands nor native fields.

Initial base/Thin contracts **3/3** (7.31 s), `build/extrusion-limits-kernel-tests.log`.
Expanded geometry, Sweep 2D/3D, ShaftThread, Hole and Thin suites **6/7** (22.48 s),
`build/extrusion-limits-related-tests.log`: a new cut fixture failed to place stock in
the Body. Corrected fixture **1/1** (0.23 s), `build/extrusion-limits-fixture-tests.log`.
Volume cases include two oblique planes, both through-cut combinations, Thin, original
planes with deliberately stale snapshots, exact surface limits, wrong sides, missing
sources, face IDs and changed cache fingerprints.

## Original Part end targets

`extrusion.create/set` accepts `targets_forward`/`targets_reverse`, each at most one
reference. References contain original ZIMA `owner`, `key`, optional `label` and `kind`
(`plane` or `face`). Callers supply no numeric coordinates; the shared transaction reads
original document data. Empty arrays clear targets. Active `up_to` requires exactly one.

```json
{"command":"extrusion.set","arguments":{"container":"<container-ID>","extent":"two_sides","end_forward":"up_to","targets_forward":[{"owner":"<original-owner-ID>","key":"<original-face-key>"}],"end_reverse":"length","length_reverse_mm":4}}
```

Construction planes use `entity` and key `plane`, not container ID. Origin planes use
original Origin ID and `origin:plane:xy`, `origin:plane:xz`, `origin:plane:yz`. Targets
may be earlier original faces or available Origins. Later objects, unknown IDs and
foreign occurrences are rejected before transaction. Assembly references use the
separate Assembly path below.

Planarity derives from saved analytical face geometry, not merely coplanar display
triangles: coarse curved faces cannot masquerade as planes. Native types define
construction/Origin planes.

Sides can independently use planes/faces, length or through cuts. `symmetric` mirrors
the forward limit across the profile plane and ignores unused reverse targets. Preview
resolves both ends from saved ZIMA data. GUI uses the common picker and same transaction.

Calculation retains original faces before subsequent Booleans, so end references survive
trimming of visible portions. Only requested faces pass between Bodies in appropriate
local coordinates. Cache fingerprints include source/target-Body geometry and placement.
Runtime original-face chains share OCCT objects, without copying whole B-Rep per history
step. After cold loading, explicit calculation reconstructs needed original topology
from native history.

Original-geometry changes refresh saved target snapshots during calculation. Lost references
retain IDs/last data for repair but do not validate active termination. Reads/tab switches
do not run the kernel. Format/templates unchanged; catalog stayed at 164 commands.

Verification:

- Full Windows Release **92/92** (421.62 s), `build/extrusion-target-complete-tests.log`;
  both apps/tests built, `build/extrusion-target-complete-build.log`.
- After coarse-face protection, labels and translations, affected suites **9/9**
  (47.02 s), `build/extrusion-target-final-tests.log`; final build
  `build/extrusion-target-final-build.log`.
- Geometry checks two oblique limits, Thin, mixed through cuts, symmetry, reverse
  direction, ignored inactive reverse references, faces removed from visible results,
  rotated Bodies, moved sources/targets, cold caches and source suppression. Spherical
  termination compares analytical volume and checks sphere equations at saved points
  of both mirrored caps.
- Commands target planes, main Origin and original faces of other translated Bodies,
  modify sources, Undo/Redo, save and atomically reject invalid references. Real CLI
  also checks UTF-8 labels, two limits and symmetric termination.
- GUI actually clicks fields/View planes, checking Cancel, OK, Undo and saved volume.
  Target construction planes have nonzero offsets to catch substitution of their Origins.

The first full run exposed two fixture errors: an axis parameter applied to a plane,
and an incorrect expected error code for rejected reference removal. Both fixtures were
corrected; final results above include them. No external cache/new format was introduced.

## Assembly profile cuts

The same `extrusion.create/get/set` and `revolution.create/get/set` work in active
Assemblies. `assembly.cut.list` lists cuts without calculation/source opening. Assembly
`create` takes a standalone sketch and assigns a new owning cut container; Part
conversion still preserves its original container.

```json
{"command":"extrusion.create","arguments":{"sketch":"SKETCH-ID","length_forward_mm":4,"targets":["OCCURRENCE-ID"]}}
{"command":"extrusion.set","arguments":{"container":"CUT-ID","length_forward_mm":2,"targets":["FIRST-OCCURRENCE","SECOND-OCCURRENCE"]}}
{"command":"assembly.cut.list"}
```

`targets` contains immediate inserted Part-occurrence IDs. Repeated Parts are distinct
targets. Internal subassembly Parts, whole subassemblies and derived occurrences cannot
be cut. Duplicate/unknown targets are rejected. Creation without `targets` selects all
current unsuppressed immediate editable Parts, as GUI does; editing omission retains
the list. Empty arrays save a cut affecting no occurrences. Part rejects this argument.

`combine` must be `subtract`. Other parameters share Part behavior: dimensions, Thin,
extent/direction, local sketch Revolution axis, numeric placement and original end
references. `targets_forward/reverse` use `instance_path` for exact original-face
occurrences and read saved reference geometry. Outputs add `targets`, `suppressed`.
One Undo step; source Parts are unchanged.

GUI/CLI use `commit_assembly_profile`. Identity, sketch ownership and targets validate
before commit. Dependency regeneration occurs in a prepared document copy, honoring
current unsaved open sources. Assembly commits only after every cut completes. Invalid
requests publish no intermediate regeneration state. Native documents retain input
bodies for Properties rollback.

### Complete profile-parameter persistence

New transaction tests exposed incomplete Assembly profile serialization. Part/Assembly
now share `save_profile_parameters` / `load_profile_parameters`, retaining two-sided
lengths/angles, Thin, sketch offsets, end conditions and original references. Assembly
also stores full existing placement/numeric locks through the Placement format. The
incomplete Assembly serialization branch was removed.

Extensions were unchanged. This milestone introduced Assembly INI **17**, internal **26**;
older versions were unsupported under project rules. Part remained **19/43** because
its fields were unchanged. Start Assembly/versioned test fixtures were updated. These
are historical versions; subsequent format changes are documented with their features.
All required data remains native.

Verification:

- Initial tests found incomplete serialization; corrected Extrusion/Revolution models
  check volumes for extents, Thin, source Parts and original end faces, not just counts.
- Integration **8/9 in 121.68 s** covered real CLI, bidirectional GUI Properties,
  history, templates and original references. The sole error expected a full through
  cut from a one-sided test starting inside a box. It now checks 30 mm³ one-sided
  and 60 mm³ two-sided separately.
- Corrected models, translations and both templates **4/4 in 13.93 s**
  (`build/assembly-profile-final-model-tests.log`). Native applications resaved both
  templates; Part retained every original value.
- Extra open-profile failure, nested-subassembly protection and saved-lock checks
  **1/1 in 0.83 s** (`build/assembly-profile-atomic-tests.log`).
- Final apps/all tests built, followed by full **136/136 in 540.06 s**, no failures
  (`build/assembly-profile-full-build.log`, `build/assembly-profile-full-tests.log`).

Cut ordering, suppression and deletion:
[ASSEMBLY_CUT_HISTORY_COMMANDS.md](ASSEMBLY_CUT_HISTORY_COMMANDS.md).

## Profile-sketch batch commits (2026-09-13)

`extrusion.sketch.edit` / `revolution.sketch.edit` edit the exact profile-container
sketch and calculate its body once, in Part and Assembly cuts. They share GUI Properties
commit, preserving owner, feature ID and target occurrences.

```json
{"command":"extrusion.sketch.edit","arguments":{"container":"FEATURE-ID","operations":[{"command":"sketch.point.move","arguments":{"point":"POINT-1-ID","position":[12,0]}},{"command":"sketch.point.move","arguments":{"point":"POINT-2-ID","position":[12,8]}}]}}
```

`operations` contains 1–1000 available sketch-edit commands. Inner commands omit `sketch`
and `document`, targeting only this sketch's private working copy. Document commands
such as `save` are unavailable. Original external references and exact occurrence paths
are supported. Inner failures return zero-based `operation_index`. Batch/final-profile
calculation errors publish nothing.

Success returns profile data and ordered `results`, `changed`, `body_calculated`.
Changed batches have one Undo/Redo step. Identical final state calculates nothing and
adds no history. Standalone `sketch.*` still changes only sketches under its existing
contract.

Verification passed model **4/4 in 2.27 s**, final CLI/model integration **9/9 in
29.29 s**, GUI console/translations **2/2 in 70.63 s**. Both apps/all tests built.
Checks cover volumes for all four combinations, open-profile errors, batch boundaries,
unchanged history on failure/no-op, exact-occurrence original references and native
saving after Undo/Redo. Logs: `build/profile-sketch-batch-integration-build.log`,
`build/profile-sketch-batch-integration-tests.log`, `build/profile-sketch-batch-gui-tests.log`.
