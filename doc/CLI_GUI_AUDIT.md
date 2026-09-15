# Final GUI and CLI audit (2026-09-15)

The scope is command coverage for currently supported Part, Assembly, Drawing,
and template-editor model operations. Domain coverage and incremental results
are in [CAD_COMMAND_COVERAGE.md](CAD_COMMAND_COVERAGE.md).

## Direct GUI writes

The audit of production document-session writes distinguished model operations
from mouse-driven publication of already prepared results. Three separate inline
paths are now unified: thread catalog selection, component mate offset, and
3D curve / Sweep radius. See [INLINE_DIMENSION_COMMANDS.md](INLINE_DIMENSION_COMMANDS.md)
for details and regressions.

Remaining direct writes have these counterparts:

| GUI path | Model result available by command | GUI adapter responsibility |
| --- | --- | --- |
| `assembly_drag.cpp` | `component.set` with `placement` | Project the pointer into the plane and available degrees of freedom, solve preview, commit drag or pass proposal to Properties |
| `sketch_drag.cpp`, point | `sketch.point.move` | Coordinates from the shared ray/plane intersection, native `Sketch::move_point`, publish resulting proposal |
| `sketch_drag.cpp`, corner fillet | `sketch.corner_fillet.create` | Pointer-derived radius, native `Sketch::add_corner_fillet`, commit result |
| `sketch_drag.cpp`, dimension placement | `sketch.dimension.set` with `position` | Store label placement; this is not a new value manipulator |
| `sketch_drag.cpp`, existing reference drag | `component.set` with `placement_references` | Project linear/angular offset, apply limits, solve native mates, publish preview |
| `primitive_properties.cpp` / `sketch_document.cpp`, owned profile | `extrusion/revolution.create/set/sketch.edit` | Switch between Properties and owned Sketch, temporary wrapper, return to feature editor; shared operation commits the resulting profile |
| Sweep and section Sketches | Sweep commands and `section.sketch.edit` | Independent Sketch proposal passed to the owning operation's OK |

These inputs need no commands simulating button presses, cursor positions, or
individual preview frames. CLI supplies final coordinates, references, and
parameters. The existing console context adapter provides hover and selection;
batch processes use explicit IDs and occurrence paths.

The audit does not relocate the solver or change shared placement, formats,
regeneration rules, or source-document ownership. Assembly Extrusion and
Revolution remain exclusively subtractive.

## Completion scope

Command coverage concerns functions currently supported by the program. AI and
voice integration are separate subsequent stages. Relation-driven dimensions and
family-variant generation are not yet GUI model features; existing commands
manage their native tables.

`export.view` captures the existing View through a host adapter. A standalone batch
process without a View returns the documented `view_unavailable` error. Geometry
and sheet exports have separate data commands and require no widget control.

A comprehensive Undo/Redo audit across gesture combinations and transitions
between editors remains a separate task in the agreed modeling-feature order.
Each converted operation already has relevant history and invalid-input regressions.
Completed command coverage does not imply that the software contains no further bugs.

## Evidence

- Model and process tests check commands, stable references, ownership, geometric
  results, serialization, and rejected changes.
- `console_ui_verification.cpp` checks actual controls, shared operations, saved
  files, and repeated occurrences.
- `zima_cpp_workspace_startup_contract` covers gestures, open Properties,
  confirmation/cancellation, and walkthroughs of individual tools.
- The final Windows Release suite and exact results are recorded in
  [CAD_COMMAND_COVERAGE.md](CAD_COMMAND_COVERAGE.md).
