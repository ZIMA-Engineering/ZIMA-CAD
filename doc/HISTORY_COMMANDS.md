# Part history from GUI, console, and CLI

Status: 2026-09-11. Six commands share `workspace/history_operations.hpp`; the tree
no longer duplicates Part suppression, movement, deletion, or cursor logic. Pure
order/dependency checks are in `workspace/history_policy.hpp`. At this stage Assembly
uses these from GUI; Assembly mutation commands are a later stage.

## Commands

| Command | Arguments | Purpose |
| --- | --- | --- |
| `history.list` | `[document]` | Feature order, IDs, names, kinds, owning Bodies, suppression, cursor, last calculation errors |
| `history.suppress` | `object suppressed [document]` | Set boolean suppression, calculate, commit history |
| `history.delete` | `object [document]` | Delete feature, standalone Sketch, construction, Body, or Boolean |
| `history.move` | `object [before document]` | Move before an object in the same history scope; omitted before means end |
| `history.can_move` | `object [before document]` | Check order, dependencies, ownership without geometry calculation |
| `history.cursor` | `index [document]` | Set zero-based insertion index in complete Part history |

Commands use stable ZIMA IDs, not labels or OCCT edge indexes. Mutation `document`
must be active; queries can read another open Part. Edits inside Bodies require
activation. Bodies/Booleans move in main Body order; features move only within their
own Body. Cross-Body transfer is not reordering and is unsupported here.

`history.cursor` indexes complete Part history and respects active Body scope.
`body.cursor` instead offers local Body indexes or main Body/Boolean indexes.
Neither calculates geometry.

```json
{"command":"history.suppress","arguments":{"object":"<feature-ID>","suppressed":true}}
{"command":"history.can_move","arguments":{"object":"<feature-ID>","before":"<next-feature-ID>"}}
{"command":"history.cursor","arguments":{"index":0}}
```

## Transactions and references

Calculation uses a working copy and one history commit. Invalid types, foreign scope,
dependency-order violations, and pre-commit exceptions preserve document/history.
GUI/CLI share Undo/Redo. Repeated identical values neither calculate nor create revisions.

Movement protects valid dependency order, Body references, and Boolean inputs. After
calculation it checks persisted references, original geometry, and construction,
container, and external Sketch-reference validity. `history.can_move` checks data only;
success cannot guarantee future geometry calculation. Responses contain `allowed`
and `would_change`; rejection supplies a specific error code.

Deletion retains existing downstream Fillet/Chamfer repair: edges may reconnect to
input before the deleted feature only with one unambiguous persisted-geometry match.
New, changed, or ambiguous edges are never guessed.

Suppression/deletion may leave downstream features uncalculable, matching GUI behavior.
The document retains the change and valid preceding geometry. Commands return
`ok:false`, `code:calculation_errors`, and **`data.changed:true`** with an error map,
explicitly distinguishing committed changes with calculation errors from rejected
transactions. Undo or reference repair remains available. Ordinary CLI batches stop;
`--keep-going` continues. Saving is always explicit.

Queries, cursors, and move previews invoke no OCCT. Operations do not regenerate
parent Assemblies. Placement solving, formats, and start templates are unchanged.

## Verification

Models check actual box volumes, stable references around reorder/suppression/deletion,
correct cursor insertion, Undo/Redo, inactive-Body/Boolean protection, invalid arguments,
and save/load. Two-chamfer scenarios verify that deleting the first leaves the second
working on a surviving edge. A chamfer on a newly generated edge checks reported
restoration of valid preceding results after source loss, error maps, and Undo.
GUI uses actual tree, dragging, cursor, and Suppress/Delete menus. Actual CLI reads
resulting native files.

Full Windows Release passed **61/61 in 386.26 s**, `build/history-full-tests.log`:
Qt-free models, actual CLI, GUI history, existing tree-drag regression, modeling,
Assemblies, drawings, splines, and offsets. GUI/CLI built from identical final source.
Expanded edge regression also passed (`build/history-edge-tests.log`, 0.47 s).

## Contextual references (2026-09-13)

Part-history commit now uses the shared reference/Assembly-summary transaction.
Deleting features, Bodies, or owned Sketches removes only demonstrably unused
dependencies, checking closed native Parts in the same branch too. Part Undo/Redo
prepares matching summaries before history mutation. `history.can_move` respects an
explicit inactive Part even with Assembly active, without activation/geometry changes.
See [CONTEXT_REFERENCE_TRANSACTIONS.md](CONTEXT_REFERENCE_TRANSACTIONS.md).

Independent Assembly Undo/Redo preserves current source Parts and recalculates only
derived dependency lists, not geometry. Unverifiable dependencies remain; resulting
cycles reject the step before publication. GUI reports errors and permits retry after
sources become available. See [ASSEMBLY_REFERENCE_SUMMARIES.md](ASSEMBLY_REFERENCE_SUMMARIES.md).
