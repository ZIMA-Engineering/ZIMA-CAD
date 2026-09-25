# External-thread commands

`shaft_thread.create/get/set` manages current native **ShaftThread**, adding a
technological root surface and optional runout without changing shaft volume or
cutting a helical thread profile.

GUI Properties and CLI share `commit_shaft_thread`, original references before the
editing boundary, and catalog selection. Calculation occurs on commit. `get` only
reads persisted data; identical `set` changes neither revision nor body calculation.
Actual changes create one Undo step.

## Example

Create a Part with `new part shaft`, then extrude a circular Sketch. Replace
`SKETCH-ID` with the ID returned by `sketch.create`:

```json
{"command":"sketch.create","arguments":{"name":"Shaft profile","plane":"XY"}}
{"command":"sketch.circle.create","arguments":{"sketch":"SKETCH-ID","center":[0,0],"radius_mm":5}}
{"command":"extrusion.create","arguments":{"sketch":"SKETCH-ID","length_forward_mm":30}}
```

Use `reference.list` to obtain the resulting Extrusion's original cylindrical
face and planar start/end faces. Copy their returned owner and semantic keys;
these keys derive from the authored Sketch and must not be guessed from face
positions. Replace the placeholders below with those reference identities:

```json
{"command":"shaft_thread.create","arguments":{"cylinder":{"owner":"EXTRUSION-ID","key":"CYLINDRICAL-FACE-KEY"},"start":{"owner":"EXTRUSION-ID","key":"START-FACE-KEY"},"designation":"M10","length_mm":15}}
{"command":"shaft_thread.get","arguments":{"container":"THREAD-ID"}}
{"command":"shaft_thread.set","arguments":{"container":"THREAD-ID","length_mm":20,"root_diameter_mm":8.05}}
{"command":"shaft_thread.set","arguments":{"container":"THREAD-ID","end_condition":"up_to","end":{"owner":"EXTRUSION-ID","key":"END-FACE-KEY"}}}
```

## Parameters

Dimensions are JSON numbers in mm, independent of display units. `create` requires
`cylinder` and `start`; `set` requires `container` plus at least one parameter.
Optional `document` identifies the Part.

| Parameter | Meaning |
| --- | --- |
| `name` | Nonempty name; default name is localized |
| `standard` | `metric`, `whitworth`, `pipe` |
| `designation` | Exact shared `thread.catalog` designation |
| `root_diameter_mm` | Custom root diameter |
| `length_mm` | Length from start face, not entry-chamfer end |
| `end_condition` | `length`, `up_to`, `through_all` |
| `runout_enabled` | Runout, allowed only with length termination |
| `runout_pitch_factor` | Nonnegative pitch multiple for runout |
| `cylinder` | Original external shaft cylinder |
| `start` | Original planar start face |
| `chamfer` | Optional original conical entry-chamfer face |
| `end` | Original plane/cylinder/cone for `up_to` |

References contain textual `owner`/`key` and optional empty `instance_path`. This stage
accepts local original Part faces, rejecting result geometry, missing faces, later
features, and other occurrences. `{}` clears optional `chamfer`/`end`, but mandatory
faces cannot be cleared and committed. `get` returns absent optional references as
`null`. Input contains no analytic substitute geometry.

Standard/size changes adopt catalog nominal diameter, pitch, and **external** root
diameter; explicit `root_diameter_mm` takes precedence. Changing standard without size
retains an available designation or selects its first entry as Properties does.
Dimension locks prevent indirect catalog changes in CLI and GUI commit. Properties
allows explicit unlocking before commit.

`up_to`/`through_all` disable runout; explicit `runout_enabled: true` with them is an
error. Length-mode runout respects remaining shaft length. Existing geometry rules
calculate root-surface intersections with entry/end chamfers. Independently chosen
root surfaces outside shaft material remain supported.

`get` additionally returns document/container/feature/body IDs, nominal diameter,
pitch, locks, and revision. Editing preserves identities. Invalid requests change
neither document, calculated geometry, nor Undo. Derived bodies are not directly
editable; editing requires the thread's owning Body active.

## Scope and verification

Native format, start templates, and general placement are unchanged. Missing-source
regeneration keeps existing rules. This stage exposes existing Thread/Properties
through CLI; standalone Hole, DrillPoint, and opening children remain planned at this stage.

Model tests check unchanged shaft volume, exact root/runout radii and endpoints,
reversed direction, chamfers, termination, catalog selection, locks, reference errors,
Undo/Redo, and native save with fresh calculation. Process/GUI tests check actual
console creation/editing, OK/Cancel, Undo, and catalog equivalence.

Both programs/all tests built. Related regression passed **9/9** (70.03 s):
`build/shaft-thread-command-full-build.log`, `build/shaft-thread-command-related-tests.log`.
Final review also moved lock validation into shared commit so GUI catalog cannot
bypass it. Afterward, rebuild and **3/3** passed (57.52 s): model, actual CLI, and GUI
including rejected locked-diameter confirmation. Logs:
`build/shaft-thread-command-final-build.log`, `build/shaft-thread-command-final-tests.log`.

The first model attempt corrected a wrong M12 expectation: 10.106 mm is internal
root diameter; external is 9.853 mm. Adding a chamfered-shaft fixture also required
correcting path const qualification. Catalog and geometry rules did not change for
these fixture errors. Tests additionally cover Whitworth/G, chamfer endpoints
0.08/29.92 mm, and later-feature reference rejection.

Subsequent cleanup removed two full reference-packet copies on Properties opening
that were immediately overwritten by shared-filter output. Picking, rollback, and
reference solving are unchanged. Build passed; thread model **1/1** (0.29 s) and
GUI console **1/1** (43.86 s) passed. Logs:
`build/shaft-thread-reference-copy-build.log`, `build/shaft-thread-reference-copy-tests.log`,
`build/shaft-thread-reference-copy-gui-tests.log`.
