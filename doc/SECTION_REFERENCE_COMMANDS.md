# Original section placement references

`section.reference.set` assigns an original reference to an existing Part or Assembly
section. It uses approved shared reference-field assignment and the transaction
used by Section Properties OK. The placement-solving algorithm is unchanged.

```json
{
  "command": "section.reference.set",
  "arguments": {
    "object": "<section-ID>",
    "index": 0,
    "reference": {
      "owner": "<original-object-ID>",
      "key": "<original-geometry-semantic-key>"
    },
    "offset_mm": 2,
    "flip": false,
    "derive_orientation": true
  }
}
```

Obtain `reference` from `reference.list/get`. In Assembly it also contains the exact
`instance_path`, including all subassembly levels. Part references are local without
an occurrence path. Command, argument, and error-code names remain English in every language.

## Behavior

- `index` 0–2 identifies position fields; 3–4 are independent FRONT/TOP fields.
- Nonzero `offset_mm` is allowed only for face position references. Default is zero;
  `flip` defaults to `false`.
- `derive_orientation` defaults to `true` and may add an independent orientation
  reference as shared GUI assignment does. `false` disables this addition without
  overwriting stored orientations.
- Replacing a locked position reference preserves its lock and uses distance measured
  from current placement under the shared contract.
- Own section geometry, missing sources, duplicate assignment, invalid fields, and
  unsolvable combinations are rejected without document changes.
- Section, Sketch, Origin, and section-path identities are preserved.
- The document must be both active and displayed, as required by current Section
  Properties. An activated source Part inside a displayed Assembly is not editable
  through this path.
- Open editors block mutation; optional `document` guards against targeting another
  active document.

Results contain section details, `document`, `revision`, `changed`, and
`body_calculated: false`. No-ops add no history; actual changes create one Undo/Redo step.

Commit derives and validates the section from already calculated ZIMA data, invoking
no OCCT, source-body calculation, or Assembly mate solving. Calculated Part geometry
and shared Assembly sources are preserved. Data stays in existing `.prtz`/`.asmz`;
formats and templates are unchanged.

## Verification

`section_reference_command_tests.cpp` measures a section through a 10 × 20 × 30 mm
box: at X = 2 mm, area remains 600 mm² and body volume 6000 mm³. Tests include locked
distance, rejected inputs, original identities, native saving, and Undo/Redo. Assembly
contains two occurrences of the same subassembly; changing exact reference paths
moves the section from X = 3 to X = 28 mm while preserving area and shared source geometry.

The process test runs actual CLI and then reads the saved Part. GUI opens Properties
from the tree and checks command-supplied values, Cancel, OK, Undo/Redo, and original
volume. Final results: [CAD_COMMAND_COVERAGE.md](CAD_COMMAND_COVERAGE.md).
