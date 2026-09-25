# Sweep commands

Properties support includes `sweep2d.get/set`, `sweep3d.get/set`, `helical.get/set`.
GUI creation/edit confirmation and commands share `workspace::commit_sweep`: validation,
explicit calculation and one Undo entry. `sweep2d.create`, `sweep3d.create`,
`helical.create` build from native inputs. 2D/3D `set` manage complete profile/station lists.

## Usage

```json
{"command":"sweep2d.get","arguments":{"container":"<ID>"}}
{"command":"sweep3d.set","arguments":{"container":"<ID>","result_type":"thin","thin_mode":"symmetric","thickness_mm":0.5}}
{"command":"helical.set","arguments":{"container":"<ID>","pitch_mm":10,"left_handed":true}}
```

Each `get` returns document/container/feature/owning-Body IDs, name, `add/subtract`
operation, locks, reference validity and revision. Reads consume saved models without
OCCT, sketch/cache changes. Optional `document` can read another open Part. During
Properties, reads return committed state rather than pending drafts.

2D/3D also return `result_type` (`solid/thin`), `thin_mode`
(`one_side/other_side/symmetric`), `thickness_mm`, profiles with IDs, station IDs,
incoming-branch flags, owned-sketch IDs and correspondence starts. 2D returns
`path_sketch` and optional `path_plane`; 3D its path ID. Helical returns `pitch_mm`,
`left_handed`, `circle`, `start_point`, `guide_start_point`, and three owned-sketch
IDs ordered as circle, radial guide, section. Existing Sketcher commands expose geometry.

## Changes and protections

`set` takes `container`, optional active-Part identity guard `document`, `name`,
`combine`, and `placement` with existing numeric placement fields. 2D/3D take the
three Thin fields above; Helical takes pitch, handedness, selected base circle/start
point. Thickness: 0.001–1000000 mm; pitch: 0.0001–1000000 mm. Unknown choices,
nonnumeric values, invalid geometry/references reject the complete request atomically.

Features must belong to the active editable Body. Pending GUI commands, foreign
documents, derived Bodies and locked values block edits. Container, feature, Origin
and embedded-sketch IDs are retained. Calculation uses existing placement and the
same rollback boundary as Properties. No Assembly regeneration or bypass of reference/
placement-lock protection occurs.

## Native-loading bug found

Helical loading reframed sketches before reading container placement, causing saved
bodies and reopened sketch frames to disagree for moved/rotated features. Reframing
now follows placement loading, as for 3D Sweep. This changes current-format read order,
not file structure/templates.

## Properties verification

Model tests compare independent volumes for straight solid/Thin sweeps and circular
helical sections. They check locks, ownership, rejected transactions, Undo/Redo,
translation/rotation, complete saved history and cold calculation. Real CLI exercises
all six commands without Widgets. GUI compares Properties/console, Cancel, OK, Undo
and saved volume.

- Targeted model/process/GUI: **9/9**, 120.57 s, `build/sweep-command-gui-tests.log`.
- Full Windows Release: **93/93**, 432.54 s, `build/sweep-command-full-tests.log`.
- Both programs/all tests built: `build/sweep-command-gui-build.log`.

Catalog at this stage: 170 commands. Overall CLI coverage is maintained in
[CAD_COMMAND_COVERAGE.md](CAD_COMMAND_COVERAGE.md).

## Embedded 3D path and stations

`sweep3d.set` accepts `path` with `curve_type` (`polyline/interpolating_spline`),
`rounding_enabled`, complete `points`. Points with `construction` retain native IDs;
others receive new IDs. Fields match standalone 3D Curves: `name`, `values`,
`radius_mm`, `tangent`, `tangent_enabled`. `values` coordinates are path-local. Whole-
Sweep placement remains on the container; no extra translation is inserted into the path.

```json
{"command":"sweep3d.set","arguments":{"container":"<SWEEP>","path":{"curve_type":"polyline","rounding_enabled":true,"points":[{"construction":"<FIRST_POINT>"},{"values":{"x":0,"y":0,"z":20},"radius_mm":5},{"construction":"<LAST_POINT>","values":{"x":10,"y":0,"z":20}}]}}}
```

The list is the entire proposed state: omitted points and their profiles are removed,
as in Properties. If this makes the feature uncalculable, such as losing the required
first profile, reject everything. Surviving point/profile IDs remain. Existing point
locks/references still apply to complete-list replacement.

