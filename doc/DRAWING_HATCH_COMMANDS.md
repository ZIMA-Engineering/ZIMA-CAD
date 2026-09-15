# Drawing section hatching in GUI and CLI

## Commands

`drawing.view.hatch.get` requires `view`, with optional `document` and `limit`
(1–10000, default 2000). It returns current section settings from the source Part
or Assembly and independent hatch visibility in the requested view. It reads the
source from an open document or its native file relative to the Drawing path.
It does not project geometry, open a document or change history. Use
`drawing.view.get` for saved view state alone when the source is unavailable.

The result includes `source_document`, `section`, `items`, `total`, and units mm
and degrees. Each item reports its exact `component` key, name, `available`, view
`mode`, independent `source_mode`, `hidden_in_view`, `custom_hatch`, and effective
`hatch` parameters. The key is a Body ID in a Part and a complete occurrence path
in an Assembly. Two occurrences of the same screw are separate items. Previously
saved unavailable components are identifiable by `available: false`.

`drawing.view.hatch.set` requires `view` and a nonempty `components` array, with
optional `document`. It edits only listed items. Each component may occur once
per batch; batches contain at most 10000 items.

```json
{
  "command": "drawing.view.hatch.set",
  "arguments": {
    "view": "<view ID>",
    "components": [{
      "component": "<Body ID or exact occurrence path>",
      "mode": "cut_only",
      "hatch": {
        "pattern": "cross",
        "angle_degrees": 30,
        "spacing_mm": 3.125,
        "offset_mm": 0.375
      }
    }]
  }
}
```

## Parameter ownership

The Properties table and command use identical mode semantics:

| `mode` | Drawing view | Source section |
| --- | --- | --- |
| `cut_hatch` | Section with hatching | Preserves independent 3D visibility; changes `uncut` to cut if needed |
| `cut_only` | Section without hatching in this view only | Preserves independent 3D visibility; changes `uncut` to cut if needed |
| `uncut` | Component without sectioning | Sets the source component mode to `uncut` |

Style belongs to the source section's component. Changing it updates all views of
that section in the edited Drawing, preserving each view's own hidden hatches.
Other open Drawings refresh calculated projections on explicit regeneration.
Switching tabs performs neither projection nor body calculation.

`hatch` accepts any nonempty subset of parameters. Pattern is `parallel`, `cross`
or `dashed`; paper spacing is 0.1–100 mm. Angle in degrees and offset in mm must
be finite. JSON numbers are stored without rounding to the control's display
precision. Supplying `hatch` enables custom style. `custom_hatch: false` restores
inherited style, including alternating adjacent-component directions, and cannot
be combined with new `hatch` values in the same item.

## Confirmation, history and files

GUI and CLI use `workspace::set_drawing_section_components` and
`prepare_section_component_commit`. The component parser is shared with
`section.create/set`.

The complete batch and a private Drawing draft, including all affected projections,
are validated first. Source and Drawing are committed only after success. Invalid
components, incorrect spacing or a dependent-view failure leave no partial edit.
Identical settings are a no-op without new history. `changed` reports a change;
`source_changed` distinguishes source edits from local hiding. `body_calculated`
is always `false`.

A closed source is opened only when committing an actual source-parameter change.
The Drawing remains active. The source is marked modified, never automatically
saved to disk. Local hatch hiding does not open a source window. Source ID must
match even if another file occupies its path or another document is open.

Drawing and source retain **separate Undo/Redo histories**. Drawing Undo restores
saved projections and local visibility; source Undo restores source style. Save
both documents after changing style. There is no new shared multi-document history.

Prepared commits reject a source changed in the meantime, including Undo to the
same revision and closing/reopening the same document. They never overwrite new
edits with an old model. Bodies, original references, section sketch and shared
source geometry are preserved. OCCT is not used.

All settings remain in existing `.prtz`, `.asmz` and `.drwz` files. This step
changes neither formats nor templates.

## Verification

`drawing_hatch_command_tests` covers reads, local hiding, exact styles, inheritance,
`uncut`, invalid batches, late projection failure, Undo/Redo, stale edits, native
files, opening closed Parts/Assemblies, source identity and nested repeated
occurrences. It also checks calculated-body preservation and source-geometry
sharing. `cli_process_tests` runs the real command line without GUI initialization.
`section_ui_verification` checks GUI → CLI → the same GUI Properties in both
directions and preserves Cancel.

The first test fixture lacked a BodyHistory owner and crashed on read. After
fixing fixture construction, the new model suite passed 1/1 in 0.32 s. Initial
integration passed 7/8 in 37.07 s and exposed an incorrect GUI-test assumption
about two-decimal display. After separating display checks from stored precision
and restoring selection after console mutation, GUI and translations passed
2/2 in 16.82 s. The subsequent **full suite passed 130/130 in 542.95 s**.
Both applications and all tests built. Logs:
`build/drawing-hatch-full-build.log`, `build/drawing-hatch-final-build.log`,
`build/drawing-hatch-model-tests.log`, `build/drawing-hatch-integration-tests.log`,
`build/drawing-hatch-gui-tests.log`, `build/drawing-hatch-full-tests.log`.
The catalog contained 234 commands at this milestone.
