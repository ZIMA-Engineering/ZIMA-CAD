# Missing dimension Relations - 2026-10-20

## Behavior and scope

Deleting an Fx dimension left its authored Relation behind. Save's physical
parameter evaluation then rejected the missing dimension and prevented saving
the edited Part. The user approved retaining the source and identifying the
affected rows, while skipping unavailable assignments and their dependents.

RelationProgram now derives a transient executable subset from the current
dimension inputs. A missing reserved `d<number>` target or operand disables its
assignment and all transitively dependent assignments, including dependencies
in conditional guards. Diagnostics retain original line numbers and identify
the unavailable dimension. Restoring its binding makes the original source
executable again. Source text and persisted document format are unchanged.

Ordinary workspace dimension/parameter evaluation and physical parameter
evaluation use this subset. Save still performs physical analysis without
applying driving dimensions, replaying history or changing geometry. Independent
valid expressions such as `mass = model.mass` are published. Skipped parameters
retain their previous text; they must not be treated as newly calculated values.
Factory authoring must still validate every binding and explicitly Regenerate
before accepting an asset.

The Relations editor marks affected assignment rows and shows the first
diagnostic with its dimension identifier. The check is debounced and parses
Relations without invoking OCCT. The new diagnostic is translated into Czech,
English, German, French and Russian using the shared translation catalogs.
Unrelated unknown names, cycles, quantity mismatches, read-only targets and
evaluation-domain errors remain errors. Unit conversion remains strict.

## Windows verification

- `zima_cpp_relation_program_tests`: missing targets, transitive dependencies,
  missing conditional guards, binding restoration, source retention and
  independent invalid expressions. Empty conditionals containing unrelated
  unknown names remain errors even when another dimension is unavailable.
- `zima_cpp_save_properties_tests`: create an actual steel Part and Fx
  dimension, delete that dimension, commit, and execute the threaded workspace
  Save preparation/write/completion path. Mass is 0.047 kg; geometry and native
  BRep packets remain unchanged. The authored source and stale dependent text
  survive save/reopen. Undo restores the dimension; Redo and saving work again.
  An unchanged subsequent Save is a no-op. The existing complete Save suite
  also passes.
- `zima_cpp_relations_editor_contract`: the real dialog marks two affected rows,
  retains its source and commits that text through OK.
- `zima_cpp_translations_contract`: catalog validation and the actual diagnostic
  in the Relations dialog pass in all five supported languages.

Evidence: `build/missing-fx-relations-regressions.log` (three tests, 16.39 s)
and `build/missing-fx-relations-editor.log` (editor test, 1.50 s).

This is finite Windows native and dialog verification. A single integrated
mouse-driven dimension-deletion-to-Save session, native Linux execution and
release-package acceptance remain pending. A missing dimension used only by an
empty conditional block with no assignments is not covered by the filtering
test; do not infer universal recovery for every possible source program.
