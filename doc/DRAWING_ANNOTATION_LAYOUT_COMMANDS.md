# Local model-dimension properties in drawings

`drawing.annotation.get` returns a specific persisted annotation and local layout.
`drawing.annotation.set` edits a model dimension's layout or text style in one view.
Both use drawing data without opening the source Part or calculating bodies.

```json
{"command":"drawing.annotation.get","arguments":{"view":"VIEW-ID","reference":{"source_document":"PART-ID","owner":"OWNER-ID","key":"DIMENSION-ID","instance_path":"OCCURRENCE-PATH"}}}
{"command":"drawing.annotation.set","arguments":{"view":"VIEW-ID","reference":{"source_document":"PART-ID","owner":"OWNER-ID","key":"DIMENSION-ID","instance_path":"OCCURRENCE-PATH"},"layout":{"text_along":3,"text_outward":4,"line_offset":2,"arrows_reversed":true},"style":{"prefix":"REF ","decimals":4,"tolerance_mode":"symmetric","symmetric_tolerance":"0.02"}}}
```

References require all four fields, including `instance_path` (possibly empty for
a direct Part). Obtain them from `drawing.annotation.list`; Part names and dimension
order are not identity. Optional `document` guards the target. A mismatched occurrence
or view must never edit a neighboring annotation.

`layout` partially updates these properties:

- `text_along`, `text_outward`, `line_offset`: model-mm offsets in the dimension
  frame; rendering applies view scale.
- `envelope_offset`: nonnegative distance from the model envelope, or null for default.
- `plane_quarter_turns`: integer 0–3.
- `arrows_reversed`, `radius_center_line_hidden`: boolean.
- `radius_rotation_degrees`: radial-dimension rotation in degrees, inapplicable to
  other kinds. Angular-dimension planes are determined by their measured arms.

`style` partially updates `prefix`, `suffix`, `text_override`, `decimals` (0–12),
`tolerance_mode`, `symmetric_tolerance`, `single_tolerance`, `upper_tolerance`, and
`lower_tolerance`. Tolerance mode is empty, `symmetric`, `single_deviation`, or
`deviations`. Deviations are text as in Properties. Overridden display text does
not change the measured value. `set` requires at least `layout` or `style`.

Results contain original `model_layout`, optional `view_layout`, effective `layout`
and `style`, measured `value`, displayed `text`, visibility, `unresolved`, `editable`,
`dimension_kind`, identity, and revision. Edits also return `changed`. Axis and
construction annotations can be read but do not support these dimension properties.

GUI Properties and commands share one operation: validate the proposal, recalculate
only annotation projection, and commit one Drawing change. Old manual paper-coordinate
handles are cleared as previously in GUI. Value, geometric reference, model dimension,
and default model layout stay unchanged. Undo/Redo restores the whole local edit.

If the source dimension is missing but its last geometric display remains in the
drawing, local appearance can still be edited while retaining `unresolved`. Source
refresh updates the actual value and preserves local layout. Ambiguous references,
wrong types, unknown fields, and invalid values are rejected without partial changes.

Everything stays in existing `.drwz`; formats and start templates are unchanged.

Full Windows Release regression passed **146/146 in 587.41 s**,
`build/annotation-layout-full-tests.log`. After explicit validation of the fixture's
correctly oriented camera, final model, standalone CLI, and actual GUI tests passed
**3/3 in 27.52 s**, `build/annotation-layout-verified-tests.log`. Production code did
not change between runs; both applications and all targets built.
