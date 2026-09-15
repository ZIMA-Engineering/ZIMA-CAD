# View labels and section-line endpoints through CLI

`drawing.view.labels.get` reads persisted view-caption, section-label, and both
section-line endpoint positions. `drawing.view.labels.set` changes them in one
Undo step. Both require `view`; optional `document` identifies an open Drawing.
Mutations use the usual active-document and open-editor guards.

```json
{"command":"drawing.view.labels.set","arguments":{"view":"VIEW_ID","values":{"caption_position_mm":[12,-7],"section_label_position_mm":[-3,14],"markers":[{"section":"SECTION_ID","offsets_mm":[3,-2]}]}}}
```

- Label positions are `[x,y]` in **paper millimeters**, relative to the view origin,
  right and up. Model scale does not multiply them. `null` restores automatic
  placement above the outline. Moving a label changes neither visibility nor name.
- `markers` is a partial list of the view's markers. Each item contains exactly
  `section` and `offsets_mm`. `[first,second]` gives each end's signed shift along
  the section line: positive extends, negative shortens. `null` removes both custom offsets.
- Like mouse dragging, offsets are constrained by shared `section_trace_layout`
  minima. The first end is adjusted before the second, which respects the new
  shared-segment length. At least the existing renderer's permitted length remains;
  clients do not supply an estimated minimum.
- Queries return persisted `offsets_mm`, actual `effective_offsets_mm`,
  `minimum_offsets_mm`, and `displayable`. Without a displayable trace, derived
  values are `null`. Automatic placement can be restored, but supplying a custom
  endpoint array returns `trace_unavailable`.
- Omitted values stay unchanged. Empty objects and identical values create no
  history. Implicit zero offsets are not unnecessarily pinned. Unknown items,
  duplicates, wrong types, and nonnumerical/nonfinite values are rejected.
  Failure even on the final marker leaves the entire document unchanged.

GUI dragging and commands use `set_drawing_label_position` and
`set_drawing_section_end` in `drawing_label_operations`. Drag offsets remain preview
state until mouse release commits history. Commands use a private Drawing copy and
commit only the complete valid change.

No Part/Assembly loading, OCCT, new projection, or changes to dimensions/view geometry
occur. Trace data comes from the same persisted view and other views on **the same
sheet** as rendering. Results use existing `.drwz` fields; formats and templates are unchanged.

Tests cover actual endpoint distances, 2:1 scale, minimum shortening, missing sources,
atomic errors, edit guards, reset, Undo/Redo, and native saving. A separate CLI process
opens, queries, edits, performs Undo/Redo, and saves. GUI reads actual drag results
through commands and checks visible handle positions after commands, Undo, and Redo.
