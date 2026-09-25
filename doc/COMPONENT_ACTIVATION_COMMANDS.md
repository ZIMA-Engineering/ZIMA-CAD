# Component activation in Assembly context

`component.activate` and `component.deactivate` share GUI activation. Activation is temporary Workspace state, changing
neither document formats nor templates.

## Commands and addressing

```json
{"command":"component.activate","arguments":{"document":"TOP_ASSEMBLY_ID","instance_path":"EXACT_PATH_FROM_COMPONENT_LIST"}}
{"command":"context"}
{"command":"extrusion.set","arguments":{"container":"SOURCE_EXTRUSION_ID","length_forward_mm":40}}
{"command":"save"}
{"command":"component.deactivate"}
```

For activation, optional `document` identifies the **displayed top-level Assembly**,
defaulting to the current displayed document. `instance_path` is the full exact path
from it; obtain it from recursive `component.list` with explicit top-level Assembly ID.
Never construct paths from names. Activating a derived copy resolves to the original
editable occurrence.

Results contain edited-source `document`, `displayed_document`, canonical
`instance_path`, `opened`, and `changed`. Repeated activation of the same occurrence
returns `changed=false`. Repeated Parts share source IDs but have different activation
paths. `context.active_occurrence` returns this path in actual CLI and GUI console.

Activated Parts accept model commands on their own objects. Activated subassemblies
accept Assembly commands on immediate components. Here,
`component.list/get/set/remove/dependencies` uses local paths from the active source
Assembly, with optional `document` identifying that source. After activating a
subassembly, call `component.list` and pass its returned paths to `component.set`.
A full top-level path is not a local `component.set` address.

`component.insert` and GUI insertion target the exact active owning subassembly.
GUI then opens the same new-occurrence Properties in full Assembly context.
Dependency cycles remain forbidden.

`component.deactivate` returns editing to the displayed top-level Assembly, or returns
`changed=false` if none is activated. By contrast, `component.open` and
`activate DOCUMENT_ID` open the source in its own tab and end contextual activation.

## Ownership, history, and display

- Activation opens a missing native source without body calculation or mate solving.
  Deep paths need not open intermediate Assemblies as tabs.
- Open sources are authoritative, including unsaved edits; activation never overwrites
  them from disk.
- Modeling, Undo/Redo, saving, and explicit Regenerate act on the active source.
  The top-level Assembly remains visible as context. Refreshing current-source
  display does not recalculate its mates or operations.
- Closing the active source returns editing to the top-level Assembly. Closing the
  top-level Assembly leaves the active source open in its own tab. Closing unrelated
  documents through CLI preserves exact activation.
- Open editing blocks activation like other mutations. Mismatched active source
  and persisted path blocks model mutation.
- Source reads through the GUI event loop track exact activation paths. A newer
  switch between occurrences of the same source must not be overwritten by a pending
  read. Read/identity failures are rejected before insertion.

Activation exposes already implemented commands according to ownership. Contextual
external-reference creation in another Part is a subsequent stage, not completed
by this change. The known mate-chain ordering repair is separately documented in
[ASSEMBLY_MATE_ORDER_REVIEW.md](ASSEMBLY_MATE_ORDER_REVIEW.md).

## Verification

Activation, source-opening, and host model tests passed **3/3** (0.64 s),
`build/component-activation-model-tests.log`.

Regression compares volume 6000 → 8000 mm³ after height 30 → 40 mm for a 10 × 20 mm
footprint, Undo/Redo, and saved native source. It covers repeated deep occurrences
with intermediate tabs closed, unchanged top-level revision/placement, local insertion/
editing/removal, cycles, invalid contexts, idempotence, and document lifecycle.
A separate test checks derived sources and occurrence changes during reading.

Full GUI/CLI/test build: `build/component-activation-all-build.log`. Actual CLI and
ordinary console passed integration. After correcting its tree-root assertion,
the new GUI scenario passed **1/1** (9.57 s), `build/component-activation-gui-tests.log`,
including real-menu insertion and subassembly Properties.

The first full suite passed **108/109** (490.27 s),
`build/component-activation-full-tests.log`. The older startup scenario tried activating
another component while insertion Properties remained open. It now verifies rejection,
closes with Cancel, then continues. The top-level-return button also uses shared
activation and cannot leave open Properties or Sketches.

After this change, final related tests passed **9/9** (174.88 s),
`build/component-activation-final-tests.log`, including the previously failing full
GUI startup (93.91 s), component Properties, console, actual CLI, translations, and
model contracts. GUI built under `build/component-activation-final-build.log`; other
programs came from this stage's full build.