`construction.list/get` includes embedded paths/points. `owning_feature` identifies the
Sweep; `parent`, `body`, `coordinate_owner` define hierarchy. Path coordinates use
`container`; points use `parent_construction`. Reads calculate/create nothing. Direct
`construction.set` redirects embedded-object edits to the owning Sweep command so path
changes always commit a complete valid feature.

`sweep3d.get` adds `stations`: native point ID, incoming/outgoing branch, active state,
local position, tangent and optional owned profile. `station_coordinate_owner` is path
ID. `stations_valid`/`stations_error` permit reading damaged paths without repair or
regeneration.

Model coverage checks lengthened volume, straight interpolating splines, rounded arcs
with independently calculated lengths, embedded point IDs, own-plane references with
simultaneously moved/rotated Sweep and Body, damaged-path reads and atomic duplicate/
foreign-point rejection.

Targeted suites **7/7** (61.86 s), `build/sweep-path-gui-tests.log`: 3D Sweep core,
translations, real CLI, construction/Sweep commands, catalog, console with nested Point
Properties. Additional Body-frame/invalid-path checks **1/1** (4.99 s),
`build/sweep-path-frame-tests.log`. Apps/tests built in `build/sweep-path-gui-build.log`.
Catalog remained 170; native format/templates unchanged.

## Creating 3D Sweep from native inputs

`sweep3d.create` takes a standalone 3D Curve (`source_path`) and one or more standalone
sketches (`profiles`). Profiles specify `sketch`, native path `point`, optional `incoming`
and correspondence `start_point`. Existing construction/sketch commands create inputs;
no internal serialization editing is needed.

```json
{"command":"sweep3d.create","arguments":{"source_path":"<CURVE_3D>","name":"Sweep","profiles":[{"sketch":"<SKETCH>","point":"<FIRST_PATH_POINT>"}],"result_type":"solid"}}
```

Creation shares operation, name, Thin and placement parameters with editing. The new
Sweep inherits path placement. Profile-local 2D curves/constraints remain; its station
defines new placement. One Sweep owns the path/sketches, and commit removes standalone
input containers. Path, point, sketch and curve IDs remain. Sweep container, feature
and profile-to-station binding get new IDs. Undo restores independent inputs and their
original placements; Redo restores the identical Sweep.

Inputs must belong to the active editable Body before its cursor. Each profile sketch
can be consumed once. Reject inputs needed by another object, interdependent inputs or
profile references to sketch containers being removed. References to earlier unconsumed
objects remain. Checks, calculation and input removal form one atomic transaction;
errors cannot leave removed sketches or partial features.

GUI commit/creation share owned-path setup: transforms stay on the container and local
points in the path, avoiding double translation/rotation. Format/templates unchanged.

Models check actual volume/spatial bounds of rotated sweeps, original curve IDs,
dependent-object protection, foreign stations, exact Undo restoration and sole ownership
after saving. R2→R3 circles over 20 mm match frustum volume 380π/3 mm³. An open 4 mm
segment survives invalid Solid creation; subsequent 0.5 mm Thin over 20 mm gives 40 mm³.
Coverage includes suppressed inputs, inactive Bodies and point references to path Origin.

Real CLI creates, undoes and resaves consumed Sweeps. GUI opens Properties, edits embedded
points, distinguishes parent Cancel/OK and checks Undo/saved volume. Related suites:
15 tests passed (126.44 s), `build/sweep-create-gui-tests.log`.

Regression also found missing mapping of embedded point/sketch reference ownership to
Sweep containers: `history.can_move` incorrectly permitted moving dependents before
sources. Shared dependency collection now registers paths, points/Origins and all owned
sketches. Queries/moves reject invalid ordering without data/cache changes. Baseline:
`build/sweep-owned-dependency-red-tests.log`; corrected expanded models both passed
(5.53 s), `build/sweep-create-profiles-tests.log`. History deletion rules are unchanged.

Final apps/all tests built (`build/sweep-create-full-build.log`). Full run **91/93**
(478.27 s, `build/sweep-create-full-tests.log`): Measurement Inspector exceeded 90 s
and Assembly-profile GUI confirmation failed once. With identical binaries, profile
passed independently (89.51 s), then both affected tests **2/2** (90.14 s,
`build/sweep-create-ui-recheck-tests.log`). Intermittent GUI causes are unproven; the
original full run is not claimed fully passing. New Sweep geometry/CLI/GUI tests passed.

## Complete profile lists for 2D and 3D Sweep

