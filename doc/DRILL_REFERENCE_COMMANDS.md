# Assigning Hole and Opening references

`hole.reference.set` and `opening.reference.set` assign an original placement
reference to an existing hole. `placement.reference.set` also accepts these types
using `object` instead of `container`.

```json
{"command":"hole.reference.set","arguments":{"container":"HOLE_ID","index":0,"reference":{"owner":"PART_ID:origin","key":"origin:plane:xy"},"offset_mm":-20}}
```

Arguments follow the other reference-assignment commands:

- `container`: an existing Hole or Opening in the active Part;
- `index`: positional rows 0-2, FRONT/TOP rows 3-4;
- `reference`: original `owner`, `key`, and optional empty `instance_path`;
- `offset_mm`: signed distance from a planar reference, default 0;
- `flip`: reference reversal, default `false`;
- `derive_orientation`: ordinary automatic orientation completion, default `true`;
- `document`: optional active-document guard.

The source must precede the feature in history or be an available Origin. The hole's
own profile/result, later objects, missing geometry, and foreign occurrences are
rejected. The active Body must be writable. Normal open-editor guards apply.

The command reuses `prepare_part_feature_reference` and approved
`assign_placement_reference`; shared placement solving is unchanged. Existing
`commit_hole` / `commit_opening` performs the same commit as Properties OK:
explicit body calculation and one history transaction. Failure preserves document
and calculated data. Identical requests create neither history nor a new body.
Owned circles, Sketches, chamfers, and drill tips retain identity; Opening also
retains its thread surface.

The first planar reference is FRONT. Drilling direction follows each existing
feature: referenced Hole uses local +Y, Opening local -Y. With FRONT=XY, the test
drills a block from z=-20 for Hole and z=+20 for Opening. Choose entry side and
any reversal from the preview, as in the GUI. Commands do not override these conventions.

The response includes document, container, coordinate system, current persisted
placement/references, revision, `changed`, and `body_calculated`. Existing `hole.get`
and `opening.get` return other dimensions. Data uses existing `.prtz`; format and
templates are unchanged.

Regression checks analytical removed cylinder volume and actual wall coordinates
after a 4 mm hole translation. It covers Hole, plain Opening, metric, Whitworth,
and pipe threads, rejected references, no-op requests, Undo/Redo, and native saving.
The process test uses the actual CLI. The GUI test opens the same Properties and
checks the reference row, Cancel, OK, Undo/Redo, and persisted volume.
