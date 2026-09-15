# Component removal in CLI and GUI

`component.remove` removes an exact immediate occurrence in the active Assembly.
It shares `workspace::remove_component` with context-menu Delete. The catalog has
**207 commands** at this stage.

```json
{"command":"component.dependencies","arguments":{"instance_path":"EXACT-PATH"}}
{"command":"component.remove","arguments":{"instance_path":"EXACT-PATH"}}
```

Use paths from `component.list/get`. Parents cannot remove internal Parts of inserted
subassemblies. Optional `document` must match the active Assembly. Dialog/Sketcher
editing rejects the command. Results contain `occurrence`, `instance_path`, `document`,
`revision`, `removed:true`, and `changed:true`. Missing occurrences are errors, not
successful deletions.

## Preserved rules

Dependency checks match existing GUI and `component.dependencies`. Removal is blocked
by mate-row use, another occurrence's dependency, or Assembly Sketch external references.
This includes the removed component's own placement rows: remove them first through
Properties or `component.set` with empty `placement_references`. Derived copies block
source removal; copies can be removed independently. There is no bypass flag or
cascading deletion of dependent objects.

Source documents/files and other occurrences remain. Removal creates one Undo/Redo
step preserving original stable occurrence identity. GUI retains confirmation;
explicit CLI executes directly like other model commands.

## Atomic Assembly preparation

Previously, GUI published regenerated sources into the live Assembly before deleting
an ordinary occurrence and updating/calculating sections. The shared transaction
uses the same dependency preparation but retains a private candidate until the full
removal succeeds. `Workspace::prepare_assembly_calculation` merely exposes existing
candidate calculation; solver, order, and references are unchanged. Ordinary regeneration
continues using the same preparation.

Ordinary occurrences are removed from current input source data, including unsaved
calculated Parts, without saving/recalculating source Parts. Derived-copy removal
retains the existing persisted-Assembly branch. After occurrence removal, existing
policy updates section targets, removes owned dependencies, solves placement, and
calculates sections, then commits once. Preparation, section, or physical-relation
failure leaves no partial positions, source packets, history, or generation changes.

Sections receive fresh inputs before cutting; rollback inputs contain only remaining
occurrences. Removing the last target retains the section definition with an empty
target list. Existing `extrusion.target_face` loss policy for `UpToPlane/UpToSurface`
converts to blind termination and clears that target. This stage does not redesign
opening termination or general container placement.

Data stays in existing `.asmz`, `.prtz`, and persisted references. Formats and start
templates are unchanged.

At this stage, mate-chain ordering and its separately pending approval were tracked
in [ASSEMBLY_MATE_ORDER_REVIEW.md](ASSEMBLY_MATE_ORDER_REVIEW.md). Component removal
did not perform or bypass that then-unapproved solver change.

## Verification

Initial build and both focused model tests passed **2/2** (0.83 s):
`build/component-removal-model-build.log`, `build/component-removal-model-tests.log`.
Properties tests were rebuilt from the committed version without the deferred mate-chain reproduction.

Removal tests check exact occurrences, every blocking dependency group, active editing,
nested paths, unsaved sources, preserved source files, Undo/Redo, derived copies, and
last section targets. A 3000 mm³ body with a 200 mm³ rectangular cut correctly retains
2800 mm³ after another target is removed; rollback input is 3000 mm³ and native output
is verified. Tests explicitly fail a physical relation after successful preparation
of new sources, which previously could partially mutate the live Assembly.

Both applications and test programs built. Related tests passed **20/20** (192.48 s):
`build/component-removal-all-build.log`, `build/component-removal-related-tests.log`.
Coverage includes actual CLI, console, context-menu confirmation/Undo, full startup
GUI contract, source documents, Assemblies, imports, sections, derived copies, and
translations. The suite now has 108 tests; this stage ran 20 related tests, not a new
full run of all 108.
