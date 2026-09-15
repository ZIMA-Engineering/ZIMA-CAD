# Standalone drill-point commands

`drill_point.create/get/set` manages current native **DrillPoint**. One operation
subtracts tips from one or more circular hole bottoms, each supplying its diameter,
center, and direction into material. Included angle is shared. GUI Properties and
CLI use `commit_drill_point`.

## Example

Obtain original bottom references from `reference.list/get`, supplying exact owner
ID and semantic key. Tree labels and face order are not identifiers.

```json
{"command":"drill_point.create","arguments":{"faces":[{"owner":"OPENING-ID","key":"ORIGINAL-BOTTOM-KEY"}],"angle_degrees":118}}
{"command":"drill_point.get","arguments":{"container":"TIP-ID"}}
{"command":"drill_point.set","arguments":{"container":"TIP-ID","angle_degrees":120}}
{"command":"drill_point.set","arguments":{"container":"TIP-ID","faces":[{"owner":"OTHER-OPENING-ID","key":"ORIGINAL-BOTTOM-KEY"}]}}
```

`create` requires nonempty `faces`; `set` requires `container` and at least one changed
parameter. Optional `name` is nonempty; `document` identifies the active Part. `get`
returns document/container/feature/body IDs, name, angle, references, locks, and revision
without geometry calculation.

`angle_degrees` is a JSON number from 1 to 179, default 118. References have textual
`owner`/`key` and optional empty `instance_path`. Only local original faces preceding
the feature are accepted, each bottom once, at most 10000 references. Input includes
no substitute analytic geometry.

Editing `faces` replaces the complete list, supporting addition, replacement, reordering,
and removal. Empty lists on existing features preserve identity and disable removal,
as clearing all bottoms in Properties does. Empty creation is rejected. Any invalid
face rejects the whole commit rather than cutting only valid bottoms.

Included-angle lock uses Properties key `angle`. Derived bodies are not directly
editable; editing requires the tip's Body active. Success creates one Undo step;
identical settings do not recalculate. GUI preview changes commit only on OK; Cancel discards them.

## Generated geometry identity

Tip face/rim-edge identities contain owner, semantic role, and exact original bottom
reference, never bottom order or OCCT enumeration. Removing the first bottom therefore
does not rename the second tip. Sweep end faces use the same principle.

Keys are `drill-point:ROLE:from:LENGTH:OWNER:BOTTOM-KEY`. `LENGTH` is owner-ID byte
length; bottom keys may contain separators. Roles are `side`, `base`, or `base-circle`.
`kernel::drill_point_source` recovers parent references without OCCT. New calculations
do not create order-based numerical keys.

Native structure/extensions remain unchanged. Start templates contain no drill tips
and need no geometry conversion. Missing-source regeneration retains existing rules;
explicit editing accepts only available valid bottoms. Standalone Hole and other
operations remain in the broader CLI plan at this stage.

## Verification

Model tests use two blind holes of different diameters, comparing removed volume with
summed cone volumes. They check cone-surface points, ancestry, angle changes, list
reordering/removal/emptying, locks, atomic errors, Undo/Redo, and native save with fresh
calculation. Initial run passed **1/1** (0.47 s):
`build/drill-point-command-model-build.log`, `build/drill-point-command-model-tests.log`.

Process/GUI tests add actual CLI creation/editing, saved-model reading, Properties
entry, OK/Cancel, and removal of one bottom in the dialog.

Both programs built and related tests passed **11/11** (88.78 s):
`build/drill-point-command-full-build.log`, `build/drill-point-command-related-tests.log`.
Coverage includes base geometry, 3D Sweeps, existing holes/threads, GUI, translations,
locks, and actual CLI. Calculation fingerprints now include topology-identity version
so explicit calculation cannot adopt derived cache with order-based keys. Displaying
saved models triggers no calculation.

Final build/full suite passed **97/97** (458.94 s) without reruns:
`build/drill-point-command-final-build.log`, `build/drill-point-command-full-tests.log`.
An additional model scenario using a bottom from current `opening.create` passed
**1/1** (0.53 s): `build/drill-point-opening-build.log`, `build/drill-point-opening-tests.log`.
It checks actual removal and the new cone's parent link to the original Opening
bottom. Only tests changed after the full run.
