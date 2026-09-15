# Refreshing Part references in Assembly context

`sketch.reference.refresh` supports existing external references of a Part activated
in an Assembly. It requires the exact persisted top-level Assembly and dependent
occurrence path; another instance of the same Part is rejected.

The command reads current calculated original source edges, points, axes, and
faces. Shared conversion maps them into dependent-Part coordinates, then the
Sketch frame. It calculates no body, Assembly cut, or mate; the result reports
`body_calculated: false`. The refreshed Sketch, linked native curve, trim, and
offset are committed through one existing Undo/Redo step. Assembly dependencies
and their ownership remain unchanged.

An open source is authoritative before saving. A closed Part is loaded privately
from its native file. Missing edges, unavailable files, or mismatched document IDs
mark affected references invalid while preserving their last geometry. Restoring
the same original source repairs them. Malformed geometry is rejected rather than
converted into a guessed replacement reference.

Ordinary Part and root-Assembly references retain their existing path. Format and
templates are unchanged; the catalog remains at **209 commands**. Context-reference
creation/detachment with a shared Sketch/Assembly-dependency commit is the next stage.

## Verification

Initial tests exposed two fixture properties: independently specified analytical
coordinates differ in their final bits after composed rotation, and Assembly
preparation alone does not calculate a root cut. The fixture now normalizes coordinate
representation once and verifies no-op repeated refresh. Its empty cut is in a
subassembly where preparation actually invokes calculation.

Model tests and an actual CLI process then passed **4/4** (18.20 s),
`build/context-refresh-tests.log`, covering:

- 257 exact rational-quarter-circle points after a 0.01 mm source translation,
  linked offset, and unchanged trim intervals;
- all four reference kinds, unsaved/open and closed sources, last-curve retention
  when an edge disappears, and repair when it returns;
- incorrect file identity, forbidden repeated occurrence, unchanged top-level
  Assembly history/generation/geometry, Undo/Redo, and native persistence;
- a standalone CLI process opening an Assembly, activating a Part, refreshing its
  reference, saving `.prtz`, and rejecting another occurrence path.

New context and cycle-validation messages are translated into cs/en/de/fr/ru.

After adding a physically missing native-file case and rebuilding all programs,
the **full suite passed 113/113** (511.11 s). Logs:
`build/context-refresh-all-build.log` and `build/context-refresh-full-tests.log`.
Coverage includes model commands, regeneration, native files, import/export,
references, Sketcher, Drawings, shared GUI dialogs, full startup, console, and all translations.