`profiles` in `sweep2d.set` and `sweep3d.set` replaces the complete profile list.
An omitted existing profile is removed. An entry with `profile` retains its
Sketch and identity; `point`, `incoming` and `start_point` may change. An entry
with `sketch` and `point` consumes a standalone Sketch under the same rules as
3D Sweep creation. The list must contain 1–5000 entries, with a usable profile
at the first valid station. A feature cannot be saved without a calculable start.

```json
{"command":"sweep2d.set","arguments":{"container":"<SWEEP>","profiles":[{"profile":"<FIRST_PROFILE>"},{"sketch":"<STANDALONE_SKETCH>","point":"<END_POINT>","incoming":true}]}}
{"command":"sweep3d.set","arguments":{"container":"<SWEEP>","profiles":[{"profile":"<FIRST_PROFILE>"},{"profile":"<OTHER_PROFILE>","point":"<OTHER_POINT>","incoming":false,"start_point":"<BOUNDARY_POINT>"}]}}
```

Point IDs and `incoming` come from `stations` in `get`. 2D Sweep also returns
these stations, their positions, tangents and profiles. 2D station coordinates
belong to the body (`station_coordinate_owner`); 3D stations are local to the
embedded path. This is a pure ZIMA geometry query without OCCT, frame changes
or cache changes. Invalid paths return `stations_valid:false` and diagnostics
without hiding stored parameters.

`start_point` selects an actual native boundary correspondence point; an empty
string restores automatic selection. Circles use their C points: Sketch points
with an actual `point_on_circle` constraint. No synthetic sequential IDs are
created. Owned profile curves remain accessible through Sketcher commands.

A new standalone Sketch may precede or follow the Sweep but must not depend on
it or later history. Consuming it must not remove an input used by another
object. After consumption, the validation boundary is located again by Sweep
ID because removing an earlier Sketch container changes the history index.
Geometry, input removal and profile list commit as one Undo entry; errors
cannot save a partial change.

Process and model regressions passed **2/2** (17.19 s,
`build/sweep-profiles-process-tests.log`): Sketch consumption, independently
calculated frustum volume, Undo restoring standalone inputs, original IDs,
duplicate/invalid station rejection, profile removal, real C points, persisted
correspondence and protection against dependence on the owning Sweep.

Final profile-management verification:

- **15/15** related model, CLI and GUI tests (128.34 s),
  `build/sweep-profiles-gui-tests.log`; both executables built in
  `build/sweep-profiles-gui-build.log`. Moving an R3 profile halfway along a
  20 mm path preserves its ID and gives an independently verified volume of
  460π/3 mm³ (frustum in the first half, R3 cylinder in the second). GUI opens
  the consumed circle in its original Sketch; confirmation does not duplicate inputs.
- An additional regression showed that a 3D profile could be moved to the
  nonexistent incoming branch of the first point and ignored during calculation
  (`build/sweep-profile-station-red-tests.log`). New or moved attachments now
  require an active station, including `sweep3d.create`. Existing inactive
  profiles remain stored when path rounding changes.
- After the fix, **5/5** final command, process, GUI and localization tests
  passed (53.86 s), `build/sweep-profiles-final-tests.log`; both executables and
  all tests built in `build/sweep-profiles-final-build.log`. The catalog remained
  at **171 commands**: existing `get/set` commands were extended. Formats and
  templates were unchanged.

## 2D Sweep path plane

`sweep2d.set path_plane` accepts an original `owner`, semantic `key`, optional
empty `instance_path` and `offset_mm` within ±1,000,000 mm. An empty object
`{}` removes the explicit reference and restores the local path plane using
normal Properties behavior. Omitting the offset for an unchanged reference
preserves its value; a new reference defaults to zero.

```json
{"command":"sweep2d.set","arguments":{"container":"<SWEEP>","path_plane":{"owner":"<SWEEP_ORIGIN>","key":"origin:plane:xy","offset_mm":7}}}
{"command":"sweep2d.set","arguments":{"container":"<SWEEP>","path_plane":{"owner":"<ORIGINAL_OBJECT>","key":"<PLANAR_FACE_KEY>"}}}
{"command":"sweep2d.set","arguments":{"container":"<SWEEP>","path_plane":{}}}
```

Sources may be one of the three planes of the feature's own Origin, an available
main/body Origin, or an earlier original plane or planar face in this Part.
The result body, foreign occurrences, the feature's own result face and later
history are invalid sources. Shared CLI/GUI commit validates history order.
The existing native path-plane solver determines plane type and geometry;
CLI adds neither a separate picker nor OCCT traversal.

