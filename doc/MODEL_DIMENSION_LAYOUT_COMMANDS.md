# 3D dimension properties through CLI

Command, JSON parameter, and error-code names are English and independent of
application language. Descriptions, help, and user-facing messages are localized.

`dimension.layout.list`, `dimension.layout.get`, and `dimension.layout.set` operate
on native dimensions in open Parts/Assemblies. GUI Dimension Properties and CLI
commit appearance through the same data operation. Active Sketch editing retains
its existing commands and pending transaction.

```json
{"command":"dimension.layout.list","arguments":{"owner":"BOX_ID"}}
{"command":"dimension.layout.get","arguments":{"reference":{"owner":"BOX_ID","key":"parameter:length"}}}
{"command":"dimension.layout.set","arguments":{"reference":{"owner":"BOX_ID","key":"parameter:length"},"reset":true}}
```

`list` offers parameter identities including zero or currently hidden dimensions.
It accepts optional `owner`, `document`, and `limit` (1–10000, default 2000), returning
`items` and matching `total`. Items are not a visible-geometry list; no scene is built.

`get` returns `reference`, owner name, `has_override`, persisted/default `layout`,
document revision, and `body_calculated: false`. `text_style: null` inherits original
dimension style; standalone CLI does not construct a displayed dimension to discover it.

For editing, copy the complete `layout` from `get`, change desired values, and send
it to `set`:

```json
{"command":"dimension.layout.set","arguments":{"reference":{"owner":"BOX_ID","key":"parameter:length"},"layout":{"plane_quarter_turns":0,"envelope_offset":8,"text_along":4,"text_outward":0,"radius_rotation_degrees":0,"arrows_reversed":false,"line_offset":0,"radius_center_line_hidden":false,"text_style":null}}}
```

Lengths are model mm; radius rotation uses degrees. `plane_quarter_turns` is integer
0–3; `envelope_offset` is nonnegative or `null` for free placement. All numbers must
be finite. Custom `text_style` is a complete object with `prefix`, `suffix`,
`text_override`, `decimals` (0–12), `tolerance_mode`, `symmetric_tolerance`,
`single_tolerance`, `upper_tolerance`, and `lower_tolerance`. Tolerance mode is empty,
`symmetric`, `single_deviation`, or `deviations`; text fields allow at most 2048 UTF-8 bytes.

`reset: true` removes overrides and cannot accompany `layout`. Identical values,
empty reset, and unchanged defaults create no Undo step. Appearance changes affect
neither dimension value, object placement, calculated body, nor original references.
One transaction supports Undo/Redo, without OCCT, source loading, mate solving, or regeneration.

Writing requires the active document, appropriate active Body, and closed editors.
Dimensions of the Body itself may be edited without activating it, matching GUI.
Omitted `instance_path` adopts the exact active occurrence; supplied paths must match.
In an activated Part inside Assembly, appearance is stored in the source Part and
shared by repeated occurrences. Parent Assembly properties/placement are unchanged.
Queries for another open document use its empty local path.

Existing `dimension_layouts` in `.prtz`/`.asmz` remain unchanged, with no auxiliary
files or template changes.

Model tests check actual calculated box geometry/volume, history, invalid inputs,
Body, repeated occurrences, and reopening both native formats. Process tests use
standalone CLI. GUI opens real Dimension Properties and checks command-style transfer,
Cancel, OK, Undo/Redo, and native saving.

## Purple placement-handle repair (2026-09-14)

Coordinate/reference-offset dimensions now supply the actual dimension-plane normal,
perpendicular to measurement and extension-line offset directions. Previously the
field held the measurement direction itself, producing a zero cross product for
translation and preventing purple-handle label movement. Normal sign follows actual
span so negative offsets do not flip extension lines to the opposite side.

Regression covers all three handles, X/Y/Z, and positive/negative offsets. Actual
press/move/release events move labels without changing values or geometry, committing
appearance exactly once on release. Model and `dimension_layout_contract_tests` passed.

This repair covers direct View label editing. At this stage movement remains blocked
with container Properties open. Follow-up work must retain appearance alongside
parameters in the pending transaction: OK commits both in one Undo step, Cancel
leaves no document change. Merely enabling handles over immediate writes would
violate that contract; this transactional extension was not yet implemented at this stage.
