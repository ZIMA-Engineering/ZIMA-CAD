# Named views in GUI and CLI

Implementation status: 2026-09-29. Named Part/Assembly views store complete camera
state. The original dialog wrote only pan and scales, losing rotation after reopening.
Assembly also read `named_views` but did not write it.

## Initial datum display scale (2026-10-02)

The viewer establishes its screen-constant datum baseline only after plane or
Origin-axis display extents are available. An earlier scene containing a point or
body alone must not freeze the fallback extent; doing so made later Origin planes
and axes excessively large. Existing initialized camera states retain their datum
baseline across zoom and Fit. This changes presentation only, with no model,
placement, reference identity or localized text changes.

Initialization also runs when a scene refresh preserves the camera and skips
Fit. The actual new-document GUI reproduced this path with a 5 mm display axis
and a stale 1.4 baseline, leaving the plane borders outside the viewport.
Publishing the first datum packet establishes its baseline without changing
camera orientation, zoom or pan.

`zima_cpp_origin_camera_tests` covers empty, point-only and body-first scene setup,
followed by Origin display with and without Fit, zoom and Fit. The regression
failed before the fix. The new-document GUI check also measures the actual
Origin scale in every supported language.

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

The Normal View dialog stages additions and removals locally. Its Save control
captures the current camera into the pending list; each custom row has the shared
remove cross. Built-in directions cannot be removed. OK commits the complete list
as one metadata transaction and retains the selected camera. Cancel discards the
pending list and restores the pre-dialog camera. An unchanged OK creates no Undo.

Normal View, the Standard Views menu/toolbar and Drawing view properties use the
same localized standard direction labels. Custom views belong to the source Part
or Assembly and appear under their exact user-authored names. The model selector
restores the full camera; Drawing uses its orientation only, preserving the Drawing
view's own scale and sheet position. Drawing persists that selected orientation in
its existing camera data, so subsequent bookmark edits do not silently alter an
already placed view. Open source documents are authoritative.

Reference text arms entry or replacement; an independent eye inspects the stored
reference. The first reference already establishes a normal view, and the optional
second reference controls orientation. A short middle click ends entry and clears
inspection without deleting values. Planes use their persisted reference geometry;
no kernel calculation is required to obtain their normal.

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