A construction plane includes its own offset. Additional `offset_mm` belongs
to the path plane. Its reference and offset can change in the same transaction
as container placement; profiles are reframed onto the resulting path. Source
changes affect body geometry on explicit calculation. `get` reads the stored
reference and stations without regeneration. An invalid partial request also
prevents other simultaneously requested changes from committing.

The first model run passed **1/1** (6.47 s),
`build/sweep-path-plane-model-final-tests.log`: a construction plane at z=10 mm,
with its own 3 mm offset and a 2 mm path offset, gives z=15 mm. After changing
the source and regenerating, z=18 mm. The calculated body's viewer geometry
also verified an own plane rotated 30° and offset 4 mm. Saving retains original
IDs, offsets and Sketch frames. Reference format remains original owner, key,
occurrence and offset; no new persistent fields or template changes.

The expanded suite passed **14/15** cases (126.33 s,
`build/sweep-path-plane-gui-tests.log`), including GUI and standalone CLI.
The last model scenario exposed test setup errors: string dimensions in
model creation, confusing an analytic plane's axis with a particular face's
orientation, and attempting to change constrained body Z directly. The corrected
test selects the actual highest horizontal face and moves the body using its
existing XY mate offset. The complete model test then passed **1/1** (6.73 s),
`build/sweep-path-plane-body-tests.log`: an original face of another body,
compensation for its 10 mm movement in local coordinates, edge-as-plane
rejection, preserved volume and exact native persistence.

Both executables and all test programs were built
(`build/sweep-path-plane-gui-build.log`); final changes affected only that test
scenario (`build/sweep-path-plane-body-build.log`). Catalog: **171 commands**.

## Creating 2D Sweep from Sketches

`sweep2d.create` consumes a standalone path Sketch (`source_path`) and a
`profiles` array with the same contract as 3D Sweep. A station point is an
original Sketch point ID, available through `sketch.entities` /
`sketch.entity.get`. As in GUI, the 2D path must be a connected open curve
starting at local `(0, 0)`. The first profile belongs to that station with
`incoming: false`; the end of a segment uses `incoming: true`. After creation,
`sweep2d.get` returns all stations and these flags.

```json
{"command":"sweep2d.create","arguments":{"source_path":"<PATH_SKETCH>","profiles":[{"sketch":"<PROFILE_SKETCH>","point":"<PATH_POINT>"}],"name":"Sweep"}}
```

Optional arguments: `name`, `combine`, `placement`, `result_type`, `thin_mode`,
`thickness_mm`, `path_plane` and the target `document` guard. `path_plane` follows
the editing rules; the default retains the source Sketch's physical plane and
offset. Unless explicitly overridden, the source path container's placement,
including live references, is inherited.

All inputs must be standalone, active and in the body being edited, before the
history cursor. No other feature may require them, and they must not depend on
a consumed Sketch container. Consumption removes the input root containers,
preserves Sketch and local geometry identities, and creates a new owning Sweep
ID. Profiles orient by station as in GUI. Errors leave the document unchanged;
Undo restores the original inputs and history.

A referenced Sketch uses quarter-turns about local Y; a normal container uses
Z. Sweep creation converts this local quarter-turn into a correction on the
new feature; original references remain live. The shared placement solver is
unchanged. Path-plane offset is stored in the existing own-Origin plane
reference. Format and start templates are unchanged.

Initial model verification passed **1/1** (10.70 s),
`build/sweep2d-create-model-tests.log`: 72 combinations of XY/XZ/YZ, zero/one/three
plane references, FRONT/BACK and quarter-turns 0–3, corrections and a rotated
body. Actual path origins/axes are compared before and after consumption;
independent cylinder volume is `π × 2² × 20 = 80π mm³`. Checks cover exact Undo,
atomic rejection of incorrect/shared/inactive inputs, native save and fresh
calculation. Follow-up coverage adds an arc path, live offset, real CLI process
and GUI Properties.

Final verification of this stage:

- Both executables and all tests built: `build/sweep2d-create-full-build.log`.
- Related regressions **14/15** (133.92 s), `build/sweep2d-create-gui-tests.log`.
  Real CLI, GUI creation and subsequent Properties (OK/Cancel), other Sweep
  types, history, placement, localization and native documents passed.
- The new arc test initially supplied the endpoint against the path direction
  as the start branch. Correcting the input to the actual native station gave
  a complete model pass **1/1** (11.72 s),
  `build/sweep2d-create-final-tests.log`; production code did not change between runs.
- The model test also changes a live reference offset after consumption. For
  an R10 quarter-circle and R2 section, independent volume is `20π² mm³`;
  symmetric Thin 0.5 mm gives `10π² mm³`. Checks cover original arc, ownership,
  saving and cold calculation.

