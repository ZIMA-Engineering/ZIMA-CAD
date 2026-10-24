# Feature modeling commands

Ordinary Default modeling uses `feature.create`, `feature.get`, `feature.set`,
and `feature.sketch.edit` in both Part and Assembly. Creation converts an owned
standalone Sketch while preserving its container, Origin, Body and history
position. The Sketch and feature IDs are not inferred from display names.

The former ordinary `extrusion.*` and `revolution.*` commands are removed, with
no compatibility aliases. Sheet Cut and Sheet Revolution remain distinct Part
operations through `sheet_cut.*` and `sheet_revolution.*`. They share geometric
implementation with Feature without offering a switch into an ordinary feature.
Their native kernel request types are still required by the implementation.

## Definition and units

`parameters` uses the current native Feature definition. Omitted root fields
retain their existing/default value. An array replaces that entire array: send
both complete side definitions when changing `sides`. Obtain an existing full
object from `feature.get` at `data.parameters`, change the required values, and
send it back through `feature.set`. Do not change its `sketch_id`.

This example creates an extrusion of 20 mm toward End and 5 mm toward Start:

```json
{
  "command": "feature.create",
  "arguments": {
    "sketch": "SKETCH-ID",
    "parameters": {
      "symmetric": false,
      "sides": [
        {"operation":"extrude","length":20,"extrusion_extent":"length","angle_degrees":90,"rotation_extent":"angle","draft_angle_degrees":0,"targets":[]},
        {"operation":"extrude","length":5,"extrusion_extent":"length","angle_degrees":90,"rotation_extent":"angle","draft_angle_degrees":0,"targets":[]}
      ]
    }
  }
}
```

Lengths are finite JSON numbers in mm; angles are degrees. A side operation is
`none`, `extrusion`, `revolution`, or the supported first-side `twist`. Extrusion
uses `length` and `extrusion_extent` (`length`, `through_all`, `up_to`). Rotation
uses `angle_degrees` and `rotation_extent` (`angle`, `full`, `up_to`). Revolution
uses a construction centerline from the owned Sketch and persists its exact ID in
`axis_segment_id`. Axis resolution uses the existing first-valid-centerline rule.
The total active rotation must not exceed a full turn.

`symmetric` evaluates both sides from the End definition while retaining the
independent stored Start settings. End/Start identities remain distinct at zero
offset. Two independently authored sides must not be treated as a single original
sweep identity merely because they meet at the Sketch plane.

`result_type` is `solid`, `thin`, or `surface`. Thin uses `thin_thickness` and
`thin_mode` (`one_side`, `other_side`, `symmetric`). Closed contours offset into
walls; supported open contours close at their original endpoints. Invalid offsets
are rejected rather than silently converted into solids. Surface cannot subtract.
See [Surface profiles](SURFACE_PROFILES.md).

## Limits and placement

An active `up_to` side requires exactly one current original target. A target
object includes `kind` (`point`, `plane`, `face`), `owner`, `key`, `instance_path`,
`label`, `origin`, `normal`, and `triangles`, matching the serialized definition.
Fallback geometry is a native snapshot; explicit calculation resolves authoritative
original geometry rather than trusting stale numeric coordinates. Queries do not
run the kernel. Construction planes use their entity ID and `plane` key. Origin
planes use the Origin ID and `origin:plane:xy`, `origin:plane:xz`, or
`origin:plane:yz`. Part sources precede their consumers. Assembly targets retain
the complete exact occurrence path. Curved faces cannot masquerade as planes merely
because their display tessellation is coarse.

Through All is a subtraction limit. One active side starts at the Sketch plane;
cutting both directions requires both sides or symmetry. Front/Back orientation
uses the shared placement contract, not a separate modeling direction flag.

The optional top-level `placement` contains the shared numeric placement patch.
`profile_plane` accepts `AUTO`, `XY`, `XZ`, or `YZ`. The Sketch offset is
`parameters.profile_plane_offset`. Numeric locks use `profile_offset`,
`thin_thickness`, `side0_length`, `side1_length`, `side0_angle`, `side1_angle`, and
the corresponding `side0_draft_angle` / `side1_draft_angle` keys.

Use [placement reference commands](PROFILE_REFERENCE_COMMANDS.md) for original
position and orientation references, including removal. Source data, side choice,
reference identity and dependent history remain in the native document.

## Transactions and Assembly cuts

A Part edit requires the active editable owning Body. Assembly modeling always
subtracts from selected immediate Part occurrences. Top-level `targets` contains
those occurrence IDs; omitted creation targets select current unsuppressed editable
immediate Parts, while omitted edit targets retain the stored list. Duplicate,
unknown, derived, or internal subassembly targets are rejected. Source Parts are
read-only inputs, including their current unsaved calculated geometry.

Creation, changed definitions and owned-Sketch batches use the existing common
profile transaction. Invalid requests publish no partial history, geometry or
source refresh. Each committed edit has one Undo/Redo step. Unchanged confirmation
must not calculate or create history. `feature.get` and `assembly.cut.list` are
read-only. Assembly cut results retain their real input boundaries for Properties
rollback; downstream items remain transiently suppressed during editing.

## Atomic owned-Sketch edits

```json
{"command":"feature.sketch.edit","arguments":{"container":"FEATURE-ID","operations":[{"command":"sketch.point.move","arguments":{"point":"POINT-ID","position":[12,0]}}]}}
```

A batch contains 1–1000 supported Sketch commands against a private copy. Inner
commands omit `sketch` and `document`; document commands are unavailable. An inner
failure reports its zero-based `operation_index`. Final calculation failure also
publishes nothing. A changed successful batch calculates and commits once; an
unchanged final definition creates no transaction. The response includes ordered
`results`, `changed`, and `body_calculated`.

## Sheet-specific commands

`sheet_cut.create/get/set/reference.set/sketch.edit` enforce a Part-owned Sheet
Cut with subtraction and a closed solid profile. `sheet_cut_clearance` selects
profile clearance versus surface projection. Existing independent lengths,
directions, limits, target references and sheet precision remain supported.

`sheet_revolution.create/get/set/reference.set/sketch.edit` enforce a Part-owned
additive thin Sheet Revolution. Axis, angles, extent, thin side, inherited thickness
and `thickness_override` retain their established sheet meaning. The public API
accepts neither a `sheet_cut` nor a `sheet_metal` enable/disable switch.

Historical development results are retained in
[profile milestones](history/PROFILE_COMMAND_MILESTONES.md); current release
acceptance is recorded separately after the complete verification gates pass.

The public side operation is `extrude` (action name); `extrusion` is rejected.
Native document enum identifiers remain unchanged. `feature.create`, `feature.set`
and `feature.get` share the same parameter schema for solid, surface and thin results.
