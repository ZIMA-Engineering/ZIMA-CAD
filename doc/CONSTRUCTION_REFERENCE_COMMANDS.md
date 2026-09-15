# Original construction-container references through CLI

`construction.reference.set` assigns an original reference to an existing independent
point, axis, plane, entire 3D-curve container, or its owned point. It uses the approved
shared position/FRONT/TOP helper and `commit_construction`, also used by construction
Properties OK. Standalone constructions do not recalculate bodies. Sweep3D-owned
points commit the whole Sweep through its shared transaction; see
[SWEEP_POINT_REFERENCES.md](SWEEP_POINT_REFERENCES.md).

```json
{"command":"construction.create","arguments":{"kind":"plane","name":"Referenced plane","base_plane":"xy"}}
{"command":"construction.reference.set","arguments":{"construction":"CONSTRUCTION-ID","index":0,"reference":{"owner":"DOCUMENT-ID:origin","key":"origin:plane:xy"},"offset_mm":7}}
{"command":"construction.get","arguments":{"construction":"CONSTRUCTION-ID"}}
{"command":"placement.set","arguments":{"object":"CONSTRUCTION-ID","values":{"reference_offset:0":13}}}
```

`index` 0–2 identifies position fields, 3 FRONT, and 4 TOP. `reference` contains
`owner`, `key`, and optional `instance_path`: actual original IDs from persisted
geometry. A construction point uses its stored Origin (`origin` from
`construction.get`) and key `point`; a construction plane entity uses `entity`
and key `plane`. Tree names and face indexes are not references.

Part accepts local references. In Assembly, `instance_path` identifies the exact
occurrence relative to the source Assembly owning the construction. Repeated Parts
must remain distinct, with every nested path level preserved. Lookup uses persisted
scene geometry without OCCT or source-Part mutation.

`offset_mm` is a finite signed plane-position offset. Orientation and non-offset
references accept only zero. `flip` retains existing GUI orientation-flip rules.
`derive_orientation` defaults to true: plane position references populate independent
FRONT/TOP fields. The position row itself gains no additional rotational meaning.
After all translational degrees of freedom are removed, another position field
may define direction under the existing Properties contract.

Replacing a locked position field preserves the actual measured distance. Transient
`measured_offset` is removed after assignment; persisted `offset` and its lock remain.
At this stage, a Plane's first plane reference selects base plane XZ as GUI does.
Explicit FRONT/TOP remain independent. Later work-plane selection is documented in
[WORK_PLANES.md](WORK_PLANES.md).

Own/later constructions, later model features, missing sources, duplicate references,
invalid fields, and unsolvable placement are rejected before mutation. Inactive
and derived bodies retain their protections. Open GUI dialogs block commands;
optional `document` guards against active-tab changes.

One change creates one Undo step. No-op assignment adds no history;
`body_calculated` is false for standalone constructions. Returned properties match
`construction.get` plus `changed`. Changes persist in existing `.prtz` / `.asmz`,
with unchanged format and extensions.

Address a standalone 3D-curve point by its own ID from `construction.get`. Coordinates
are local to the parent curve, whose ID is returned as `coordinate_owner`. Allowed
sources include the parent frame and points/axes/planes of earlier points. The full
curve's result edge, the point's own point/frame, and later points are forbidden.
Preceding Part geometry and exact Assembly occurrences remain available.

Solving first removes old owned-point frames from working inputs. After each point
is solved, its complete current local frame (point, axes, planes) becomes available
to later points. Embedded Sweep3D paths use the same process. Missing sources retain
the last saved position and mark the reference invalid; stale later-point frames
are never substituted.

Embedded-path point reference assignment is implemented through the same command.
Actual changes recalculate the Sweep and return `body_calculated: true`. Point values
use `sweep3d.set` and the complete `path.points` list. General
`placement.get/set/reference.set` still addresses Sweep placement and standalone
constructions. [placement.reference.remove](PLACEMENT_REFERENCE_REMOVAL.md) covers
standalone and embedded path points. File formats are unchanged.

## Verification (2026-09-14)

Part/Assembly model tests cover points, axes, planes, root 3D curves, distance locks,
FRONT/TOP, failures without changes, Undo/Redo, and native saving. A separate repeated/
nested-occurrence test passed **1/1 in 0.26 s**; replacing the same face between
occurrences moves the construction by an independently known 30 mm. Related integration
passed **11/11 in 106.48 s**, including actual CLI and GUI Properties Cancel/OK,
Undo/Redo, and saving. Calculated bodies and fingerprints are checked for preservation.
Both applications and all targets built.

## Curve points and current-frame repair (2026-09-14)

The original defect was reproduced by changing the first point's offset from 2 to
4 mm: its dependent fourth point incorrectly stayed at 2 mm. The repaired resolver
passes each freshly solved complete frame to later points. Verification covers
point/axis/plane chains, parent/child rotation, rotated bodies, own/forward-source
rejection, and retained last positions for missing references. Embedded Sweep tests
check actual cylindrical Sweep volume 4π√909 mm³, native saving/loading, and repeated solving.

GUI edits a child inside curve Properties: Cancel restores original state, child
OK commits only to the parent's proposal, and parent OK changes the document.
Undo/Redo and saving preserve local coordinates and original identities. Repeated-
occurrence tests include a child of a rotated curve in a nested Assembly.

Both applications and all test targets built in Windows Release. Full regression
passed **155/155 in 630.24 s**, including actual CLI, GUI console, and main workspace
window. The catalog remains at **288 commands** at this stage; existing assignment
coverage expands. Logs: `build/curve-reference-final-build.log`,
`build/curve-reference-full-tests.log`.