Catalog at this stage: **172 commands**. Helical Sweep creation was next.

## Creating Helical Sweep

`helical.create` consumes three standalone Sketches. `base_sketch` contains
`circle` and its `start_point`. `guide_sketch` contains the radial guide path
starting at `guide_start_point` at local `(0, 0)`. `profile_sketch` is a closed
section. All input Sketch, point and curve IDs remain original; their common
container receives a new ID.

```json
{"command":"helical.create","arguments":{"base_sketch":"<BASE>","guide_sketch":"<GUIDE>","profile_sketch":"<PROFILE>","circle":"<CIRCLE>","start_point":"<POINT_ON_CIRCLE>","guide_start_point":"<GUIDE_ORIGIN>","pitch_mm":5,"left_handed":false}}
```

Optional arguments: `name`, `combine`, `placement`, `pitch_mm`, `left_handed`,
`base_offset_mm` and `document`. Units are mm/degrees; pitch range is
0.0001–1,000,000 mm. Existing Helical Sweep geometry rules apply, including
monotonic height, connected radial guide, section and turn-count limits; see
[HELICAL_SWEEP.md](HELICAL_SWEEP.md). There is no separate Thin switch. A hollow
section is defined by the profile Sketch.

Inputs must be distinct standalone Sketches of the active editable body,
before the history cursor. Other containers must not need them, and they must
not depend on each other. Calculation, consumption and removal of original
root containers form one transaction. Failure preserves document, cache and
Undo; one Undo after success restores the three original Sketches.

The feature frame is inherited from the base Sketch container, including live
references, FRONT/BACK and quarter-turns. Radial guide and section orient
according to the Helical Sweep definition, as in GUI editing. `base_offset_mm`
belongs to the winding's base plane; it moves the base Sketch and derived
winding, not the container. If omitted, the source Sketch's offset is retained.

`helical.get` returns the offset, `helical.set` changes it and Helical Sweep
Properties exposes the same field. It respects the `base_offset` lock, uses
OK/Cancel and persists in the base Sketch's existing `.prtz` field.
`helical.set guide_start_point` also changes the selected original radial
path start point with normal geometry validation. No format/template changes.

The shared placement solver and base-reframing function remain unchanged.
Offset preservation is confined to Helical Sweep. Source Sketch orientation
conversion reuses the unchanged function used by 2D Sweep creation.

Both executables and all tests built (`build/helical-create-full-build.log`);
the related suite passed **15/15** (142.88 s,
`build/helical-create-gui-tests.log`). Model coverage:

- 72 combinations of XY/XZ/YZ, zero/one/three plane references, FRONT/BACK,
  quarter-turns and corrections; source and owned-Sketch physical frame comparison.
- Original IDs of all three Sketches; one Undo restores original native inputs.
- Independent volume for R10 winding, height 10 and circular section R0.5:
  `π × 0.5² × sqrt((2π × 10 × 10 / pitch)² + 10²)`.
  Pitch 5 and 10 mm, both handedness values; relative tolerance 0.1%.
- Offset 3 → −5 mm in a rotated body and with BACK moves the base plane −8 mm
  along its normal while preserving container placement and volume.
- Missing/duplicate/suppressed/inactive/shared inputs, invalid circle/path start,
  value range, offset lock, exact save and cold calculation.

Real CLI and GUI consume Sketches with a source offset of 3 mm; after editing,
they save the plane at z=7 mm and check actual saved volume. During development,
the model test needed corrections to source-Sketch access after atomic document
restore and an incorrect assumption of consecutive revision numbers after Undo.
These test corrections did not change production behavior.

Catalog at this stage: **173 commands**.

An additional GUI return-from-base-Sketch test passed **1/1** (38.31 s),
`build/helical-create-sketcher-tests.log`. It checks the original circle in the
View at z=7 mm, return to pending Properties without premature commit, then
OK/Cancel. `helical-offset-properties.png` shows the expanded Properties.
Production code did not change after the 15/15 pass; only this focused GUI
regression was added. Final application build:
`build/helical-create-sketcher-build.log`.

## Placement references (2026-09-14)

`sweep2d.reference.set`, `sweep3d.reference.set`, `helical.reference.set` and
general `placement.reference.set` attach original references to existing Sweeps
through the shared GUI/CLI commit. Full arguments, ownership rules and tests:
[SWEEP_REFERENCE_COMMANDS.md](SWEEP_REFERENCE_COMMANDS.md).
