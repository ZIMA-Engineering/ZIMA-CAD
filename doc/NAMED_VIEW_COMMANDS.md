# Named views in GUI and CLI

Implementation status: 2026-09-15. Named Part/Assembly views store complete camera
state. The original dialog wrote only pan and scales, losing rotation after reopening.
Assembly also read `named_views` but did not write it.

## Commands

| Command | Required arguments | Result |
| --- | --- | --- |
| `view.named.list` | none | Ordered `views` |
| `view.named.get` | `name` | One complete `view` |
| `view.named.set` | `name`, `camera` | Store/replace, `changed` |
| `view.named.delete` | `name` | Remove, `changed` |

All accept optional `document`. Queries may read any open Part/Assembly; writing
belongs to the active document. Activated subassemblies write their source document,
not the displayed top-level Assembly. Unknown names return `named_view_not_found`.

`camera` is complete:

```json
{
  "rotation": [0.70710678, 0, 0, 0.70710678],
  "zoom": 3.25,
  "pan_x": -17.5,
  "pan_y": 41.25,
  "reference_scale": 7.5
}
```

`rotation` is quaternion `w, x, y, z`; this example rotates X toward positive Y.
`pan_x/pan_y` are logical viewer pixels. Scales have MeshView semantics, not geometry
accuracy/import tolerance. A view refers to the displayed scene, not topology or
component placement.

`view.named.set` stores supplied data; `get/list` return it without changing the live
camera. Dialog restoration uses all eight stored values. Seven standard directions
remain viewer functions, not user-stored entries.

## Shared transaction

Qt-free `document/named_views.hpp` validates/serializes records.
`workspace/named_view_operations.hpp` is shared by GUI and command host. Replacing a
name preserves list position; saving identical state creates no transaction or Undo.

Names are 1–1024 UTF-8 bytes without control characters or surrounding spaces.
All eight numbers must be finite, both scales positive, and quaternion nonzero.
Unnormalized quaternions are normalized. All fields are required; unknown fields
and duplicate names are rejected before publication.

Edits are metadata with `body_calculated=false`: no OCCT, mate solving, or dependency
regeneration. Calculated geometry remains shared unchanged. Undo/Redo restores the
list and full saved camera state.

The dialog commits through the shared operation before updating its list. Errors
appear inside the window, preserving pending names or selected deletion items.
Existing behavior remains: Save/Delete explicitly modify bookmarks; Cancel restores
the pre-dialog camera, while OK retains the selected view.

## Native files and verification

Records persist only in `.prtz/.asmz` as `named_views`: name, four `rotation`
components, `zoom`, `pan_x`, `pan_y`, and `reference_scale`. At this stage Part uses
INI **20** / JSON **44**, Assembly INI **19** / JSON **28**. Start templates and shared
test documents were updated. Extensions remain; no required additional files or
legacy migration. `.drwz` is unchanged.

`zima_cpp_named_view_command_tests` verifies:

- Independent geometry calculation of a quarter-turn quaternion's effect.
- Part/Assembly exact camera state after saving/reloading.
- Invalid data, atomicity, queries, no-ops, Undo/Redo.
- Preserved calculated bodies and Assembly mates.
- Activated subassembly and inactive-document write rejection.

GUI opens the actual dialog, captures rotated/panned camera state, saves/reopens,
selects the view, and checks all eight values. It compares full native files from
GUI/CLI and tests errors/deletion. It also checks workspace destruction with Views
still open: the dialog must be destroyed while window members used by its `destroyed`
callback remain alive.

Both applications and all tests built. Focused model/GUI passed **2/2 in 129.39 s**.
Full regression passed **161/161 in 659.35 s**, including actual CLI, console,
workspace startup, native files, modeling, Assemblies, drawings, and Sketches.

Local logs:

- `build/named-view-final-build.log`
- `build/named-view-final-focused-tests.log`
- `build/named-view-full-regression.log`
